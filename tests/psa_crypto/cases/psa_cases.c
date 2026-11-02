/*
 * Provider-neutral PSA Crypto conformance cases (Issue #142). See psa_cases.h.
 *
 * Only <psa/crypto.h> and ISO C are used. Every case releases the key and
 * operations it created, so a failing case does not starve the cases after it.
 */
#include "psa_cases.h"

#include <psa/crypto.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vectors.h"

/* An unassigned hash algorithm identifier: category HASH, hash id 0xfe. */
#define UNASSIGNED_HASH ((psa_algorithm_t)0x020000feU)

static char failure_text[200];
static const char *note_text;

const char *psa_case_note(void)
{
    return note_text;
}

static const char *fail_check(const char *file, int line, const char *expr)
{
    const char *base = strrchr(file, '/');

    (void)snprintf(failure_text, sizeof failure_text, "%s:%d: %s", base != NULL ? base + 1 : file,
                   line, expr);
    return failure_text;
}

static const char *fail_status(const char *file, int line, const char *expr, psa_status_t got,
                               psa_status_t want)
{
    const char *base = strrchr(file, '/');

    (void)snprintf(failure_text, sizeof failure_text, "%s:%d: %s returned %d, expected %d",
                   base != NULL ? base + 1 : file, line, expr, (int)got, (int)want);
    return failure_text;
}

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            return fail_check(__FILE__, __LINE__, #cond);                                          \
        }                                                                                          \
    } while (0)

#define CHECK_STATUS(want, expr)                                                                   \
    do {                                                                                           \
        psa_status_t status_ = (expr);                                                             \
        if (status_ != (want)) {                                                                   \
            return fail_status(__FILE__, __LINE__, #expr, status_, (want));                        \
        }                                                                                          \
    } while (0)

/* The specification lists both codes for this input; either is accepted, success is not. */
#define CHECK_REJECTED_AS(a, b, expr)                                                              \
    do {                                                                                           \
        psa_status_t status_ = (expr);                                                             \
        if (status_ != (a) && status_ != (b)) {                                                    \
            return fail_status(__FILE__, __LINE__, #expr, status_, (a));                           \
        }                                                                                          \
    } while (0)

#define INIT() CHECK_STATUS(PSA_SUCCESS, psa_crypto_init())

struct res {
    psa_key_id_t key;
    psa_hash_operation_t hash;
    psa_key_derivation_operation_t kdf;
};

static void res_init(struct res *r)
{
    r->key = PSA_KEY_ID_NULL;
    r->hash = psa_hash_operation_init();
    r->kdf = psa_key_derivation_operation_init();
}

static void res_release(struct res *r)
{
    (void)psa_hash_abort(&r->hash);
    (void)psa_key_derivation_abort(&r->kdf);
    if (r->key != PSA_KEY_ID_NULL) {
        (void)psa_destroy_key(r->key);
        r->key = PSA_KEY_ID_NULL;
    }
}

#define CASE(name)                                                                                 \
    static const char *name##_body(struct res *r);                                                 \
    const char *psa_case_##name(void)                                                              \
    {                                                                                              \
        struct res r;                                                                              \
        const char *failure;                                                                       \
        note_text = NULL;                                                                          \
        res_init(&r);                                                                              \
        failure = name##_body(&r);                                                                 \
        res_release(&r);                                                                           \
        return failure;                                                                            \
    }                                                                                              \
    static const char *name##_body(struct res *r)

static int all_bytes(const uint8_t *p, size_t n, uint8_t value)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] != value) {
            return 0;
        }
    }
    return 1;
}

static size_t distinct_values(const uint8_t *p, size_t n)
{
    uint8_t seen[256] = {0};
    size_t count = 0;

    for (size_t i = 0; i < n; i++) {
        if (seen[p[i]] == 0) {
            seen[p[i]] = 1;
            count++;
        }
    }
    return count;
}

static const char two_block_message[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

static const uint8_t pbkdf2_salt[] = {'s', 'a', 'l', 't'};

#define ECDSA_SHA256 PSA_ALG_ECDSA(PSA_ALG_SHA_256)

/* Volatile P-256 public key. Persistent keys are out of scope for this issue. */
static psa_status_t import_p256_public(struct res *r, psa_key_usage_t usage, psa_algorithm_t alg,
                                       const uint8_t *data, size_t length)
{
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, alg);
    status = psa_import_key(&attributes, data, length, &r->key);
    psa_reset_key_attributes(&attributes);
    return status;
}

#define VERIFY_USAGE PSA_KEY_USAGE_VERIFY_HASH

static psa_status_t pbkdf2_start(psa_key_derivation_operation_t *op, uint32_t cost,
                                 const char *password)
{
    psa_status_t status = psa_key_derivation_setup(op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));

    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_key_derivation_input_integer(op, PSA_KEY_DERIVATION_INPUT_COST, cost);
    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_key_derivation_input_bytes(op, PSA_KEY_DERIVATION_INPUT_SALT, pbkdf2_salt,
                                            sizeof pbkdf2_salt);
    if (status != PSA_SUCCESS) {
        return status;
    }
    return psa_key_derivation_input_bytes(op, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                          (const uint8_t *)password, strlen(password));
}

/* ---- initialisation and random ---- */

CASE(init_is_idempotent)
{
    (void)r;
    INIT();
    INIT();
    return NULL;
}

CASE(random_fills_requested_bytes_only)
{
    uint8_t buffer[48];
    uint8_t first[32];

    (void)r;
    INIT();
    memset(buffer, 0xa5, sizeof buffer);
    CHECK_STATUS(PSA_SUCCESS, psa_generate_random(buffer, 32));
    CHECK(all_bytes(buffer + 32, 16, 0xa5));
    CHECK(!all_bytes(buffer, 32, 0x00));
    CHECK(!all_bytes(buffer, 32, 0xa5));
    memcpy(first, buffer, sizeof first);
    CHECK_STATUS(PSA_SUCCESS, psa_generate_random(buffer, 32));
    CHECK(memcmp(first, buffer, sizeof first) != 0);
    return NULL;
}

CASE(random_large_request_succeeds)
{
    static uint8_t buffer[2500];

    (void)r;
    INIT();
    memset(buffer, 0, sizeof buffer);
    CHECK_STATUS(PSA_SUCCESS, psa_generate_random(buffer, sizeof buffer));
    /* 2500 uniform bytes leave fewer than 200 distinct values with negligible probability. */
    CHECK(distinct_values(buffer, sizeof buffer) >= 200);
    return NULL;
}

/* ---- hash ---- */

CASE(sha256_known_answer_one_shot)
{
    uint8_t hash[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];
    size_t length = 0;

    (void)r;
    INIT();
    CHECK_STATUS(PSA_SUCCESS, psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)"abc", 3, hash,
                                               sizeof hash, &length));
    CHECK(length == 32);
    CHECK(memcmp(hash, vec_sha256_abc, 32) == 0);
    return NULL;
}

CASE(sha256_multipart_matches_known_answer)
{
    const uint8_t *message = (const uint8_t *)two_block_message;
    uint8_t hash[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];
    size_t length = 0;

    INIT();
    CHECK(sizeof two_block_message - 1 == 56);
    CHECK_STATUS(PSA_SUCCESS, psa_hash_setup(&r->hash, PSA_ALG_SHA_256));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_update(&r->hash, message, 1));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_update(&r->hash, message + 1, 20));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_update(&r->hash, message + 21, 35));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_finish(&r->hash, hash, sizeof hash, &length));
    CHECK(length == 32);
    CHECK(memcmp(hash, vec_sha256_two_block, 32) == 0);
    return NULL;
}

CASE(sha256_update_after_finish_is_bad_state)
{
    uint8_t hash[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];
    size_t length = 0;

    INIT();
    CHECK_STATUS(PSA_SUCCESS, psa_hash_setup(&r->hash, PSA_ALG_SHA_256));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_update(&r->hash, (const uint8_t *)"abc", 3));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_finish(&r->hash, hash, sizeof hash, &length));
    CHECK_STATUS(PSA_ERROR_BAD_STATE, psa_hash_update(&r->hash, (const uint8_t *)"d", 1));
    CHECK_STATUS(PSA_ERROR_BAD_STATE, psa_hash_finish(&r->hash, hash, sizeof hash, &length));
    return NULL;
}

CASE(sha256_buffer_too_small)
{
    uint8_t hash[PSA_HASH_LENGTH(PSA_ALG_SHA_256) - 1];
    size_t length = 0;

    INIT();
    /* hash_length is documented only for success, so its value on failure is not checked. */
    CHECK_STATUS(
        PSA_ERROR_BUFFER_TOO_SMALL,
        psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)"abc", 3, hash, sizeof hash, &length));

    CHECK_STATUS(PSA_SUCCESS, psa_hash_setup(&r->hash, PSA_ALG_SHA_256));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_update(&r->hash, (const uint8_t *)"abc", 3));
    CHECK_STATUS(PSA_ERROR_BUFFER_TOO_SMALL, psa_hash_finish(&r->hash, hash, sizeof hash, &length));
    return NULL;
}

CASE(sha256_verify_mismatch_invalid_signature)
{
    uint8_t wrong[32];

    INIT();
    CHECK_STATUS(PSA_SUCCESS, psa_hash_setup(&r->hash, PSA_ALG_SHA_256));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_update(&r->hash, (const uint8_t *)"abc", 3));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_verify(&r->hash, vec_sha256_abc, sizeof vec_sha256_abc));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_abort(&r->hash));

    memcpy(wrong, vec_sha256_abc, sizeof wrong);
    wrong[31] ^= 0x01;
    CHECK_STATUS(PSA_SUCCESS, psa_hash_setup(&r->hash, PSA_ALG_SHA_256));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_update(&r->hash, (const uint8_t *)"abc", 3));
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE, psa_hash_verify(&r->hash, wrong, sizeof wrong));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_abort(&r->hash));

    /* A shorter expected value differs from the hash, which the header documents as
     * INVALID_SIGNATURE. */
    CHECK_STATUS(PSA_SUCCESS, psa_hash_setup(&r->hash, PSA_ALG_SHA_256));
    CHECK_STATUS(PSA_SUCCESS, psa_hash_update(&r->hash, (const uint8_t *)"abc", 3));
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_hash_verify(&r->hash, vec_sha256_abc, sizeof vec_sha256_abc - 1));
    return NULL;
}

CASE(hash_unsupported_algorithm_not_supported)
{
    uint8_t hash[PSA_HASH_MAX_SIZE];
    size_t length = 0;

    INIT();
    /* A well-formed hash algorithm identifier that no provider assigns. */
    CHECK_STATUS(PSA_ERROR_NOT_SUPPORTED, psa_hash_compute(UNASSIGNED_HASH, (const uint8_t *)"abc",
                                                           3, hash, sizeof hash, &length));
    CHECK_STATUS(PSA_ERROR_NOT_SUPPORTED, psa_hash_setup(&r->hash, UNASSIGNED_HASH));
    /* An HMAC algorithm is not a hash algorithm. The pinned header documents
     * NOT_SUPPORTED and INVALID_ARGUMENT for this input, so either is accepted. */
    CHECK_REJECTED_AS(PSA_ERROR_NOT_SUPPORTED, PSA_ERROR_INVALID_ARGUMENT,
                      psa_hash_compute(PSA_ALG_HMAC(PSA_ALG_SHA_256), (const uint8_t *)"abc", 3,
                                       hash, sizeof hash, &length));
    CHECK_REJECTED_AS(PSA_ERROR_NOT_SUPPORTED, PSA_ERROR_INVALID_ARGUMENT,
                      psa_hash_setup(&r->hash, PSA_ALG_HMAC(PSA_ALG_SHA_256)));
    return NULL;
}

/* ---- ECDSA P-256 verification ---- */

CASE(ecdsa_p256_import_is_volatile)
{
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_status_t status;
    int ok;

    INIT();
    CHECK_STATUS(PSA_SUCCESS,
                 import_p256_public(r, VERIFY_USAGE, ECDSA_SHA256, vec_p256_public_uncompressed,
                                    sizeof vec_p256_public_uncompressed));
    status = psa_get_key_attributes(r->key, &attributes);
    ok = status == PSA_SUCCESS && psa_get_key_lifetime(&attributes) == PSA_KEY_LIFETIME_VOLATILE &&
         psa_get_key_type(&attributes) == PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1) &&
         psa_get_key_bits(&attributes) == 256;
    psa_reset_key_attributes(&attributes);
    CHECK(ok);
    return NULL;
}

CASE(ecdsa_p256_verify_hash_known_answer)
{
    INIT();
    CHECK_STATUS(PSA_SUCCESS,
                 import_p256_public(r, VERIFY_USAGE, ECDSA_SHA256, vec_p256_public_uncompressed,
                                    sizeof vec_p256_public_uncompressed));
    CHECK_STATUS(PSA_SUCCESS,
                 psa_verify_hash(r->key, ECDSA_SHA256, vec_sha256_sample, sizeof vec_sha256_sample,
                                 vec_p256_signature_sample, sizeof vec_p256_signature_sample));
    return NULL;
}

CASE(ecdsa_p256_wrong_signature_invalid)
{
    uint8_t flipped[64];
    uint8_t zeros[64] = {0};

    INIT();
    CHECK_STATUS(PSA_SUCCESS,
                 import_p256_public(r, VERIFY_USAGE, ECDSA_SHA256, vec_p256_public_uncompressed,
                                    sizeof vec_p256_public_uncompressed));
    memcpy(flipped, vec_p256_signature_sample, sizeof flipped);
    flipped[63] ^= 0x01;
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_verify_hash(r->key, ECDSA_SHA256, vec_sha256_sample, sizeof vec_sha256_sample,
                                 flipped, sizeof flipped));
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_verify_hash(r->key, ECDSA_SHA256, vec_sha256_sample, sizeof vec_sha256_sample,
                                 zeros, sizeof zeros));
    /* A truncated signature is never accepted. */
    CHECK_REJECTED_AS(PSA_ERROR_INVALID_SIGNATURE, PSA_ERROR_INVALID_ARGUMENT,
                      psa_verify_hash(r->key, ECDSA_SHA256, vec_sha256_sample,
                                      sizeof vec_sha256_sample, vec_p256_signature_sample, 63));
    return NULL;
}

CASE(ecdsa_p256_tampered_input_invalid)
{
    static const uint8_t tampered_message[] = {'s', 'a', 'm', 'p', 'l', '3'};
    uint8_t tampered_hash[32];
    size_t length = 0;

    INIT();
    CHECK_STATUS(PSA_SUCCESS,
                 import_p256_public(r, VERIFY_USAGE, ECDSA_SHA256, vec_p256_public_uncompressed,
                                    sizeof vec_p256_public_uncompressed));
    /* The image-check flow: hash the (altered) data, then verify the signature over the hash. */
    CHECK_STATUS(PSA_SUCCESS,
                 psa_hash_compute(PSA_ALG_SHA_256, tampered_message, sizeof tampered_message,
                                  tampered_hash, sizeof tampered_hash, &length));
    CHECK(length == sizeof tampered_hash);
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_verify_hash(r->key, ECDSA_SHA256, tampered_hash, sizeof tampered_hash,
                                 vec_p256_signature_sample, sizeof vec_p256_signature_sample));
    memcpy(tampered_hash, vec_sha256_sample, sizeof tampered_hash);
    tampered_hash[0] ^= 0x80;
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_verify_hash(r->key, ECDSA_SHA256, tampered_hash, sizeof tampered_hash,
                                 vec_p256_signature_sample, sizeof vec_p256_signature_sample));
    return NULL;
}

CASE(ecdsa_p256_invalid_public_key_rejected)
{
    uint8_t off_curve[sizeof vec_p256_public_uncompressed];

    INIT();
    memcpy(off_curve, vec_p256_public_uncompressed, sizeof off_curve);
    off_curve[sizeof off_curve - 1] ^= 0x01;
    CHECK_STATUS(PSA_ERROR_INVALID_ARGUMENT,
                 import_p256_public(r, VERIFY_USAGE, ECDSA_SHA256, off_curve, sizeof off_curve));
    CHECK(r->key == PSA_KEY_ID_NULL);
    /* The format prefix is missing: 64 bytes instead of the 65-byte uncompressed point. */
    CHECK_STATUS(PSA_ERROR_INVALID_ARGUMENT,
                 import_p256_public(r, VERIFY_USAGE, ECDSA_SHA256, vec_p256_public_uncompressed + 1,
                                    sizeof vec_p256_public_uncompressed - 1));
    CHECK(r->key == PSA_KEY_ID_NULL);
    return NULL;
}

CASE(ecdsa_p256_key_policy_not_permitted)
{
    INIT();
    /* A key without any verify usage flag. */
    CHECK_STATUS(PSA_SUCCESS, import_p256_public(r, 0, ECDSA_SHA256, vec_p256_public_uncompressed,
                                                 sizeof vec_p256_public_uncompressed));
    CHECK_STATUS(PSA_ERROR_NOT_PERMITTED,
                 psa_verify_hash(r->key, ECDSA_SHA256, vec_sha256_sample, sizeof vec_sha256_sample,
                                 vec_p256_signature_sample, sizeof vec_p256_signature_sample));
    CHECK_STATUS(PSA_SUCCESS, psa_destroy_key(r->key));
    r->key = PSA_KEY_ID_NULL;

    /* A key bound to ECDSA(SHA-256) does not permit the wildcard ECDSA_ANY. */
    CHECK_STATUS(PSA_SUCCESS,
                 import_p256_public(r, VERIFY_USAGE, ECDSA_SHA256, vec_p256_public_uncompressed,
                                    sizeof vec_p256_public_uncompressed));
    CHECK_STATUS(PSA_ERROR_NOT_PERMITTED,
                 psa_verify_hash(r->key, PSA_ALG_ECDSA_ANY, vec_sha256_sample,
                                 sizeof vec_sha256_sample, vec_p256_signature_sample,
                                 sizeof vec_p256_signature_sample));
    return NULL;
}

/* ---- PBKDF2-HMAC-SHA-256 ---- */

CASE(pbkdf2_hmac_sha256_known_answer_rfc7914)
{
    uint8_t out[64];

    INIT();
    CHECK_STATUS(PSA_SUCCESS, pbkdf2_start(&r->kdf, 1, "passwd"));
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_output_bytes(&r->kdf, out, sizeof out));
    CHECK(memcmp(out, vec_pbkdf2_rfc7914_64, sizeof out) == 0);
    return NULL;
}

CASE(pbkdf2_hmac_sha256_known_answer_4096)
{
    uint8_t out[32];

    INIT();
    CHECK_STATUS(PSA_SUCCESS, pbkdf2_start(&r->kdf, 4096, "password"));
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_output_bytes(&r->kdf, out, sizeof out));
    CHECK(memcmp(out, vec_pbkdf2_4096_32, sizeof out) == 0);
    return NULL;
}

CASE(pbkdf2_wrong_password_derives_different_output)
{
    uint8_t right[32];
    uint8_t wrong[32];

    INIT();
    /* The right password must reproduce the known answer, so a provider that returns
     * fixed output cannot satisfy the comparison below. */
    CHECK_STATUS(PSA_SUCCESS, pbkdf2_start(&r->kdf, 4096, "password"));
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_output_bytes(&r->kdf, right, sizeof right));
    CHECK(memcmp(right, vec_pbkdf2_4096_32, sizeof right) == 0);
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_abort(&r->kdf));

    CHECK_STATUS(PSA_SUCCESS, pbkdf2_start(&r->kdf, 4096, "Password"));
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_output_bytes(&r->kdf, wrong, sizeof wrong));
    CHECK(memcmp(wrong, right, sizeof wrong) != 0);
    return NULL;
}

/* psa_key_derivation_verify_bytes() may be reported as not supported; when it is
 * supported it must accept the right password and reject a wrong one. */
CASE(pbkdf2_verify_bytes_when_supported)
{
    psa_status_t status;

    INIT();
    CHECK_STATUS(PSA_SUCCESS, pbkdf2_start(&r->kdf, 4096, "password"));
    status =
        psa_key_derivation_verify_bytes(&r->kdf, vec_pbkdf2_4096_32, sizeof vec_pbkdf2_4096_32);
    if (status == PSA_ERROR_NOT_SUPPORTED) {
        note_text = "psa_key_derivation_verify_bytes is not supported by this provider";
        return NULL;
    }
    CHECK(status == PSA_SUCCESS);
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_abort(&r->kdf));

    CHECK_STATUS(PSA_SUCCESS, pbkdf2_start(&r->kdf, 4096, "Password"));
    CHECK_STATUS(
        PSA_ERROR_INVALID_SIGNATURE,
        psa_key_derivation_verify_bytes(&r->kdf, vec_pbkdf2_4096_32, sizeof vec_pbkdf2_4096_32));
    return NULL;
}

CASE(kdf_output_on_inactive_operation_is_bad_state)
{
    uint8_t out[16];

    INIT();
    /* An operation that was never set up. */
    CHECK_STATUS(PSA_ERROR_BAD_STATE, psa_key_derivation_output_bytes(&r->kdf, out, sizeof out));

    /* An operation that was aborted after a successful derivation. */
    CHECK_STATUS(PSA_SUCCESS, pbkdf2_start(&r->kdf, 1, "passwd"));
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_output_bytes(&r->kdf, out, sizeof out));
    CHECK(memcmp(out, vec_pbkdf2_rfc7914_64, sizeof out) == 0);
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_abort(&r->kdf));
    CHECK_STATUS(PSA_ERROR_BAD_STATE, psa_key_derivation_output_bytes(&r->kdf, out, sizeof out));
    return NULL;
}

CASE(kdf_unsupported_algorithm_not_supported)
{
    (void)r;
    INIT();
    CHECK_STATUS(PSA_ERROR_NOT_SUPPORTED,
                 psa_key_derivation_setup(&r->kdf, PSA_ALG_PBKDF2_HMAC(UNASSIGNED_HASH)));
    return NULL;
}

/* ---- key identifiers ---- */

CASE(destroyed_key_is_invalid_handle)
{
    psa_key_id_t stale;

    INIT();
    CHECK_STATUS(PSA_SUCCESS,
                 import_p256_public(r, VERIFY_USAGE, ECDSA_SHA256, vec_p256_public_uncompressed,
                                    sizeof vec_p256_public_uncompressed));
    stale = r->key;
    CHECK_STATUS(PSA_SUCCESS, psa_destroy_key(stale));
    r->key = PSA_KEY_ID_NULL;
    CHECK_STATUS(PSA_ERROR_INVALID_HANDLE,
                 psa_verify_hash(stale, ECDSA_SHA256, vec_sha256_sample, sizeof vec_sha256_sample,
                                 vec_p256_signature_sample, sizeof vec_p256_signature_sample));
    CHECK_STATUS(PSA_ERROR_INVALID_HANDLE, psa_destroy_key(stale));
    return NULL;
}
