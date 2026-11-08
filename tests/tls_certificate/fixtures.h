/*
 * Certificates made with OpenSSL 3.0.13 for these tests. Only public parts are kept: no private key
 * of any of them is in the repository. fixture_device_key is the public point of the key that
 * fixture_ec_self and fixture_ec_by_rsa certify, and stands for the device key.
 */
#ifndef TLS_CERTIFICATE_FIXTURES_H
#define TLS_CERTIFICATE_FIXTURES_H

#include <stdint.h>

extern const uint8_t fixture_device_key[65];

/* Self-signed by the device key, v3, O and CN, two alternative names, 825 days. */
extern const uint8_t fixture_ec_self[488];

#define FIXTURE_EC_SELF_NOT_BEFORE 1793878805LL
#define FIXTURE_EC_SELF_NOT_AFTER 1865158805LL

/* The device key, signed by an RSA test CA with sha256WithRSAEncryption. */
extern const uint8_t fixture_ec_by_rsa[620];

#define FIXTURE_EC_BY_RSA_NOT_BEFORE 1793878805LL
#define FIXTURE_EC_BY_RSA_NOT_AFTER 1825414805LL

/* Another P-256 key's self-signed certificate. */
extern const uint8_t fixture_other[376];

#define FIXTURE_OTHER_NOT_BEFORE 1793878805LL
#define FIXTURE_OTHER_NOT_AFTER 1796470805LL

/* A P-384 key's self-signed certificate. */
extern const uint8_t fixture_p384[435];

#define FIXTURE_P384_NOT_BEFORE 1793878805LL
#define FIXTURE_P384_NOT_AFTER 1796470805LL

/* An RSA key's self-signed certificate. */
extern const uint8_t fixture_rsa_self[769];

#define FIXTURE_RSA_SELF_NOT_BEFORE 1793878805LL
#define FIXTURE_RSA_SELF_NOT_AFTER 1796470805LL

/* The device key, notAfter in 2067, so a GeneralizedTime. */
extern const uint8_t fixture_far[376];

#define FIXTURE_FAR_NOT_BEFORE 1793878805LL
#define FIXTURE_FAR_NOT_AFTER 3089878805LL

/* fixture_ec_self in PEM, which the device does not take. */
extern const uint8_t fixture_ec_self_pem[717];

#endif
