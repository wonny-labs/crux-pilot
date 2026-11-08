/*
 * Differential test of the parser against OpenSSL's DER decoder: seeded mutations of the OpenSSL
 * fixtures, and every mutant the parser accepts must also decode with d2i_X509. The other way is
 * not required: the parser may refuse more. Built only when the Host has OpenSSL's headers.
 *
 *   tls_certificate_differential [rounds]
 */
#include "fixtures.h"
#include "tls_x509.h"

#include <openssl/x509.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t state = 0x2545f4914f6cdd1du;

static uint64_t next_random(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static bool openssl_decodes(const uint8_t *der, size_t length)
{
    const unsigned char *cursor = der;
    X509 *certificate = d2i_X509(NULL, &cursor, (long)length);
    bool whole = certificate != NULL && cursor == der + length;
    X509_free(certificate);
    return whole;
}

int main(int argc, char **argv)
{
    unsigned long rounds = argc > 1 ? strtoul(argv[1], NULL, 10) : 300000ul;
    const uint8_t *seeds[] = {fixture_ec_self, fixture_ec_by_rsa, fixture_far, fixture_other};
    const size_t lengths[] = {sizeof(fixture_ec_self), sizeof(fixture_ec_by_rsa),
                              sizeof(fixture_far), sizeof(fixture_other)};
    for (size_t i = 0; i < 4u; ++i) {
        if (!openssl_decodes(seeds[i], lengths[i])) {
            printf("FAIL: OpenSSL refuses seed %zu\n", i);
            return 1;
        }
    }
    unsigned long accepted = 0, disagreements = 0;
    for (unsigned long round = 0; round < rounds; ++round) {
        size_t which = (size_t)(next_random() % 4u);
        size_t length = lengths[which];
        uint8_t work[1024];
        memcpy(work, seeds[which], length);
        unsigned edits = 1u + (unsigned)(next_random() % 3u);
        for (unsigned e = 0; e < edits; ++e) {
            size_t at = (size_t)(next_random() % length);
            switch (next_random() % 3u) {
            case 0:
                work[at] = (uint8_t)next_random();
                break;
            case 1:
                work[at] ^= (uint8_t)(1u << (next_random() % 8u));
                break;
            default:
                work[at] = (uint8_t)(0x80u | (next_random() % 4u));
                break;
            }
        }
        tls_x509_view_t view;
        if (tls_x509_parse(work, length, &view) != TLS_X509_OK) {
            continue;
        }
        accepted += 1u;
        if (!openssl_decodes(work, length)) {
            disagreements += 1u;
            if (disagreements <= 10u) {
                printf("FAIL: round %lu, seed %zu: accepted here, refused by OpenSSL\n", round,
                       which);
            }
        }
    }
    printf("differential: %lu rounds, %lu accepted, %lu refused by OpenSSL\n", rounds, accepted,
           disagreements);
    return disagreements == 0u ? 0 : 1;
}
