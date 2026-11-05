/*
 * Bounded DER building and parsing of the few X.509 and PKCS structures the module needs. No
 * allocation, no recursion, and every read is checked against the bytes that remain. Touches no
 * state and no crypto: a signature comes from the caller's function.
 */
#ifndef CRUX_TLS_X509_H
#define CRUX_TLS_X509_H

#include <crux/tls_certificate.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TLS_X509_P256_POINT_BYTES 65u
#define TLS_X509_P256_SCALAR_BYTES 32u
#define TLS_X509_SERIAL_BYTES 16u
/* 9999-12-31T23:59:59Z, the latest date either time form holds. */
#define TLS_X509_TIME_MAX_S 253402300799

/* A parse's verdict. Each is a reason a certificate is refused, except OK. */
typedef enum {
    TLS_X509_OK = 0,
    /* Not strict DER, not the X.509 shape, bytes after it, a bad string, time or parsed value. */
    TLS_X509_MALFORMED,
    /* Well formed: not v3, not P-256, differing algorithms, reversed dates, a long serial, an
     * extension twice, too many, or an unknown critical one. */
    TLS_X509_INVALID,
} tls_x509_result_t;

/* What a parse found. Pointers point into the parsed bytes. */
typedef struct {
    const uint8_t *public_key; /* The 65-byte uncompressed P-256 point. */
    int64_t not_before_s;
    int64_t not_after_s;
    bool no_expiry; /* notAfter is 99991231235959Z. */
    char subject[CRUX_TLS_CERTIFICATE_NAME_SIZE];
    char issuer[CRUX_TLS_CERTIFICATE_NAME_SIZE];
} tls_x509_view_t;

/*
 * Check that der is exactly one DER X.509 v3 certificate with a P-256 public key. Neither the
 * signature nor any date against a clock is checked. out is written only on TLS_X509_OK.
 */
tls_x509_result_t tls_x509_parse(const uint8_t *der, size_t length, tls_x509_view_t *out);

/* True if the names follow the rules in crux/tls_certificate.h. */
bool tls_x509_names_valid(const crux_tls_certificate_names_t *names);

/* Signs a SHA-256 digest: r then s. Returns false if signing failed. */
typedef bool (*tls_x509_sign_t)(void *context, const uint8_t digest[32], uint8_t signature[64]);
/* Hashes data. Returns false if hashing failed. */
typedef bool (*tls_x509_hash_t)(const uint8_t *data, size_t length, uint8_t digest[32]);

/* How a build ended. */
typedef enum {
    TLS_X509_BUILT = 0,
    TLS_X509_NO_ROOM,     /* The output is too small. */
    TLS_X509_BAD_INPUT,   /* Names or dates outside the rules. */
    TLS_X509_NO_SIGNATURE /* The hash or the signature failed. */
} tls_x509_build_t;

/* What a self-signed certificate holds besides its key. */
typedef struct {
    const crux_tls_certificate_names_t *names;
    uint8_t serial[TLS_X509_SERIAL_BYTES]; /* Any bytes. The top bit is cleared when written. */
    int64_t not_before_s;
    uint32_t validity_days; /* 0 for notAfter 99991231235959Z. */
} tls_x509_self_signed_t;

/*
 * A self-signed X.509 v3 certificate, ecdsa-with-SHA256: subject and issuer CN, basic constraints
 * not a CA, key usage digital signature, extended key usage server authentication, and subject
 * alternative names when there are any.
 */
tls_x509_build_t tls_x509_build_self_signed(const tls_x509_self_signed_t *input,
                                            const uint8_t public_key[TLS_X509_P256_POINT_BYTES],
                                            tls_x509_hash_t hash, tls_x509_sign_t sign,
                                            void *context, uint8_t *out, size_t capacity,
                                            size_t *length);

/* A PKCS #10 request, with the alternative names in an extensionRequest attribute. */
tls_x509_build_t tls_x509_build_request(const crux_tls_certificate_names_t *names,
                                        const uint8_t public_key[TLS_X509_P256_POINT_BYTES],
                                        tls_x509_hash_t hash, tls_x509_sign_t sign, void *context,
                                        uint8_t *out, size_t capacity, size_t *length);

/* PKCS #8 PrivateKeyInfo holding an RFC 5915 ECPrivateKey with its public point. */
tls_x509_build_t tls_x509_build_pkcs8(const uint8_t private_key[TLS_X509_P256_SCALAR_BYTES],
                                      const uint8_t public_key[TLS_X509_P256_POINT_BYTES],
                                      uint8_t *out, size_t capacity, size_t *length);

/* A civil UTC date and time, from year 1 to 9999, to Unix seconds. False if it is not a date. */
bool tls_x509_time_from_civil(int year, int month, int day, int hour, int minute, int second,
                              int64_t *out);

/* Uppercase hexadecimal pairs joined by colons. */
void tls_x509_fingerprint_text(const uint8_t digest[32],
                               char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE]);

#endif
