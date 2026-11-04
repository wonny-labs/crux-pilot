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

/* What a composition declares that its provider does not support. Empty by default. */
#ifndef PSA_CASES_DECLARED_UNSUPPORTED
#define PSA_CASES_DECLARED_UNSUPPORTED ""
#endif
#ifndef PSA_CASES_DECLARED_DEVIATIONS
#define PSA_CASES_DECLARED_DEVIATIONS ""
#endif

static char failure_text[200];
static const char *note_text;
static const char not_supported_marker[] = "not supported by this provider";

const char *psa_case_note(void)
{
    return note_text;
}

static int listed(const char *list, const char *name)
{
    const size_t length = strlen(name);

    while (*list != '\0') {
        const char *end = strchr(list, ',');
        const size_t entry = end != NULL ? (size_t)(end - list) : strlen(list);

        if (entry == length && strncmp(list, name, length) == 0) {
            return 1;
        }
        list += entry;
        if (*list == ',') {
            list++;
        }
    }
    return 0;
}

psa_outcome_t psa_case_outcome(const char *name, const char *result, const char **text)
{
    const int unsupported = listed(PSA_CASES_DECLARED_UNSUPPORTED, name);
    const int deviation = listed(PSA_CASES_DECLARED_DEVIATIONS, name);

    *text = NULL;
    if (result == NULL) {
        if (unsupported || deviation) {
            *text = "passes, but this composition declares otherwise: correct the declaration";
            return PSA_OUTCOME_FAIL;
        }
        return PSA_OUTCOME_PASS;
    }
    if (result == not_supported_marker) {
        if (unsupported) {
            return PSA_OUTCOME_DECLARED_UNSUPPORTED;
        }
        *text = "PSA_ERROR_NOT_SUPPORTED, and this composition does not declare that";
        return PSA_OUTCOME_FAIL;
    }
    *text = result;
    return deviation ? PSA_OUTCOME_DECLARED_DEVIATION : PSA_OUTCOME_FAIL;
}

void psa_case_print_random_probe(void)
{
    uint8_t bytes[16];
    psa_status_t status = psa_crypto_init();

    if (status == PSA_SUCCESS) {
        status = psa_generate_random(bytes, sizeof bytes);
    }
    if (status != PSA_SUCCESS) {
        printf("PSA_RANDOM_PROBE status %d\n", (int)status);
        return;
    }
    printf("PSA_RANDOM_PROBE ");
    for (size_t i = 0; i < sizeof bytes; i++) {
        printf("%02x", (unsigned)bytes[i]);
    }
    printf("\n");
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

/* The first use of what a consumer needs: a provider without it reports a gap, not a failure. */
#define CHECK_SUPPORTED(expr)                                                                      \
    do {                                                                                           \
        psa_status_t status_ = (expr);                                                             \
        if (status_ == PSA_ERROR_NOT_SUPPORTED) {                                                  \
            return not_supported_marker;                                                           \
        }                                                                                          \
        if (status_ != PSA_SUCCESS) {                                                              \
            return fail_status(__FILE__, __LINE__, #expr, status_, PSA_SUCCESS);                   \
        }                                                                                          \
    } while (0)

#define INIT() CHECK_STATUS(PSA_SUCCESS, psa_crypto_init())

struct res {
    psa_key_id_t key;
    psa_key_id_t key2;
    psa_hash_operation_t hash;
    psa_key_derivation_operation_t kdf;
    psa_aead_operation_t aead;
};

static void res_init(struct res *r)
{
    r->key = PSA_KEY_ID_NULL;
    r->key2 = PSA_KEY_ID_NULL;
    r->hash = psa_hash_operation_init();
    r->kdf = psa_key_derivation_operation_init();
    r->aead = psa_aead_operation_init();
}

static void res_release(struct res *r)
{
    (void)psa_hash_abort(&r->hash);
    (void)psa_key_derivation_abort(&r->kdf);
    (void)psa_aead_abort(&r->aead);
    if (r->key != PSA_KEY_ID_NULL) {
        (void)psa_destroy_key(r->key);
        r->key = PSA_KEY_ID_NULL;
    }
    if (r->key2 != PSA_KEY_ID_NULL) {
        (void)psa_destroy_key(r->key2);
        r->key2 = PSA_KEY_ID_NULL;
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
    /* The header does not promise an operation is unchanged after a failed setup. */
    CHECK_STATUS(PSA_SUCCESS, psa_hash_abort(&r->hash));
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

/* Whether or not a provider refuses the point at import, it must never verify with it. */
CASE(ecdsa_p256_off_curve_key_never_verifies)
{
    uint8_t off_curve[sizeof vec_p256_public_uncompressed];
    psa_status_t status;

    INIT();
    memcpy(off_curve, vec_p256_public_uncompressed, sizeof off_curve);
    off_curve[sizeof off_curve - 1] ^= 0x01;
    status = import_p256_public(r, VERIFY_USAGE, ECDSA_SHA256, off_curve, sizeof off_curve);
    if (status == PSA_ERROR_INVALID_ARGUMENT) {
        return NULL;
    }
    CHECK_STATUS(PSA_SUCCESS, status);
    status = psa_verify_hash(r->key, ECDSA_SHA256, vec_sha256_sample, sizeof vec_sha256_sample,
                             vec_p256_signature_sample, sizeof vec_p256_signature_sample);
    CHECK(status != PSA_SUCCESS);
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

/* ---- keyed hash, for crux_access_port_hmac ---- */

static psa_status_t import_key(psa_key_id_t *key, psa_key_type_t type, size_t bits,
                               psa_key_usage_t usage, psa_algorithm_t alg, const uint8_t *data,
                               size_t length)
{
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_status_t status;

    psa_set_key_type(&attributes, type);
    psa_set_key_bits(&attributes, bits);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, alg);
    status = psa_import_key(&attributes, data, length, key);
    psa_reset_key_attributes(&attributes);
    return status;
}

static psa_status_t generate_key(psa_key_id_t *key, psa_key_type_t type, size_t bits,
                                 psa_key_usage_t usage, psa_algorithm_t alg)
{
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_status_t status;

    psa_set_key_type(&attributes, type);
    psa_set_key_bits(&attributes, bits);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, alg);
    status = psa_generate_key(&attributes, key);
    psa_reset_key_attributes(&attributes);
    return status;
}

#define HMAC_SHA256 PSA_ALG_HMAC(PSA_ALG_SHA_256)

CASE(hmac_sha256_known_answer)
{
    uint8_t mac[32];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(import_key(&r->key, PSA_KEY_TYPE_HMAC, 256, PSA_KEY_USAGE_SIGN_MESSAGE,
                               HMAC_SHA256, vec_hmac_key, sizeof vec_hmac_key));
    CHECK_SUPPORTED(psa_mac_compute(r->key, HMAC_SHA256, (const uint8_t *)vec_hmac_data,
                                    sizeof vec_hmac_data - 1, mac, sizeof mac, &length));
    CHECK(length == sizeof mac);
    CHECK(memcmp(mac, vec_hmac_sha256, sizeof mac) == 0);
    return NULL;
}

CASE(hmac_sha256_buffer_too_small)
{
    uint8_t mac[31];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(import_key(&r->key, PSA_KEY_TYPE_HMAC, 256, PSA_KEY_USAGE_SIGN_MESSAGE,
                               HMAC_SHA256, vec_hmac_key, sizeof vec_hmac_key));
    CHECK_STATUS(PSA_ERROR_BUFFER_TOO_SMALL,
                 psa_mac_compute(r->key, HMAC_SHA256, (const uint8_t *)vec_hmac_data,
                                 sizeof vec_hmac_data - 1, mac, sizeof mac, &length));
    return NULL;
}

CASE(hmac_key_policy_not_permitted)
{
    uint8_t mac[32];
    size_t length = 0;

    INIT();
    /* A key without a usage flag for computing a MAC. */
    CHECK_SUPPORTED(import_key(&r->key, PSA_KEY_TYPE_HMAC, 256, 0, HMAC_SHA256, vec_hmac_key,
                               sizeof vec_hmac_key));
    CHECK_STATUS(PSA_ERROR_NOT_PERMITTED,
                 psa_mac_compute(r->key, HMAC_SHA256, (const uint8_t *)vec_hmac_data,
                                 sizeof vec_hmac_data - 1, mac, sizeof mac, &length));
    return NULL;
}

/* ---- PBKDF2 at the limits of crux_access_port_derive ---- */

static psa_status_t pbkdf2_bytes(psa_key_derivation_operation_t *op, uint32_t cost,
                                 const uint8_t *password, size_t length)
{
    psa_status_t status = psa_key_derivation_setup(op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));

    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_key_derivation_input_integer(op, PSA_KEY_DERIVATION_INPUT_COST, cost);
    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_key_derivation_input_bytes(op, PSA_KEY_DERIVATION_INPUT_SALT, vec_pbkdf2_salt16,
                                            sizeof vec_pbkdf2_salt16);
    if (status != PSA_SUCCESS) {
        return status;
    }
    return psa_key_derivation_input_bytes(op, PSA_KEY_DERIVATION_INPUT_PASSWORD, password, length);
}

CASE(pbkdf2_longest_password_known_answer)
{
    uint8_t out[32];

    INIT();
    CHECK_SUPPORTED(
        pbkdf2_bytes(&r->kdf, 10000, vec_pbkdf2_long_password, sizeof vec_pbkdf2_long_password));
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_output_bytes(&r->kdf, out, sizeof out));
    CHECK(memcmp(out, vec_pbkdf2_long_10000, sizeof out) == 0);
    return NULL;
}

/* crux_access derives before its product rule can refuse a password, so length 0 reaches here. */
CASE(pbkdf2_empty_password_known_answer)
{
    static const uint8_t empty[1] = {0};
    uint8_t out[32];

    INIT();
    CHECK_SUPPORTED(pbkdf2_bytes(&r->kdf, 1000, empty, 0));
    CHECK_STATUS(PSA_SUCCESS, psa_key_derivation_output_bytes(&r->kdf, out, sizeof out));
    CHECK(memcmp(out, vec_pbkdf2_empty_1000, sizeof out) == 0);
    return NULL;
}

/* ---- AES-256-GCM, for crux_credential_port_seal and _open ---- */

#define GCM_NONCE_BYTES 12u
#define GCM_TAG_BYTES 16u
#define AES_GCM_USAGE (PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT)

static psa_status_t import_aes256(psa_key_id_t *key, psa_key_usage_t usage, const uint8_t *data)
{
    return import_key(key, PSA_KEY_TYPE_AES, 256, usage, PSA_ALG_GCM, data, 32);
}

/*
 * The form a credential store provider needs: the port hands over the tag apart from the payload.
 * The payload goes in the given number of parts, and the provider may hold back up to a block
 * until finish.
 */
static psa_status_t gcm_seal(psa_aead_operation_t *op, psa_key_id_t key, const uint8_t *nonce,
                             const uint8_t *aad, size_t aad_length, const uint8_t *plain,
                             size_t length, size_t parts, uint8_t *sealed, size_t capacity,
                             uint8_t tag[GCM_TAG_BYTES])
{
    size_t done = 0;
    size_t written = 0;
    size_t tag_length = 0;
    size_t step = 0;
    psa_status_t status = psa_aead_encrypt_setup(op, key, PSA_ALG_GCM);

    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_aead_set_lengths(op, aad_length, length);
    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_aead_set_nonce(op, nonce, GCM_NONCE_BYTES);
    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_aead_update_ad(op, aad, aad_length);
    if (status != PSA_SUCCESS) {
        return status;
    }
    for (size_t part = 0; part < parts; part++) {
        const size_t size = part + 1 == parts ? length - done : length / parts;

        status =
            psa_aead_update(op, plain + done, size, sealed + written, capacity - written, &step);
        if (status != PSA_SUCCESS) {
            return status;
        }
        done += size;
        written += step;
    }
    status = psa_aead_finish(op, sealed + written, capacity - written, &step, tag, GCM_TAG_BYTES,
                             &tag_length);
    if (status != PSA_SUCCESS) {
        return status;
    }
    written += step;
    return written == length && tag_length == GCM_TAG_BYTES ? PSA_SUCCESS
                                                            : PSA_ERROR_CORRUPTION_DETECTED;
}

static psa_status_t gcm_open(psa_aead_operation_t *op, psa_key_id_t key, const uint8_t *nonce,
                             const uint8_t *aad, size_t aad_length, const uint8_t *sealed,
                             size_t length, const uint8_t *tag, uint8_t *plain, size_t capacity)
{
    size_t written = 0;
    size_t step = 0;
    psa_status_t status = psa_aead_decrypt_setup(op, key, PSA_ALG_GCM);

    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_aead_set_lengths(op, aad_length, length);
    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_aead_set_nonce(op, nonce, GCM_NONCE_BYTES);
    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_aead_update_ad(op, aad, aad_length);
    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_aead_update(op, sealed, length, plain, capacity, &written);
    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_aead_verify(op, plain + written, capacity - written, &step, tag, GCM_TAG_BYTES);
    if (status != PSA_SUCCESS) {
        return status;
    }
    return written + step == length ? PSA_SUCCESS : PSA_ERROR_CORRUPTION_DETECTED;
}

CASE(aes256_gcm_known_answer_one_shot)
{
    uint8_t sealed[sizeof vec_gcm_cipher + GCM_TAG_BYTES];
    uint8_t plain[sizeof vec_gcm_plain];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(import_aes256(&r->key, AES_GCM_USAGE, vec_gcm_key));
    CHECK_SUPPORTED(psa_aead_encrypt(r->key, PSA_ALG_GCM, vec_gcm_nonce, sizeof vec_gcm_nonce,
                                     vec_gcm_aad, sizeof vec_gcm_aad, vec_gcm_plain,
                                     sizeof vec_gcm_plain, sealed, sizeof sealed, &length));
    CHECK(length == sizeof sealed);
    CHECK(memcmp(sealed, vec_gcm_cipher, sizeof vec_gcm_cipher) == 0);
    CHECK(memcmp(sealed + sizeof vec_gcm_cipher, vec_gcm_tag, sizeof vec_gcm_tag) == 0);
    CHECK_STATUS(PSA_SUCCESS,
                 psa_aead_decrypt(r->key, PSA_ALG_GCM, vec_gcm_nonce, sizeof vec_gcm_nonce,
                                  vec_gcm_aad, sizeof vec_gcm_aad, sealed, sizeof sealed, plain,
                                  sizeof plain, &length));
    CHECK(length == sizeof plain);
    CHECK(memcmp(plain, vec_gcm_plain, sizeof plain) == 0);
    return NULL;
}

CASE(aes256_gcm_multipart_separate_tag_known_answer)
{
    uint8_t sealed[sizeof vec_gcm_cipher + GCM_TAG_BYTES];
    uint8_t plain[sizeof vec_gcm_plain + GCM_TAG_BYTES];
    uint8_t tag[GCM_TAG_BYTES];

    INIT();
    CHECK_SUPPORTED(import_aes256(&r->key, AES_GCM_USAGE, vec_gcm_key));
    /* Three parts of the payload, none a multiple of the block size. */
    CHECK_SUPPORTED(gcm_seal(&r->aead, r->key, vec_gcm_nonce, vec_gcm_aad, sizeof vec_gcm_aad,
                             vec_gcm_plain, sizeof vec_gcm_plain, 3, sealed, sizeof sealed, tag));
    CHECK(memcmp(sealed, vec_gcm_cipher, sizeof vec_gcm_cipher) == 0);
    CHECK(memcmp(tag, vec_gcm_tag, sizeof tag) == 0);
    CHECK_STATUS(PSA_SUCCESS, psa_aead_abort(&r->aead));
    CHECK_STATUS(PSA_SUCCESS,
                 gcm_open(&r->aead, r->key, vec_gcm_nonce, vec_gcm_aad, sizeof vec_gcm_aad,
                          vec_gcm_cipher, sizeof vec_gcm_cipher, vec_gcm_tag, plain, sizeof plain));
    CHECK(memcmp(plain, vec_gcm_plain, sizeof vec_gcm_plain) == 0);
    return NULL;
}

/* The whole of a default slot, in one part, as crux_credential_port_seal receives it. */
CASE(aes256_gcm_slot_image_known_answer)
{
    static uint8_t plain[VEC_GCM_IMAGE_LENGTH + GCM_TAG_BYTES];
    static uint8_t sealed[VEC_GCM_IMAGE_LENGTH + GCM_TAG_BYTES];
    uint8_t nonce[GCM_NONCE_BYTES];
    uint8_t aad[32];
    uint8_t tag[GCM_TAG_BYTES];
    uint8_t hash[32];
    size_t length = 0;

    INIT();
    for (size_t i = 0; i < sizeof nonce; i++) {
        nonce[i] = (uint8_t)i;
    }
    for (size_t i = 0; i < sizeof aad; i++) {
        aad[i] = (uint8_t)(0x40u + i);
    }
    for (size_t i = 0; i < VEC_GCM_IMAGE_LENGTH; i++) {
        plain[i] = (uint8_t)i;
    }
    CHECK_SUPPORTED(import_aes256(&r->key, AES_GCM_USAGE, vec_hmac_key));
    CHECK_SUPPORTED(gcm_seal(&r->aead, r->key, nonce, aad, sizeof aad, plain, VEC_GCM_IMAGE_LENGTH,
                             1, sealed, sizeof sealed, tag));
    CHECK(memcmp(tag, vec_gcm_image_tag, sizeof tag) == 0);
    CHECK_STATUS(PSA_SUCCESS, psa_hash_compute(PSA_ALG_SHA_256, sealed, VEC_GCM_IMAGE_LENGTH, hash,
                                               sizeof hash, &length));
    CHECK(memcmp(hash, vec_gcm_image_cipher_sha256, sizeof hash) == 0);
    CHECK_STATUS(PSA_SUCCESS, psa_aead_abort(&r->aead));
    memset(plain, 0, sizeof plain);
    CHECK_STATUS(PSA_SUCCESS, gcm_open(&r->aead, r->key, nonce, aad, sizeof aad, sealed,
                                       VEC_GCM_IMAGE_LENGTH, tag, plain, sizeof plain));
    for (size_t i = 0; i < VEC_GCM_IMAGE_LENGTH; i++) {
        CHECK(plain[i] == (uint8_t)i);
    }
    return NULL;
}

/* Only INVALID_SIGNATURE lets a provider tell a corrupt slot from a provider that could not run. */
CASE(aes256_gcm_tampering_is_invalid_signature)
{
    enum { TOTAL = sizeof vec_gcm_cipher + GCM_TAG_BYTES };
    uint8_t sealed[TOTAL];
    uint8_t changed[TOTAL];
    uint8_t aad[sizeof vec_gcm_aad];
    uint8_t nonce[sizeof vec_gcm_nonce];
    uint8_t tag[GCM_TAG_BYTES];
    uint8_t plain[TOTAL];
    size_t length = 0;

    INIT();
    memcpy(sealed, vec_gcm_cipher, sizeof vec_gcm_cipher);
    memcpy(sealed + sizeof vec_gcm_cipher, vec_gcm_tag, sizeof vec_gcm_tag);
    CHECK_SUPPORTED(import_aes256(&r->key, AES_GCM_USAGE, vec_gcm_key));

    memcpy(changed, sealed, sizeof changed);
    changed[TOTAL - 1] ^= 0x01;
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_aead_decrypt(r->key, PSA_ALG_GCM, vec_gcm_nonce, sizeof vec_gcm_nonce,
                                  vec_gcm_aad, sizeof vec_gcm_aad, changed, sizeof changed, plain,
                                  sizeof plain, &length));
    memcpy(changed, sealed, sizeof changed);
    changed[0] ^= 0x80;
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_aead_decrypt(r->key, PSA_ALG_GCM, vec_gcm_nonce, sizeof vec_gcm_nonce,
                                  vec_gcm_aad, sizeof vec_gcm_aad, changed, sizeof changed, plain,
                                  sizeof plain, &length));
    memcpy(aad, vec_gcm_aad, sizeof aad);
    aad[0] ^= 0x01;
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_aead_decrypt(r->key, PSA_ALG_GCM, vec_gcm_nonce, sizeof vec_gcm_nonce, aad,
                                  sizeof aad, sealed, sizeof sealed, plain, sizeof plain, &length));
    memcpy(nonce, vec_gcm_nonce, sizeof nonce);
    nonce[0] ^= 0x01;
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_aead_decrypt(r->key, PSA_ALG_GCM, nonce, sizeof nonce, vec_gcm_aad,
                                  sizeof vec_gcm_aad, sealed, sizeof sealed, plain, sizeof plain,
                                  &length));

    /* The same through the multipart path, with a tag kept apart. */
    memcpy(tag, vec_gcm_tag, sizeof tag);
    tag[GCM_TAG_BYTES - 1] ^= 0x01;
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 gcm_open(&r->aead, r->key, vec_gcm_nonce, vec_gcm_aad, sizeof vec_gcm_aad,
                          vec_gcm_cipher, sizeof vec_gcm_cipher, tag, plain, sizeof plain));
    CHECK_STATUS(PSA_SUCCESS, psa_aead_abort(&r->aead));
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 gcm_open(&r->aead, r->key, vec_gcm_nonce, aad, sizeof aad, vec_gcm_cipher,
                          sizeof vec_gcm_cipher, vec_gcm_tag, plain, sizeof plain));
    CHECK_STATUS(PSA_SUCCESS, psa_aead_abort(&r->aead));

    /* A different key: a card moved to another board. */
    CHECK_STATUS(PSA_SUCCESS, import_aes256(&r->key2, AES_GCM_USAGE, vec_hmac_key));
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 gcm_open(&r->aead, r->key2, vec_gcm_nonce, vec_gcm_aad, sizeof vec_gcm_aad,
                          vec_gcm_cipher, sizeof vec_gcm_cipher, vec_gcm_tag, plain, sizeof plain));
    return NULL;
}

CASE(aes256_gcm_key_policy_not_permitted)
{
    uint8_t sealed[sizeof vec_gcm_cipher + GCM_TAG_BYTES];
    uint8_t plain[sizeof vec_gcm_plain];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(import_aes256(&r->key, PSA_KEY_USAGE_ENCRYPT, vec_gcm_key));
    memcpy(sealed, vec_gcm_cipher, sizeof vec_gcm_cipher);
    memcpy(sealed + sizeof vec_gcm_cipher, vec_gcm_tag, sizeof vec_gcm_tag);
    CHECK_STATUS(PSA_ERROR_NOT_PERMITTED,
                 psa_aead_decrypt(r->key, PSA_ALG_GCM, vec_gcm_nonce, sizeof vec_gcm_nonce,
                                  vec_gcm_aad, sizeof vec_gcm_aad, sealed, sizeof sealed, plain,
                                  sizeof plain, &length));
    CHECK_STATUS(PSA_ERROR_NOT_PERMITTED, psa_aead_decrypt_setup(&r->aead, r->key, PSA_ALG_GCM));
    return NULL;
}

CASE(aes256_key_import_wrong_size_rejected)
{
    INIT();
    /* 31 bytes declared as a 256-bit key. */
    CHECK_STATUS(
        PSA_ERROR_INVALID_ARGUMENT,
        import_key(&r->key, PSA_KEY_TYPE_AES, 256, AES_GCM_USAGE, PSA_ALG_GCM, vec_gcm_key, 31));
    CHECK(r->key == PSA_KEY_ID_NULL);
    return NULL;
}

/* ---- key generation, for crux_credential_port_generate_key ---- */

#define P256_PAIR PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)
#define P256_PUBLIC PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1)
#define ED25519_PAIR PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_TWISTED_EDWARDS)
#define ED25519_PUBLIC PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_TWISTED_EDWARDS)
#define P256_GENERATE_USAGE                                                                        \
    (PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_EXPORT)
#define ED25519_USAGE                                                                              \
    (PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE | PSA_KEY_USAGE_EXPORT)

/* Buffers are longer than the forms the store requires, so a provider must report the length. */
CASE(p256_generate_export_forms)
{
    uint8_t public_a[80];
    uint8_t public_b[80];
    uint8_t private_a[48];
    uint8_t private_b[48];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(generate_key(&r->key, P256_PAIR, 256, P256_GENERATE_USAGE, ECDSA_SHA256));
    CHECK_STATUS(PSA_SUCCESS, psa_export_key(r->key, private_a, sizeof private_a, &length));
    CHECK(length == 32);
    CHECK(!all_bytes(private_a, 32, 0x00));
    CHECK_STATUS(PSA_SUCCESS, psa_export_public_key(r->key, public_a, sizeof public_a, &length));
    CHECK(length == 65);
    CHECK(public_a[0] == 0x04);

    CHECK_STATUS(PSA_SUCCESS,
                 generate_key(&r->key2, P256_PAIR, 256, P256_GENERATE_USAGE, ECDSA_SHA256));
    CHECK_STATUS(PSA_SUCCESS, psa_export_key(r->key2, private_b, sizeof private_b, &length));
    CHECK_STATUS(PSA_SUCCESS, psa_export_public_key(r->key2, public_b, sizeof public_b, &length));
    CHECK(memcmp(private_a, private_b, 32) != 0);
    CHECK(memcmp(public_a, public_b, 65) != 0);
    return NULL;
}

CASE(p256_generated_key_signs_and_verifies)
{
    uint8_t public_key[65];
    uint8_t signature[64];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(generate_key(&r->key, P256_PAIR, 256, P256_GENERATE_USAGE, ECDSA_SHA256));
    CHECK_STATUS(PSA_SUCCESS,
                 psa_sign_hash(r->key, ECDSA_SHA256, vec_sha256_sample, sizeof vec_sha256_sample,
                               signature, sizeof signature, &length));
    CHECK(length == sizeof signature);
    CHECK_STATUS(PSA_SUCCESS,
                 psa_export_public_key(r->key, public_key, sizeof public_key, &length));
    CHECK(length == sizeof public_key);
    /* The exported form is what a later reader imports. The import checks that it is on the curve.
     */
    CHECK_STATUS(PSA_SUCCESS, import_key(&r->key2, P256_PUBLIC, 256, PSA_KEY_USAGE_VERIFY_HASH,
                                         ECDSA_SHA256, public_key, sizeof public_key));
    CHECK_STATUS(PSA_SUCCESS,
                 psa_verify_hash(r->key2, ECDSA_SHA256, vec_sha256_sample, sizeof vec_sha256_sample,
                                 signature, sizeof signature));
    signature[10] ^= 0x01;
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_verify_hash(r->key2, ECDSA_SHA256, vec_sha256_sample, sizeof vec_sha256_sample,
                                 signature, sizeof signature));
    return NULL;
}

CASE(p256_private_import_gives_known_public)
{
    uint8_t public_key[65];
    uint8_t private_key[32];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(import_key(&r->key, P256_PAIR, 256, P256_GENERATE_USAGE, ECDSA_SHA256,
                               vec_p256_private, sizeof vec_p256_private));
    CHECK_STATUS(PSA_SUCCESS,
                 psa_export_public_key(r->key, public_key, sizeof public_key, &length));
    CHECK(length == sizeof public_key);
    CHECK(memcmp(public_key, vec_p256_public_uncompressed, sizeof public_key) == 0);
    CHECK_STATUS(PSA_SUCCESS, psa_export_key(r->key, private_key, sizeof private_key, &length));
    CHECK(length == sizeof private_key);
    CHECK(memcmp(private_key, vec_p256_private, sizeof private_key) == 0);
    return NULL;
}

CASE(p256_private_export_requires_usage)
{
    uint8_t private_key[32];
    uint8_t public_key[65];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(generate_key(&r->key, P256_PAIR, 256, PSA_KEY_USAGE_SIGN_HASH, ECDSA_SHA256));
    CHECK_STATUS(PSA_ERROR_NOT_PERMITTED,
                 psa_export_key(r->key, private_key, sizeof private_key, &length));
    /* The public part needs no usage flag. */
    CHECK_STATUS(PSA_SUCCESS,
                 psa_export_public_key(r->key, public_key, sizeof public_key, &length));
    CHECK(length == sizeof public_key);
    return NULL;
}

CASE(ed25519_known_answer_rfc8032)
{
    uint8_t public_key[32];
    uint8_t seed[32];
    uint8_t signature[64];
    size_t length = 0;

    psa_status_t status;

    INIT();
    status = import_key(&r->key, ED25519_PAIR, 255, ED25519_USAGE, PSA_ALG_PURE_EDDSA,
                        vec_ed25519_seed, sizeof vec_ed25519_seed);
    /* A provider without this key family can reject the import as an invalid key type. This is a
     * published valid key, so the result counts as a gap, and an undeclared gap still fails. */
    if (status == PSA_ERROR_INVALID_ARGUMENT) {
        return not_supported_marker;
    }
    CHECK_SUPPORTED(status);
    CHECK_STATUS(PSA_SUCCESS,
                 psa_export_public_key(r->key, public_key, sizeof public_key, &length));
    CHECK(length == sizeof public_key);
    CHECK(memcmp(public_key, vec_ed25519_public, sizeof public_key) == 0);
    CHECK_STATUS(PSA_SUCCESS, psa_export_key(r->key, seed, sizeof seed, &length));
    CHECK(length == sizeof seed);
    CHECK(memcmp(seed, vec_ed25519_seed, sizeof seed) == 0);
    CHECK_STATUS(PSA_SUCCESS, psa_sign_message(r->key, PSA_ALG_PURE_EDDSA, vec_ed25519_message,
                                               sizeof vec_ed25519_message, signature,
                                               sizeof signature, &length));
    CHECK(length == sizeof signature);
    CHECK(memcmp(signature, vec_ed25519_signature, sizeof signature) == 0);
    return NULL;
}

CASE(ed25519_generate_export_forms)
{
    uint8_t public_a[48];
    uint8_t public_b[48];
    uint8_t seed_a[48];
    uint8_t seed_b[48];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(generate_key(&r->key, ED25519_PAIR, 255, ED25519_USAGE, PSA_ALG_PURE_EDDSA));
    CHECK_STATUS(PSA_SUCCESS, psa_export_key(r->key, seed_a, sizeof seed_a, &length));
    CHECK(length == 32);
    CHECK(!all_bytes(seed_a, 32, 0x00));
    CHECK_STATUS(PSA_SUCCESS, psa_export_public_key(r->key, public_a, sizeof public_a, &length));
    CHECK(length == 32);

    CHECK_STATUS(PSA_SUCCESS,
                 generate_key(&r->key2, ED25519_PAIR, 255, ED25519_USAGE, PSA_ALG_PURE_EDDSA));
    CHECK_STATUS(PSA_SUCCESS, psa_export_key(r->key2, seed_b, sizeof seed_b, &length));
    CHECK_STATUS(PSA_SUCCESS, psa_export_public_key(r->key2, public_b, sizeof public_b, &length));
    CHECK(memcmp(seed_a, seed_b, 32) != 0);
    CHECK(memcmp(public_a, public_b, 32) != 0);
    return NULL;
}

CASE(ed25519_generated_key_signs_and_verifies)
{
    uint8_t public_key[32];
    uint8_t signature[64];
    size_t length = 0;

    INIT();
    CHECK_SUPPORTED(generate_key(&r->key, ED25519_PAIR, 255, ED25519_USAGE, PSA_ALG_PURE_EDDSA));
    CHECK_STATUS(PSA_SUCCESS, psa_sign_message(r->key, PSA_ALG_PURE_EDDSA, vec_ed25519_message,
                                               sizeof vec_ed25519_message, signature,
                                               sizeof signature, &length));
    CHECK(length == sizeof signature);
    CHECK_STATUS(PSA_SUCCESS,
                 psa_export_public_key(r->key, public_key, sizeof public_key, &length));
    CHECK(length == sizeof public_key);
    CHECK_STATUS(PSA_SUCCESS,
                 import_key(&r->key2, ED25519_PUBLIC, 255, PSA_KEY_USAGE_VERIFY_MESSAGE,
                            PSA_ALG_PURE_EDDSA, public_key, sizeof public_key));
    CHECK_STATUS(PSA_SUCCESS,
                 psa_verify_message(r->key2, PSA_ALG_PURE_EDDSA, vec_ed25519_message,
                                    sizeof vec_ed25519_message, signature, sizeof signature));
    signature[10] ^= 0x01;
    CHECK_STATUS(PSA_ERROR_INVALID_SIGNATURE,
                 psa_verify_message(r->key2, PSA_ALG_PURE_EDDSA, vec_ed25519_message,
                                    sizeof vec_ed25519_message, signature, sizeof signature));
    return NULL;
}
