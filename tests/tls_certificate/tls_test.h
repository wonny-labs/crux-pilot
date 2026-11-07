/* Shared helpers for the TLS certificate suites. */
#ifndef TLS_CERTIFICATE_TEST_H
#define TLS_CERTIFICATE_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern unsigned tls_test_checks, tls_test_failures;

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++tls_test_checks;                                                                         \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            ++tls_test_failures;                                                                   \
        }                                                                                          \
    } while (0)

/* Runs a case in a forked child, so that each starts the module from nothing. */
void tls_test_case(const char *name, void (*body)(void));
/* The suite's exit status, with the totals printed. */
int tls_test_finish(const char *suite);

/* A plain SHA-256, written for these tests from FIPS 180-4, so that fingerprints are real. */
void tls_test_sha256(const uint8_t *data, size_t length, uint8_t digest[32]);
bool tls_test_hash(const uint8_t *data, size_t length, uint8_t digest[32]);

/*
 * Not ECDSA: r is SHA-256 of the digest and s is SHA-256 of r, so a signature is repeatable and
 * tied to what was signed. context may point to a tls_test_signer_t to choose or fail.
 */
typedef struct {
    bool fail;
    bool fixed; /* Use r and s below. */
    uint8_t r[32];
    uint8_t s[32];
    unsigned calls;
    uint8_t last_digest[32];
} tls_test_signer_t;
bool tls_test_sign(void *context, const uint8_t digest[32], uint8_t signature[64]);

/* Finds needle in haystack. */
bool tls_test_contains(const uint8_t *haystack, size_t length, const uint8_t *needle,
                       size_t needle_length);

#endif
