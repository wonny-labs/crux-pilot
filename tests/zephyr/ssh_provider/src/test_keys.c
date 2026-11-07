/* Host and user keys for this test image, generated at each boot. TEST ONLY. */
#include "crux_ssh_wolf_provider.h"
#include "test_support.h"

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/sha256.h>

#include <string.h>

static WC_RNG rng;
static bool rng_ready;

static bool rng_get(void)
{
    if (!rng_ready) {
        rng_ready = wc_InitRng(&rng) == 0;
    }
    return rng_ready;
}

static size_t put_string(uint8_t *out, const void *text, size_t length)
{
    sys_put_be32((uint32_t)length, out);
    memcpy(out + 4, text, length);
    return 4u + length;
}

static bool generate_ed25519(test_key_t *key)
{
    ed25519_key wolf_key;
    word32 private_length = 32u;
    word32 public_length = 32u;
    bool done;

    if (wc_ed25519_init(&wolf_key) != 0) {
        return false;
    }
    done = wc_ed25519_make_key(&rng, ED25519_KEY_SIZE, &wolf_key) == 0 &&
           wc_ed25519_export_private_only(&wolf_key, key->host_key, &private_length) == 0 &&
           wc_ed25519_export_public(&wolf_key, key->host_key + 32, &public_length) == 0;
    wc_ed25519_free(&wolf_key);
    key->host_key_length = 64u;
    key->blob_length = put_string(key->blob, "ssh-ed25519", 11u);
    key->blob_length += put_string(key->blob + key->blob_length, key->host_key + 32, 32u);
    return done;
}

static bool generate_p256(test_key_t *key)
{
    ecc_key wolf_key;
    word32 private_length = 32u;
    word32 public_length = 65u;
    bool done;

    if (wc_ecc_init(&wolf_key) != 0) {
        return false;
    }
    done = wc_ecc_make_key_ex(&rng, 32, &wolf_key, ECC_SECP256R1) == 0 &&
           wc_ecc_export_private_only(&wolf_key, key->host_key, &private_length) == 0 &&
           private_length == 32u &&
           wc_ecc_export_x963(&wolf_key, key->host_key + 32, &public_length) == 0;
    wc_ecc_free(&wolf_key);
    key->host_key_length = 97u;
    key->blob_length = put_string(key->blob, "ecdsa-sha2-nistp256", 19u);
    key->blob_length += put_string(key->blob + key->blob_length, "nistp256", 8u);
    key->blob_length += put_string(key->blob + key->blob_length, key->host_key + 32, 65u);
    return done;
}

bool test_key_generate(crux_ssh_key_type_t type, test_key_t *key)
{
    memset(key, 0, sizeof(*key));
    key->type = type;
    if (!rng_get()) {
        return false;
    }
    return type == CRUX_SSH_KEY_ED25519 ? generate_ed25519(key) : generate_p256(key);
}

static bool sign_ed25519(const test_key_t *key, const uint8_t *data, size_t length, uint8_t *out,
                         word32 *out_length)
{
    ed25519_key wolf_key;
    bool done;

    if (wc_ed25519_init(&wolf_key) != 0) {
        return false;
    }
    done = wc_ed25519_import_private_key(key->host_key, 32u, key->host_key + 32, 32u, &wolf_key) ==
               0 &&
           wc_ed25519_sign_msg(data, (word32)length, out, out_length, &wolf_key) == 0;
    wc_ed25519_free(&wolf_key);
    return done;
}

static size_t put_mpint(uint8_t *out, const uint8_t *value, size_t length)
{
    size_t lead = (length > 0u && (value[0] & 0x80u) != 0u) ? 1u : 0u;

    sys_put_be32((uint32_t)(length + lead), out);
    out[4] = 0u;
    memcpy(out + 4 + lead, value, length);
    return 4u + lead + length;
}

static bool sign_p256(const test_key_t *key, const uint8_t *data, size_t length, uint8_t *out,
                      word32 *out_length, bool good_signature)
{
    ecc_key wolf_key;
    uint8_t digest[WC_SHA256_DIGEST_SIZE];
    uint8_t der[80];
    uint8_t r[33];
    uint8_t s[33];
    word32 der_length = sizeof(der);
    word32 r_length = sizeof(r);
    word32 s_length = sizeof(s);
    bool done;

    if (wc_ecc_init(&wolf_key) != 0) {
        return false;
    }
    done = wc_Sha256Hash(data, (word32)length, digest) == 0 &&
           wc_ecc_import_private_key_ex(key->host_key, 32u, key->host_key + 32, 65u, &wolf_key,
                                        ECC_SECP256R1) == 0 &&
           wc_ecc_set_rng(&wolf_key, &rng) == 0 &&
           wc_ecc_sign_hash(digest, sizeof(digest), der, &der_length, &rng, &wolf_key) == 0 &&
           wc_ecc_sig_to_rs(der, der_length, r, &r_length, s, &s_length) == 0;
    wc_ecc_free(&wolf_key);
    if (!done) {
        return false;
    }
    if (!good_signature) {
        r[r_length - 1u] ^= 0x01u;
    }
    *out_length = (word32)put_mpint(out, r, r_length);
    *out_length += (word32)put_mpint(out + *out_length, s, s_length);
    return true;
}

bool test_key_sign(const test_key_t *key, const char *algorithm, const uint8_t *data, size_t length,
                   bool good_signature, uint8_t *signature, size_t capacity, size_t *written)
{
    uint8_t body[96];
    word32 body_length = sizeof(body);
    size_t at;

    if (key->type == CRUX_SSH_KEY_ED25519) {
        if (!sign_ed25519(key, data, length, body, &body_length)) {
            return false;
        }
        if (!good_signature) {
            body[body_length - 1u] ^= 0x01u;
        }
    } else if (!sign_p256(key, data, length, body, &body_length, good_signature)) {
        return false;
    }
    if (capacity < 8u + strlen(algorithm) + body_length) {
        return false;
    }
    at = put_string(signature, algorithm, strlen(algorithm));
    at += put_string(signature + at, body, body_length);
    *written = at;
    return true;
}

void test_key_print_fingerprint(const test_key_t *key)
{
    char fingerprint[CRUX_SSH_WOLF_FINGERPRINT_SIZE];

    if (crux_ssh_wolf_host_key_fingerprint(key->type, key->host_key, key->host_key_length,
                                           fingerprint, sizeof(fingerprint)) == CRUX_OK) {
        printk("HOST-KEY %s\n", fingerprint);
    } else {
        printk("HOST-KEY unavailable\n");
    }
}
