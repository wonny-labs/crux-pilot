/**
 * @file
 * @brief Start, and the certificate and key for a TLS provider, for a composition only. Projects
 * must not include this header.
 *
 * The target that carries this header links the credential adapter, so the credential boundary rule
 * refuses any public target or project that reaches it, as it refuses the credential adapter.
 *
 * A TLS provider registers the material below with its stack, such as Zephyr's TLS credentials,
 * which both the Mbed TLS and the wolfSSL socket backends read. Zephyr keeps the pointers it is
 * given, so this module owns two certificate buffers and one key buffer, which stay valid and
 * unchanged while taken. Nothing here depends on either TLS library.
 *
 * The private key leaves this module only as the material's PKCS #8 bytes, to the TLS stack in the
 * same image. No public operation, management route, command or file area returns it.
 *
 * Thread context only. crux_tls_certificate_start() must return before any other operation starts.
 */
#ifndef CRUX_TLS_CERTIFICATE_ADAPTER_H
#define CRUX_TLS_CERTIFICATE_ADAPTER_H

#include <crux/tls_certificate.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * notBefore of the reference self-signed certificate when no trusted clock is given:
 * 2026-01-01T00:00:00Z.
 */
#define CRUX_TLS_CERTIFICATE_DEFAULT_NOT_BEFORE_S 1767225600
/** The latest notBefore or notAfter: 9999-12-31T23:59:59Z. */
#define CRUX_TLS_CERTIFICATE_TIME_MAX_S 253402300799
/** The PKCS #8 form of a P-256 key with its public point, which the material holds. */
#define CRUX_TLS_CERTIFICATE_KEY_DER_BYTES 138u

/** What a self-signed certificate holds. Borrowed for the program's life. */
typedef struct {
    crux_tls_certificate_names_t names;
    /**
     * notBefore, in Unix seconds, UTC. The module reads no clock. Pass
     * CRUX_TLS_CERTIFICATE_DEFAULT_NOT_BEFORE_S unless the composition trusts its clock.
     */
    int64_t not_before_s;
    /**
     * Days from notBefore to notAfter, or 0 for notAfter 99991231235959Z, the "no well-defined
     * expiration" of RFC 5280, which needs no clock to renew.
     */
    uint32_t validity_days;
} crux_tls_certificate_options_t;

/** Borrowed material for a TLS stack. */
typedef struct {
    const uint8_t *certificate; /**< DER, unchanged while taken. */
    size_t certificate_length;
    const uint8_t *private_key; /**< PKCS #8 DER, unchanged until the program ends. */
    size_t private_key_length;
    uint32_t generation; /**< Changes with each certificate made active. */
} crux_tls_certificate_material_t;

/** How start went. */
typedef enum {
    CRUX_TLS_CERTIFICATE_STARTED = 0,   /**< The stored certificate matches the key. */
    CRUX_TLS_CERTIFICATE_GENERATED = 1, /**< None was stored, so a self-signed one was made. */
} crux_tls_certificate_start_t;

/**
 * Make the device key exist and load or create the certificate. Call it once the credential store
 * has started, and before any TLS listener starts. A failure can be retried. Ensures the
 * CRUX_CREDENTIAL_RECORD_TLS_KEY record as an ECDSA P-256 key, and keeps an existing key whatever
 * its type, as the store does.
 *
 * A stored certificate that is malformed or does not match the key is never replaced here. It is
 * logged as web.certificate_rejected, no certificate is active, a TLS provider has nothing to
 * serve, and only crux_tls_certificate_install() or crux_tls_certificate_regenerate(), for example
 * from the serial console, makes one active.
 *
 * @param options Borrowed for the program's life.
 * @param[out] how Written only on success.
 * @retval CRUX_TLS_CERTIFICATE_OK A certificate is active.
 * @retval CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT Bad options. Nothing was touched.
 * @retval CRUX_TLS_CERTIFICATE_ERR_INVALID The stored key is not P-256, or the stored certificate
 * record is not this module's form.
 * @retval CRUX_TLS_CERTIFICATE_ERR_MALFORMED The stored certificate does not parse. Failed closed.
 * @retval CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH The stored certificate is another key's, as after a
 * key rotation. Failed closed.
 * @retval CRUX_TLS_CERTIFICATE_ERR_STORE The key or the new certificate could not be read or saved.
 * @retval CRUX_TLS_CERTIFICATE_ERR_CRYPTO The key could not be generated, or the certificate
 * signed.
 * @retval CRUX_TLS_CERTIFICATE_ERR_NOT_READY The store is unusable or not started, or a lock could
 * not be made.
 * @retval CRUX_TLS_CERTIFICATE_ERR_BUSY Already started.
 */
crux_tls_certificate_result_t
crux_tls_certificate_start(const crux_tls_certificate_options_t *options,
                           crux_tls_certificate_start_t *how);

/**
 * Take the active certificate and the key for a TLS stack. Its buffer stays unchanged until it is
 * released. An install or regeneration writes the other buffer, and returns BUSY while that one is
 * taken too, so a stack that registers the new material and then releases the old never sees
 * either change under it.
 *
 * @param[out] out Written only on success.
 * @retval CRUX_TLS_CERTIFICATE_OK Taken. Call crux_tls_certificate_material_release() with it.
 * @retval CRUX_TLS_CERTIFICATE_ERR_NOT_READY No certificate is active.
 * @retval CRUX_TLS_CERTIFICATE_ERR_BUSY Another operation runs.
 * @retval CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT No output.
 */
crux_tls_certificate_result_t
crux_tls_certificate_material_take(crux_tls_certificate_material_t *out);

/**
 * Give back material once the TLS stack no longer refers to it, such as after
 * tls_credential_delete(). Waits for the lock, briefly.
 * @retval CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT Not material that is taken now.
 * @retval CRUX_TLS_CERTIFICATE_ERR_BUSY The lock was not free in time. Retry.
 */
crux_tls_certificate_result_t
crux_tls_certificate_material_release(const crux_tls_certificate_material_t *material);

/**
 * Set what runs after a certificate becomes active through an install or a regeneration. It runs on
 * that operation's thread, with no lock held, and may take and release material. NULL clears it.
 * Set it before start, from the thread that starts.
 */
void crux_tls_certificate_on_change(void (*changed)(void *context), void *context);

/**
 * Copy the active certificate's fingerprint, which is public, without a session, for the serial
 * console or a boot log to show before anyone can log in.
 * @retval CRUX_TLS_CERTIFICATE_ERR_NOT_READY No certificate is active.
 * @retval CRUX_TLS_CERTIFICATE_ERR_BUSY Another operation runs.
 * @retval CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT No output.
 */
crux_tls_certificate_result_t
crux_tls_certificate_fingerprint(char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
