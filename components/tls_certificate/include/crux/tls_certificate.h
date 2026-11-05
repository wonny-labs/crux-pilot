/**
 * @file
 * @brief The device's TLS certificate: what it is, a certificate request, and an operator's
 * replacement, each checked against a session.
 *
 * At first boot the composition's start generates an ECDSA P-256 key in the credential store and a
 * self-signed certificate for it. An operator can then create a certificate request signed by that
 * key, install the certificate a CA returns, or install any certificate for the device's key.
 *
 * An installed certificate is checked for format and for a match with the device key only. Its
 * signature, its chain, its trust, its revocation, its names and its dates are not checked. Every
 * result that describes a certificate carries CRUX_TLS_CERTIFICATE_CHECKS, which says so.
 *
 * A rejected certificate changes nothing: the previous one stays active. No operation here returns
 * the private key, and no output holds it.
 *
 * Every operation needs CRUX_TLS_CERTIFICATE_PERMISSION_MANAGE. Thread context only. An operation
 * that saves waits on storage, so a web route runs it on a worker, not on the server thread. An
 * operation that finds another one running returns CRUX_TLS_CERTIFICATE_ERR_BUSY and does not wait.
 * See docs/tls-certificate.md.
 */
#ifndef CRUX_TLS_CERTIFICATE_H
#define CRUX_TLS_CERTIFICATE_H

#include <crux/access.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Permission every operation here requires. */
#define CRUX_TLS_CERTIFICATE_PERMISSION_MANAGE "tls.manage"

/** What a certificate result states about the checks. Every info carries it. */
#define CRUX_TLS_CERTIFICATE_CHECKS                                                                \
    "Checked for DER format and a match with the device key only. Not checked: signature, chain, " \
    "trust, revocation, names or dates."

/** Largest certificate the device holds, in DER bytes. */
#define CRUX_TLS_CERTIFICATE_DER_MAX 4088u
/** Largest certificate request, in DER bytes, at the largest names allowed. */
#define CRUX_TLS_CERTIFICATE_CSR_MAX 1536u
/** SHA-256 fingerprint as uppercase hexadecimal pairs joined by colons, and its terminator. */
#define CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE 96u
/** A common name as shown, and its terminator. */
#define CRUX_TLS_CERTIFICATE_NAME_SIZE 65u
/** Most names in a certificate's subject alternative names, of every kind together. */
#define CRUX_TLS_CERTIFICATE_ALT_NAMES_MAX 4u
/** Longest DNS name. */
#define CRUX_TLS_CERTIFICATE_DNS_NAME_MAX 253u

/** Results; zero is success. */
typedef enum {
    CRUX_TLS_CERTIFICATE_OK = 0,
    CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT = -1, /**< A missing input or output, or bad names. */
    /** Not started, the access core is not ready, or no certificate is active (failed closed). */
    CRUX_TLS_CERTIFICATE_ERR_NOT_READY = -2,
    CRUX_TLS_CERTIFICATE_ERR_SESSION_INVALID = -3,
    CRUX_TLS_CERTIFICATE_ERR_DENIED = -4,
    CRUX_TLS_CERTIFICATE_ERR_BUSY = -5, /**< Another operation, or the TLS side holds both copies. */
    CRUX_TLS_CERTIFICATE_ERR_MALFORMED = -6,    /**< Not one DER X.509 certificate. */
    CRUX_TLS_CERTIFICATE_ERR_INVALID = -7,      /**< Well formed, but not one this device can use. */
    CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH = -8, /**< Its public key is not the device key. */
    CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE = -9,    /**< Over the limit, or the output is too small. */
    /**
     * The save failed. The previous certificate stays active in RAM. As for any replaced credential
     * record, a failed save can still win at the next boot if the store could not invalidate it.
     */
    CRUX_TLS_CERTIFICATE_ERR_STORE = -10,
    CRUX_TLS_CERTIFICATE_ERR_CRYPTO = -11 /**< Hash, signature or random source failed. */
} crux_tls_certificate_result_t;

/** Where the active certificate came from. */
typedef enum {
    CRUX_TLS_CERTIFICATE_SOURCE_SELF_SIGNED = 1, /**< Generated on the device. */
    CRUX_TLS_CERTIFICATE_SOURCE_INSTALLED = 2    /**< Installed by an operator. */
} crux_tls_certificate_source_t;

/**
 * Names for a self-signed certificate or a request. Borrowed for the call, or for the program's
 * life when given to the composition's start.
 *
 * A common name is 1 to 64 of [A-Za-z0-9 ._-]. A DNS name is 1 to 253 of [A-Za-z0-9.-], with no
 * empty label, and may start with "*.". At most CRUX_TLS_CERTIFICATE_ALT_NAMES_MAX names in all.
 */
typedef struct {
    const char *common_name;
    const char *const *dns_names;
    size_t dns_count;
    const uint8_t (*ipv4)[4];
    size_t ipv4_count;
    const uint8_t (*ipv6)[16];
    size_t ipv6_count;
} crux_tls_certificate_names_t;

/** A certificate as the device describes it. Holds nothing secret. */
typedef struct {
    crux_tls_certificate_source_t source;
    uint8_t fingerprint[32]; /**< SHA-256 of the DER certificate. */
    char fingerprint_text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    /** First common name of the subject, with any byte outside printable ASCII shown as '?'. */
    char subject[CRUX_TLS_CERTIFICATE_NAME_SIZE];
    char issuer[CRUX_TLS_CERTIFICATE_NAME_SIZE]; /**< The same for the issuer. */
    int64_t not_before_s;                        /**< Unix seconds, UTC. */
    int64_t not_after_s; /**< Unix seconds, UTC, 253402300799 when no_expiry. */
    bool no_expiry;      /**< notAfter is 99991231235959Z, RFC 5280's "no well-defined" date. */
    size_t length;       /**< DER bytes. */
    const char *checks;  /**< CRUX_TLS_CERTIFICATE_CHECKS. */
} crux_tls_certificate_info_t;

/** A certificate's dates against a clock, which the device may not have. */
typedef enum {
    CRUX_TLS_CERTIFICATE_DATES_UNKNOWN = 0, /**< No clock: nothing is claimed. */
    CRUX_TLS_CERTIFICATE_DATES_VALID = 1,
    CRUX_TLS_CERTIFICATE_DATES_NOT_YET_VALID = 2,
    CRUX_TLS_CERTIFICATE_DATES_EXPIRED = 3
} crux_tls_certificate_dates_t;

/**
 * Describe the active certificate.
 * @param[out] out Written only on success.
 * @retval CRUX_TLS_CERTIFICATE_OK Described.
 * @retval CRUX_TLS_CERTIFICATE_ERR_NOT_READY Not started, or no certificate is active.
 * @return Otherwise a session, permission, argument or BUSY result.
 */
crux_tls_certificate_result_t crux_tls_certificate_get(crux_access_session_t session,
                                                       crux_tls_certificate_info_t *out);

/**
 * Copy the active certificate's DER bytes, which are public, for a client to save or pin.
 * @param[out] der Written only on success.
 * @param[out] length The certificate's size on OK and TOO_LARGE, zero otherwise.
 * @retval CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE capacity is below the certificate's size.
 * @return Otherwise as crux_tls_certificate_get().
 */
crux_tls_certificate_result_t crux_tls_certificate_copy(crux_access_session_t session,
                                                        uint8_t *der, size_t capacity,
                                                        size_t *length);

/**
 * Create a PKCS #10 certificate request for the device key, signed by it, with these names as its
 * subject and its requested subject alternative names. Changes nothing on the device.
 *
 * @param names NULL for the names the composition gave at start.
 * @param[out] der Written only on success. CRUX_TLS_CERTIFICATE_CSR_MAX bytes always suffice.
 * @param[out] length The request's size on success, zero otherwise.
 * @retval CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT Bad names, or a missing output.
 * @retval CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE capacity is too small.
 * @retval CRUX_TLS_CERTIFICATE_ERR_CRYPTO The hash or the signature failed.
 * @return Otherwise a session, permission, NOT_READY or BUSY result.
 */
crux_tls_certificate_result_t
crux_tls_certificate_request(crux_access_session_t session, const crux_tls_certificate_names_t *names,
                             uint8_t *der, size_t capacity, size_t *length);

/**
 * Replace the active certificate with an operator's, after checking that it is one DER X.509 v3
 * certificate whose public key is the device's P-256 key. Nothing else is checked. A rejection, for
 * any reason after the permission check, is logged as web.certificate_rejected and keeps the
 * previous certificate active. Success is logged as web.certificate_replaced, and the composition's
 * change callback then runs on this thread.
 *
 * Allowed when the stored certificate was refused at start, so that an operator can recover.
 *
 * @param der The upload, borrowed for the call.
 * @param[out] out The installed certificate, or NULL. Written only on success.
 * @retval CRUX_TLS_CERTIFICATE_OK Installed and saved.
 * @retval CRUX_TLS_CERTIFICATE_ERR_MALFORMED Not one strict DER certificate, with nothing after it.
 * @retval CRUX_TLS_CERTIFICATE_ERR_INVALID Not X.509 v3, not a P-256 key, differing signature
 * algorithms, or notAfter before notBefore.
 * @retval CRUX_TLS_CERTIFICATE_ERR_KEY_MISMATCH Another key's certificate.
 * @retval CRUX_TLS_CERTIFICATE_ERR_TOO_LARGE Over CRUX_TLS_CERTIFICATE_DER_MAX.
 * @retval CRUX_TLS_CERTIFICATE_ERR_STORE The save failed. See crux_tls_certificate_result_t.
 * @retval CRUX_TLS_CERTIFICATE_ERR_BUSY Another operation runs, or the TLS side still holds the
 * certificate this one would overwrite. Nothing was checked or logged.
 * @retval CRUX_TLS_CERTIFICATE_ERR_INVALID_ARGUMENT No data. Nothing was logged.
 * @return Otherwise a session, permission or NOT_READY result, with nothing logged here.
 */
crux_tls_certificate_result_t crux_tls_certificate_install(crux_access_session_t session,
                                                           const uint8_t *der, size_t length,
                                                           crux_tls_certificate_info_t *out);

/**
 * Replace the active certificate with a new self-signed one for the same key, with a new serial
 * number and the start's dates. This is how an operator returns from an installed certificate,
 * recovers from a refused one, or follows an address change.
 *
 * @param names NULL for the names the composition gave at start.
 * @param[out] out The new certificate, or NULL. Written only on success.
 * @return As crux_tls_certificate_install(), with CRYPTO for a failed random source or signature.
 */
crux_tls_certificate_result_t
crux_tls_certificate_regenerate(crux_access_session_t session,
                                const crux_tls_certificate_names_t *names,
                                crux_tls_certificate_info_t *out);

/**
 * Compare a certificate's dates with a clock. Touches no state and needs no session.
 * @param clock_set false when the device has no trusted time: the result is then UNKNOWN.
 * @param now_s Unix seconds, UTC.
 */
crux_tls_certificate_dates_t crux_tls_certificate_dates_at(const crux_tls_certificate_info_t *info,
                                                           bool clock_set, int64_t now_s);

#ifdef __cplusplus
}
#endif

#endif
