/*
 * Provider-neutral PSA Crypto conformance cases (Issue #142).
 *
 * The cases use only <psa/crypto.h> and ISO C. They never name a provider, a
 * Crux component, Zephyr, a board or a vendor. The composition (a Host CMake
 * project or a Zephyr application) selects the provider and a thin harness
 * runs each case.
 *
 * A case returns NULL on success, or a text describing the first failed check.
 * The text is only valid until the next case runs (single-threaded harness).
 *
 * The cases assert behaviour the PSA Crypto API documents for the pinned
 * headers. Behaviour the specification leaves undefined, such as calling a
 * function before psa_crypto_init(), is deliberately not asserted.
 */
#ifndef CRUX_TESTS_PSA_CASES_H
#define CRUX_TESTS_PSA_CASES_H

/* Order is the execution order on Host. Names are the case identifiers. */
#define PSA_CASE_LIST(X)                                                                           \
    X(init_is_idempotent)                                                                          \
    X(random_fills_requested_bytes_only)                                                           \
    X(random_large_request_succeeds)                                                               \
    X(sha256_known_answer_one_shot)                                                                \
    X(sha256_multipart_matches_known_answer)                                                       \
    X(sha256_update_after_finish_is_bad_state)                                                     \
    X(sha256_buffer_too_small)                                                                     \
    X(sha256_verify_mismatch_invalid_signature)                                                    \
    X(hash_unsupported_algorithm_not_supported)                                                    \
    X(ecdsa_p256_import_is_volatile)                                                               \
    X(ecdsa_p256_verify_hash_known_answer)                                                         \
    X(ecdsa_p256_wrong_signature_invalid)                                                          \
    X(ecdsa_p256_tampered_input_invalid)                                                           \
    X(ecdsa_p256_invalid_public_key_rejected)                                                      \
    X(ecdsa_p256_key_policy_not_permitted)                                                         \
    X(pbkdf2_hmac_sha256_known_answer_rfc7914)                                                     \
    X(pbkdf2_hmac_sha256_known_answer_4096)                                                        \
    X(pbkdf2_wrong_password_derives_different_output)                                              \
    X(pbkdf2_verify_bytes_when_supported)                                                          \
    X(kdf_output_on_inactive_operation_is_bad_state)                                               \
    X(kdf_unsupported_algorithm_not_supported)                                                     \
    X(destroyed_key_is_invalid_handle)

#define PSA_CASE_DECLARE(name) const char *psa_case_##name(void);
PSA_CASE_LIST(PSA_CASE_DECLARE)
#undef PSA_CASE_DECLARE

/*
 * A case that passes because the provider reports an optional feature as
 * PSA_ERROR_NOT_SUPPORTED leaves a note here. NULL when the last case ran its
 * checks in full. Harnesses print the note next to PASS so it is not silent.
 */
const char *psa_case_note(void);

#define PSA_CASE_ONE(name) +1
#define PSA_CASE_COUNT (0 PSA_CASE_LIST(PSA_CASE_ONE))

/* Harnesses compare against this so dropping a case from the list is noticed. */
#define PSA_CASE_EXPECTED_COUNT 22

#endif
