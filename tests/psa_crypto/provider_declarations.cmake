# What each provider is declared to do differently from the conformance cases. The harnesses compare
# these with what a run does, in both directions, so a gap cannot pass silently and a declaration
# cannot go stale. See docs/psa-crypto-qualification.md.
#
# UNSUPPORTED: the case must report that the provider lacks what it needs.
# DEVIATIONS: the case must fail at its one marked assertion, with the one status named there, for a
# reason recorded in the qualification record. Any other status or failure in the case stays a failure.
# The case still states what the PSA Crypto API documents.
set(CRUX_PSA_UNSUPPORTED_TF_PSA_CRYPTO
    "ed25519_known_answer_rfc8032,ed25519_generate_export_forms,ed25519_generated_key_signs_and_verifies")
set(CRUX_PSA_DEVIATIONS_TF_PSA_CRYPTO "")
set(CRUX_PSA_UNSUPPORTED_WOLFPSA "")
set(CRUX_PSA_DEVIATIONS_WOLFPSA
    "ecdsa_p256_zero_signature_invalid,ecdsa_p256_off_curve_import_rejected")

# wolfPSA's multipart AEAD reports PSA_ERROR_NOT_SUPPORTED for GCM without this, and the wolfSSL
# module's Kconfig has no option for it. Its own standalone settings already define it.
set(CRUX_PSA_WOLFCRYPT_DEFINITIONS WOLFSSL_AESGCM_STREAM)
