#include "psa_port_cases.h"

#include "access_port.h"
#include "credential_port.h"
#include "credential_psa.h"
#include "vectors.h"

/* The known answers of the qualification, which come from independent code. */
#include "../psa_crypto/cases/vectors.h"

#include <psa/crypto.h>

#include <stdio.h>
#include <string.h>

#ifndef PSA_PORTS_HAS_ED25519
#error "The composition states whether its provider generates Ed25519 keys"
#endif

static unsigned failures;
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL %s line %d: %s\n", __func__, __LINE__, #expression);                      \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static bool all_zero(const uint8_t *bytes, size_t length)
{
    uint8_t any = 0;
    for (size_t index = 0; index < length; ++index) {
        any |= bytes[index];
    }
    return any == 0u;
}

static const uint8_t other_device_id[] = {0x12, 0x34, 0x56, 0x79};

static void use_vector_device(void)
{
    psa_port_test_device_id(vec_seal_device_id, sizeof(vec_seal_device_id));
}

static void access_derive(void)
{
    uint8_t derived[CRUX_ACCESS_DERIVED_SIZE];
    CHECK(crux_access_port_derive(CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256, 10000u,
                                  vec_pbkdf2_salt16, (const char *)vec_pbkdf2_long_password,
                                  sizeof(vec_pbkdf2_long_password), derived));
    CHECK(memcmp(derived, vec_pbkdf2_long_10000, sizeof(derived)) == 0);
    /* An empty password, with no bytes behind the pointer. */
    CHECK(crux_access_port_derive(CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256, 1000u, vec_pbkdf2_salt16,
                                  NULL, 0u, derived));
    CHECK(memcmp(derived, vec_pbkdf2_empty_1000, sizeof(derived)) == 0);
    /* A stored verifier of another kind runs nothing. */
    memset(derived, 0xA5, sizeof(derived));
    CHECK(!crux_access_port_derive(CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256 + 1u, 1000u,
                                   vec_pbkdf2_salt16, "password", 8u, derived));
    /* A zero cost is a failure of the provider, and leaves nothing that could match. */
    memset(derived, 0xA5, sizeof(derived));
    CHECK(!crux_access_port_derive(CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256, 0u, vec_pbkdf2_salt16,
                                   "password", 8u, derived));
    CHECK(all_zero(derived, sizeof(derived)));
}

static void access_random(void)
{
    uint8_t first[32] = {0};
    uint8_t second[32] = {0};
    CHECK(crux_access_port_random(first, sizeof(first)));
    CHECK(crux_access_port_random(second, sizeof(second)));
    CHECK(memcmp(first, second, sizeof(first)) != 0);
    /* Writes only what was asked. */
    uint8_t guarded[8];
    memset(guarded, 0xA5, sizeof(guarded));
    CHECK(crux_access_port_random(guarded, 4u));
    CHECK(guarded[4] == 0xA5 && guarded[7] == 0xA5);
}

static void access_hmac(void)
{
    uint8_t hash[CRUX_ACCESS_HASH_SIZE];
    CHECK(crux_access_port_hmac(vec_hmac_key, (const uint8_t *)vec_hmac_data,
                                sizeof(vec_hmac_data) - 1u, hash));
    CHECK(memcmp(hash, vec_hmac_sha256, sizeof(hash)) == 0);
    /* An empty login name reaches this. */
    CHECK(crux_access_port_hmac(vec_hmac_key, NULL, 0u, hash));
    CHECK(memcmp(hash, vec_hmac_sha256_empty, sizeof(hash)) == 0);
}

static void store_declaration(void)
{
    use_vector_device();
    const crux_credential_store_info_t *declared = crux_credential_port_declaration();
    CHECK(declared != NULL);
    if (declared != NULL) {
        CHECK(declared->basis == CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY);
        CHECK(declared->evidence == CRUX_CREDENTIAL_EVIDENCE_SOFTWARE);
        CHECK(!declared->confidential_against_medium_reader);
        CHECK(declared->forgery_detected_without_device_id);
        CHECK(!declared->rollback_detected);
    }
    /* No device ID means that the provider cannot run, and it never falls back to a constant. */
    psa_port_test_device_id(NULL, 0u);
    CHECK(crux_credential_port_declaration() == NULL);
    use_vector_device();
}

static void store_seal_known_answer(void)
{
    uint8_t sealed[sizeof(vec_seal_plain)];
    uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES];
    uint8_t plain[sizeof(vec_seal_plain)];
    use_vector_device();
    CHECK(crux_credential_port_seal(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_plain,
                                    sizeof(vec_seal_plain), sealed, vec_seal_nonce,
                                    tag) == CRUX_CREDENTIAL_OK);
    CHECK(memcmp(sealed, vec_seal_cipher, sizeof(sealed)) == 0);
    CHECK(memcmp(tag, vec_seal_tag, sizeof(tag)) == 0);
    CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_cipher,
                                    sizeof(vec_seal_cipher), vec_seal_nonce, vec_seal_tag,
                                    plain) == CRUX_CREDENTIAL_OK);
    CHECK(memcmp(plain, vec_seal_plain, sizeof(plain)) == 0);
}

/* Each is a tag that does not verify, so the slot is corrupt, and no plain text stays behind. */
static void store_open_refuses_tampering(void)
{
    uint8_t plain[sizeof(vec_seal_plain)];
    uint8_t changed_tag[sizeof(vec_seal_tag)];
    uint8_t changed_cipher[sizeof(vec_seal_cipher)];
    uint8_t changed_aad[sizeof(vec_seal_aad)];
    uint8_t changed_nonce[sizeof(vec_seal_nonce)];
    use_vector_device();

    memcpy(changed_tag, vec_seal_tag, sizeof(changed_tag));
    changed_tag[15] ^= 0x01u;
    memset(plain, 0xA5, sizeof(plain));
    CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_cipher,
                                    sizeof(vec_seal_cipher), vec_seal_nonce, changed_tag,
                                    plain) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(all_zero(plain, sizeof(plain)));

    memcpy(changed_cipher, vec_seal_cipher, sizeof(changed_cipher));
    changed_cipher[0] ^= 0x80u;
    memset(plain, 0xA5, sizeof(plain));
    CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), changed_cipher,
                                    sizeof(changed_cipher), vec_seal_nonce, vec_seal_tag,
                                    plain) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(all_zero(plain, sizeof(plain)));

    memcpy(changed_aad, vec_seal_aad, sizeof(changed_aad));
    changed_aad[8] ^= 0x01u;
    memset(plain, 0xA5, sizeof(plain));
    CHECK(crux_credential_port_open(changed_aad, sizeof(changed_aad), vec_seal_cipher,
                                    sizeof(vec_seal_cipher), vec_seal_nonce, vec_seal_tag,
                                    plain) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(all_zero(plain, sizeof(plain)));

    memcpy(changed_nonce, vec_seal_nonce, sizeof(changed_nonce));
    changed_nonce[11] ^= 0x01u;
    memset(plain, 0xA5, sizeof(plain));
    CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_cipher,
                                    sizeof(vec_seal_cipher), changed_nonce, vec_seal_tag,
                                    plain) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(all_zero(plain, sizeof(plain)));
}

/* Another board's key looks like tampering. A provider that could not run proves nothing about the
 * slot, so it is unreadable and not corrupt: decision 11 of the store. */
static void store_other_device_and_no_device(void)
{
    uint8_t plain[sizeof(vec_seal_plain)];
    uint8_t sealed[sizeof(vec_seal_plain)];
    uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES];
    psa_port_test_device_id(other_device_id, sizeof(other_device_id));
    memset(plain, 0xA5, sizeof(plain));
    CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_cipher,
                                    sizeof(vec_seal_cipher), vec_seal_nonce, vec_seal_tag,
                                    plain) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(all_zero(plain, sizeof(plain)));

    psa_port_test_device_id(NULL, 0u);
    memset(plain, 0xA5, sizeof(plain));
    CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_cipher,
                                    sizeof(vec_seal_cipher), vec_seal_nonce, vec_seal_tag,
                                    plain) == CRUX_CREDENTIAL_ERR_READ);
    CHECK(all_zero(plain, sizeof(plain)));
    crux_credential_result_t refused =
        crux_credential_port_seal(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_plain,
                                  sizeof(vec_seal_plain), sealed, vec_seal_nonce, tag);
    CHECK(refused != CRUX_CREDENTIAL_OK && refused != CRUX_CREDENTIAL_ERR_UNUSABLE);
    use_vector_device();
}

/* The largest payload of a default slot, with a header as additional data, in separate buffers. */
static void store_whole_slot_round_trip(void)
{
    enum {
        PAYLOAD =
            CRUX_CREDENTIAL_SLOT_BYTES - CRUX_CREDENTIAL_HEADER_BYTES - CRUX_CREDENTIAL_TAG_BYTES
    };
    static uint8_t plain[PAYLOAD];
    static uint8_t sealed[PAYLOAD];
    static uint8_t opened[PAYLOAD];
    uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES];
    for (size_t index = 0; index < PAYLOAD; ++index) {
        plain[index] = (uint8_t)(index * 31u + 7u);
    }
    use_vector_device();
    CHECK(crux_credential_port_seal(vec_seal_aad, sizeof(vec_seal_aad), plain, PAYLOAD, sealed,
                                    vec_seal_nonce, tag) == CRUX_CREDENTIAL_OK);
    CHECK(memcmp(sealed, plain, 64u) != 0);
    CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), sealed, PAYLOAD,
                                    vec_seal_nonce, tag, opened) == CRUX_CREDENTIAL_OK);
    CHECK(memcmp(opened, plain, PAYLOAD) == 0);
    sealed[PAYLOAD - 1u] ^= 0x01u;
    CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), sealed, PAYLOAD,
                                    vec_seal_nonce, tag, opened) == CRUX_CREDENTIAL_ERR_UNUSABLE);
    CHECK(all_zero(opened, PAYLOAD));
}

/* The exported scalar must give the exported point, or the record would hold two unrelated keys. */
static void generated_pair_matches(psa_key_type_t type, size_t bits, psa_key_usage_t usage,
                                   psa_algorithm_t algorithm, const uint8_t *public_key,
                                   size_t public_length, const uint8_t *private_key,
                                   size_t private_length)
{
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_key_id_t key = PSA_KEY_ID_NULL;
    uint8_t again[65];
    size_t again_length = 0;
    psa_set_key_type(&attributes, type);
    psa_set_key_bits(&attributes, bits);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, algorithm);
    CHECK(psa_import_key(&attributes, private_key, private_length, &key) == PSA_SUCCESS);
    psa_reset_key_attributes(&attributes);
    CHECK(psa_export_public_key(key, again, sizeof(again), &again_length) == PSA_SUCCESS);
    CHECK(again_length == public_length);
    CHECK(memcmp(again, public_key, public_length) == 0);
    CHECK(psa_destroy_key(key) == PSA_SUCCESS);
}

static void store_generate_p256(void)
{
    uint8_t public_first[80];
    uint8_t private_first[48];
    uint8_t public_second[80];
    uint8_t private_second[48];
    size_t lengths[2] = {0, 0};
    CHECK(crux_credential_port_generate_key(CRUX_CREDENTIAL_KEY_ECDSA_P256, public_first,
                                            sizeof(public_first), private_first,
                                            sizeof(private_first), lengths) == CRUX_CREDENTIAL_OK);
    CHECK(lengths[0] == 65u);
    CHECK(lengths[1] == 32u);
    CHECK(public_first[0] == 0x04u);
    generated_pair_matches(PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1), 256u,
                           PSA_KEY_USAGE_EXPORT, PSA_ALG_ECDSA(PSA_ALG_SHA_256), public_first,
                           lengths[0], private_first, lengths[1]);
    CHECK(crux_credential_port_generate_key(CRUX_CREDENTIAL_KEY_ECDSA_P256, public_second,
                                            sizeof(public_second), private_second,
                                            sizeof(private_second), lengths) == CRUX_CREDENTIAL_OK);
    CHECK(memcmp(private_first, private_second, 32u) != 0);

    /* Any failure is ERR_ENTROPY, as the port states, and leaves no part of a key. */
    uint8_t small[16];
    memset(public_first, 0xA5, sizeof(public_first));
    memset(small, 0xA5, sizeof(small));
    CHECK(crux_credential_port_generate_key(CRUX_CREDENTIAL_KEY_ECDSA_P256, public_first,
                                            sizeof(public_first), small, sizeof(small),
                                            lengths) == CRUX_CREDENTIAL_ERR_ENTROPY);
    CHECK(all_zero(small, sizeof(small)));
    CHECK(all_zero(public_first, sizeof(public_first)));
    CHECK(crux_credential_port_generate_key(
              (crux_credential_key_type_t)99, public_first, sizeof(public_first), private_first,
              sizeof(private_first), lengths) == CRUX_CREDENTIAL_ERR_ENTROPY);
}

static void store_generate_ed25519(void)
{
    uint8_t public_key[48];
    uint8_t private_key[48];
    size_t lengths[2] = {0, 0};
    memset(private_key, 0xA5, sizeof(private_key));
    crux_credential_result_t result = crux_credential_port_generate_key(
        CRUX_CREDENTIAL_KEY_ED25519, public_key, sizeof(public_key), private_key,
        sizeof(private_key), lengths);
#if PSA_PORTS_HAS_ED25519
    CHECK(result == CRUX_CREDENTIAL_OK);
    CHECK(lengths[0] == 32u);
    CHECK(lengths[1] == 32u);
    generated_pair_matches(PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_TWISTED_EDWARDS), 255u,
                           PSA_KEY_USAGE_EXPORT, PSA_ALG_PURE_EDDSA, public_key, lengths[0],
                           private_key, lengths[1]);
#else
    /* TF-PSA-Crypto has none, and the port can only say so as an entropy error. */
    CHECK(result == CRUX_CREDENTIAL_ERR_ENTROPY);
    CHECK(all_zero(private_key, sizeof(private_key)));
#endif
}

static void store_random(void)
{
    uint8_t first[CRUX_CREDENTIAL_NONCE_BYTES] = {0};
    uint8_t second[CRUX_CREDENTIAL_NONCE_BYTES] = {0};
    CHECK(crux_credential_port_random(first, sizeof(first)) == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_port_random(second, sizeof(second)) == CRUX_CREDENTIAL_OK);
    CHECK(memcmp(first, second, sizeof(first)) != 0);
}

/* TF-PSA-Crypto has 16 static key slots on native_sim, so any port that leaves one behind runs out
 * long before this ends. On Host its slots grow, and wolfPSA keeps a list, so there this case
 * cannot see a leak and shows only that the ports keep working. */
static void ports_leave_no_key_slot(void)
{
    uint8_t hash[CRUX_ACCESS_HASH_SIZE];
    uint8_t plain[sizeof(vec_seal_plain)];
    uint8_t sealed[sizeof(vec_seal_plain)];
    uint8_t tag[CRUX_CREDENTIAL_TAG_BYTES];
    uint8_t public_key[80];
    uint8_t private_key[48];
    size_t lengths[2];
    use_vector_device();
    for (unsigned round = 0; round < 40u; ++round) {
        CHECK(crux_access_port_hmac(vec_hmac_key, NULL, 0u, hash));
        CHECK(crux_credential_port_seal(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_plain,
                                        sizeof(vec_seal_plain), sealed, vec_seal_nonce,
                                        tag) == CRUX_CREDENTIAL_OK);
        CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_cipher,
                                        sizeof(vec_seal_cipher), vec_seal_nonce, vec_seal_tag,
                                        plain) == CRUX_CREDENTIAL_OK);
        /* A refused open must let go of its key too. */
        CHECK(crux_credential_port_open(vec_seal_aad, sizeof(vec_seal_aad), vec_seal_cipher,
                                        sizeof(vec_seal_cipher), vec_seal_nonce, vec_seal_cipher,
                                        plain) == CRUX_CREDENTIAL_ERR_UNUSABLE);
        CHECK(crux_credential_port_declaration() != NULL);
        CHECK(crux_credential_port_generate_key(
                  CRUX_CREDENTIAL_KEY_ECDSA_P256, public_key, sizeof(public_key), private_key,
                  sizeof(private_key), lengths) == CRUX_CREDENTIAL_OK);
    }
}

unsigned psa_port_cases_run(void)
{
    failures = 0;
    access_derive();
    access_random();
    access_hmac();
    store_declaration();
    store_seal_known_answer();
    store_open_refuses_tampering();
    store_other_device_and_no_device();
    store_whole_slot_round_trip();
    store_generate_p256();
    store_generate_ed25519();
    store_random();
    ports_leave_no_key_slot();
    printf("psa port cases: %u failed\n", failures);
    return failures;
}
