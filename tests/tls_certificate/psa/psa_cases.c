/*
 * The PSA ports of the TLS certificate module on TF-PSA-Crypto, and what the DER builders make from
 * a real P-256 key: signatures that verify, and a PKCS #8 key that the Mbed TLS profile's own key
 * parser takes. With a directory argument it also writes the certificate, the request and the key
 * there, for OpenSSL. The key is a fresh test key from the Host's entropy, and protects nothing.
 */
#include "tls_certificate_port.h"
#include "tls_x509.h"

#include <mbedtls/pk.h>
#include <psa/crypto.h>

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

void crux_tls_certificate_port_event(const crux_tls_certificate_event_t *event)
{
    (void)event;
}

static uint8_t scalar[32];
static uint8_t point[65];

static bool sign(void *context, const uint8_t digest[32], uint8_t signature[64])
{
    (void)context;
    return crux_tls_certificate_port_sign(digest, signature, scalar);
}

static void generate_key(void)
{
    psa_key_attributes_t attributes = psa_key_attributes_init();
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t key = PSA_KEY_ID_NULL;
    size_t length = 0;
    CHECK(psa_generate_key(&attributes, &key) == PSA_SUCCESS);
    CHECK(psa_export_key(key, scalar, sizeof(scalar), &length) == PSA_SUCCESS && length == 32u);
    CHECK(psa_export_public_key(key, point, sizeof(point), &length) == PSA_SUCCESS &&
          length == 65u);
    (void)psa_destroy_key(key);
}

/* The first element inside the outer SEQUENCE: what was signed. */
static size_t signed_part(const uint8_t *der, const uint8_t **part)
{
    size_t at = der[1] == 0x82 ? 4u : der[1] == 0x81 ? 3u : 2u;
    size_t header = der[at + 1u] == 0x82 ? 4u : der[at + 1u] == 0x81 ? 3u : 2u;
    size_t length = header == 4u   ? ((size_t)der[at + 2u] << 8 | der[at + 3u])
                    : header == 3u ? der[at + 2u]
                                   : der[at + 1u];
    *part = der + at;
    return header + length;
}

/* The DER Ecdsa-Sig-Value inside the closing BIT STRING. */
static size_t signature_of(const uint8_t *der, size_t length, const uint8_t **signature)
{
    for (size_t i = length - 3u; i > 0u; --i) {
        if (der[i] == 0x03 && der[i + 2u] == 0x00 && der[i + 3u] == 0x30 &&
            i + 2u + der[i + 1u] == length) {
            *signature = der + i + 3u;
            return der[i + 1u] - 1u;
        }
    }
    return 0;
}

static void verify_with_key(mbedtls_pk_context *pk, const uint8_t *der, size_t length)
{
    const uint8_t *part = NULL;
    size_t part_length = signed_part(der, &part);
    uint8_t digest[32];
    CHECK(crux_tls_certificate_port_sha256(part, part_length, digest));
    const uint8_t *signature = NULL;
    size_t signature_length = signature_of(der, length, &signature);
    CHECK(signature_length > 0u);
    CHECK(mbedtls_pk_verify(pk, MBEDTLS_MD_SHA256, digest, 32, signature, signature_length) == 0);
    /* A changed digest does not verify. */
    digest[0] ^= 1u;
    CHECK(mbedtls_pk_verify(pk, MBEDTLS_MD_SHA256, digest, 32, signature, signature_length) != 0);
}

static void write_file(const char *directory, const char *name, const uint8_t *data, size_t length)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fwrite(data, 1, length, file) == length);
        fclose(file);
    }
}

int main(int argc, char **argv)
{
    CHECK(psa_crypto_init() == PSA_SUCCESS);

    /* SHA-256 known answers, FIPS 180-4. */
    static const uint8_t abc_digest[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
                                           0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                           0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
                                           0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    uint8_t digest[32];
    CHECK(crux_tls_certificate_port_sha256((const uint8_t *)"abc", 3, digest));
    CHECK(memcmp(digest, abc_digest, 32) == 0);

    /* Random bytes differ between draws. */
    uint8_t first[16], second[16];
    CHECK(crux_tls_certificate_port_random(first, sizeof(first)));
    CHECK(crux_tls_certificate_port_random(second, sizeof(second)));
    CHECK(memcmp(first, second, sizeof(first)) != 0);

    /* A scalar of zero is not a key: the signature fails and its output is cleared. */
    static const uint8_t zero[32] = {0};
    uint8_t signature[64];
    memset(signature, 0xaa, sizeof(signature));
    CHECK(!crux_tls_certificate_port_sign(abc_digest, signature, zero));
    static const uint8_t cleared[64] = {0};
    CHECK(memcmp(signature, cleared, 64) == 0);

    generate_key();
    static const char *const dns[] = {"localhost"};
    static const uint8_t ipv4[][4] = {{127, 0, 0, 1}};
    static const crux_tls_certificate_names_t names = {
        "Crux test device", dns, 1, ipv4, 1, NULL, 0};
    tls_x509_self_signed_t input = {&names, {0}, 1767225600, 0};
    CHECK(crux_tls_certificate_port_random(input.serial, sizeof(input.serial)));

    static uint8_t certificate[CRUX_TLS_CERTIFICATE_DER_MAX];
    size_t certificate_length = 0;
    CHECK(tls_x509_build_self_signed(&input, point, crux_tls_certificate_port_sha256, sign, NULL,
                                     certificate, sizeof(certificate),
                                     &certificate_length) == TLS_X509_BUILT);
    tls_x509_view_t view;
    CHECK(tls_x509_parse(certificate, certificate_length, &view) == TLS_X509_OK);
    CHECK(memcmp(view.public_key, point, 65) == 0);

    static uint8_t request[CRUX_TLS_CERTIFICATE_CSR_MAX + 64];
    size_t request_length = 0;
    CHECK(tls_x509_build_request(&names, point, crux_tls_certificate_port_sha256, sign, NULL,
                                 request, sizeof(request), &request_length) == TLS_X509_BUILT);

    uint8_t key[138];
    size_t key_length = 0;
    CHECK(tls_x509_build_pkcs8(scalar, point, key, sizeof(key), &key_length) == TLS_X509_BUILT);

    /* The Mbed TLS profile parses TLS_CREDENTIAL_PRIVATE_KEY with this function. */
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    CHECK(mbedtls_pk_parse_key(&pk, key, key_length, NULL, 0) == 0);
    verify_with_key(&pk, certificate, certificate_length);
    verify_with_key(&pk, request, request_length);
    mbedtls_pk_free(&pk);

    char text[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    CHECK(crux_tls_certificate_port_sha256(certificate, certificate_length, digest));
    tls_x509_fingerprint_text(digest, text);
    printf("fingerprint %s\n", text);

    if (argc > 1) {
        write_file(argv[1], "certificate.der", certificate, certificate_length);
        write_file(argv[1], "request.der", request, request_length);
        write_file(argv[1], "key.der", key, key_length);
    }
    memset(scalar, 0, sizeof(scalar));
    memset(key, 0, sizeof(key));
    printf("tls_certificate_psa_cases: %u checks, %u failures, certificate %zu bytes, request %zu "
           "bytes\n",
           checks, failures, certificate_length, request_length);
    return failures == 0u ? 0 : 1;
}
