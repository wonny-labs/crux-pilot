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
    X(ecdsa_p256_flipped_signature_invalid)                                                        \
    X(ecdsa_p256_zero_signature_invalid)                                                           \
    X(ecdsa_p256_truncated_signature_rejected)                                                     \
    X(ecdsa_p256_tampered_input_invalid)                                                           \
    X(ecdsa_p256_off_curve_import_rejected)                                                        \
    X(ecdsa_p256_missing_prefix_import_rejected)                                                   \
    X(ecdsa_p256_off_curve_key_never_verifies)                                                     \
    X(ecdsa_p256_key_policy_not_permitted)                                                         \
    X(pbkdf2_hmac_sha256_known_answer_rfc7914)                                                     \
    X(pbkdf2_hmac_sha256_known_answer_4096)                                                        \
    X(pbkdf2_wrong_password_derives_different_output)                                              \
    X(pbkdf2_verify_bytes_when_supported)                                                          \
    X(kdf_output_on_inactive_operation_is_bad_state)                                               \
    X(kdf_unsupported_algorithm_not_supported)                                                     \
    X(destroyed_key_is_invalid_handle)                                                             \
    X(hmac_sha256_known_answer)                                                                    \
    X(hmac_sha256_empty_input_known_answer)                                                        \
    X(hmac_sha256_buffer_too_small)                                                                \
    X(hmac_key_policy_not_permitted)                                                               \
    X(pbkdf2_longest_password_known_answer)                                                        \
    X(pbkdf2_empty_password_known_answer)                                                          \
    X(aes256_gcm_known_answer_one_shot)                                                            \
    X(aes256_gcm_multipart_separate_tag_known_answer)                                              \
    X(aes256_gcm_slot_image_known_answer)                                                          \
    X(aes256_gcm_tampering_is_invalid_signature)                                                   \
    X(aes256_gcm_key_policy_not_permitted)                                                         \
    X(aes256_key_import_wrong_size_rejected)                                                       \
    X(p256_generate_export_forms)                                                                  \
    X(p256_generated_key_signs_and_verifies)                                                       \
    X(p256_private_import_gives_known_public)                                                      \
    X(p256_private_export_requires_usage)                                                          \
    X(ed25519_known_answer_rfc8032)                                                                \
    X(ed25519_generate_export_forms)                                                               \
    X(ed25519_generated_key_signs_and_verifies)

#define PSA_CASE_DECLARE(name) const char *psa_case_##name(void);
PSA_CASE_LIST(PSA_CASE_DECLARE)
#undef PSA_CASE_DECLARE

/*
 * A case that passes because the provider reports an optional feature as
 * PSA_ERROR_NOT_SUPPORTED leaves a note here. NULL when the last case ran its
 * checks in full. Harnesses print the note next to PASS so it is not silent.
 */
const char *psa_case_note(void);

/*
 * The result of a case when the provider reports PSA_ERROR_NOT_SUPPORTED for what the case needs.
 * It is not a pass. A composition declares which cases it expects this from, in the definition
 * PSA_CASES_DECLARED_UNSUPPORTED, a comma-separated list of case names, and psa_case_outcome()
 * compares the two. So a gap has to be declared, and a declaration cannot go stale.
 * PSA_CASES_DECLARED_DEVIATIONS does the same for a case that a provider is recorded as failing.
 */
typedef enum {
    PSA_OUTCOME_PASS,
    PSA_OUTCOME_DECLARED_UNSUPPORTED,
    PSA_OUTCOME_DECLARED_DEVIATION,
    PSA_OUTCOME_FAIL
} psa_outcome_t;

/* Classifies what case name returned. Sets *text to a description for PSA_OUTCOME_FAIL and
 * PSA_OUTCOME_DECLARED_DEVIATION. */
psa_outcome_t psa_case_outcome(const char *name, const char *result, const char **text);

/*
 * Prints one line, PSA_RANDOM_PROBE followed by 16 random bytes in hexadecimal, or the status on an
 * error. The script compares runs to check what is declared about each entropy source.
 */
void psa_case_print_random_probe(void);

#define PSA_CASE_ONE(name) +1
#define PSA_CASE_COUNT (0 PSA_CASE_LIST(PSA_CASE_ONE))

/* Harnesses compare against this so dropping a case from the list is noticed. */
#define PSA_CASE_EXPECTED_COUNT 45

#endif
