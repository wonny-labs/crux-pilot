#include "tls_test.h"

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

unsigned tls_test_checks, tls_test_failures;
static unsigned cases_run, cases_failed;

void tls_test_case(const char *name, void (*body)(void))
{
    fflush(NULL);
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        exit(2);
    }
    if (child == 0) {
        body();
        fflush(NULL);
        _exit(tls_test_failures == 0 ? 0 : 1);
    }
    int status = 0;
    waitpid(child, &status, 0);
    ++cases_run;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("CASE FAILED: %s (status %d)\n", name, status);
        ++cases_failed;
    }
}

int tls_test_finish(const char *suite)
{
    printf("%s: %u cases, %u failed\n", suite, cases_run, cases_failed);
    return cases_failed == 0 ? 0 : 1;
}

static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t rotr(uint32_t value, unsigned count)
{
    return (value >> count) | (value << (32u - count));
}

static void block(uint32_t state[8], const uint8_t data[64])
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16u; ++i) {
        w[i] = (uint32_t)data[i * 4u] << 24 | (uint32_t)data[i * 4u + 1u] << 16 |
               (uint32_t)data[i * 4u + 2u] << 8 | data[i * 4u + 3u];
    }
    for (unsigned i = 16; i < 64u; ++i) {
        uint32_t s0 = rotr(w[i - 15u], 7) ^ rotr(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        uint32_t s1 = rotr(w[i - 2u], 17) ^ rotr(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5],
             g = state[6], h = state[7];
    for (unsigned i = 0; i < 64u; ++i) {
        uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] +
                      w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void tls_test_sha256(const uint8_t *data, size_t length, uint8_t digest[32])
{
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    size_t whole = length - length % 64u;
    for (size_t offset = 0; offset < whole; offset += 64u) {
        block(state, data + offset);
    }
    uint8_t tail[128] = {0};
    size_t rest = length - whole;
    if (rest > 0u) {
        memcpy(tail, data + whole, rest);
    }
    tail[rest] = 0x80;
    size_t tail_length = rest + 1u + 8u <= 64u ? 64u : 128u;
    uint64_t bits = (uint64_t)length * 8u;
    for (unsigned i = 0; i < 8u; ++i) {
        tail[tail_length - 1u - i] = (uint8_t)(bits >> (8u * i));
    }
    block(state, tail);
    if (tail_length == 128u) {
        block(state, tail + 64);
    }
    for (unsigned i = 0; i < 8u; ++i) {
        digest[i * 4u] = (uint8_t)(state[i] >> 24);
        digest[i * 4u + 1u] = (uint8_t)(state[i] >> 16);
        digest[i * 4u + 2u] = (uint8_t)(state[i] >> 8);
        digest[i * 4u + 3u] = (uint8_t)state[i];
    }
}

bool tls_test_hash(const uint8_t *data, size_t length, uint8_t digest[32])
{
    tls_test_sha256(data, length, digest);
    return true;
}

bool tls_test_sign(void *context, const uint8_t digest[32], uint8_t signature[64])
{
    tls_test_signer_t *signer = context;
    if (signer != NULL) {
        signer->calls += 1;
        memcpy(signer->last_digest, digest, 32);
        if (signer->fail) {
            return false;
        }
        if (signer->fixed) {
            memcpy(signature, signer->r, 32);
            memcpy(signature + 32, signer->s, 32);
            return true;
        }
    }
    tls_test_sha256(digest, 32, signature);
    tls_test_sha256(signature, 32, signature + 32);
    return true;
}

bool tls_test_contains(const uint8_t *haystack, size_t length, const uint8_t *needle,
                       size_t needle_length)
{
    if (needle_length == 0u || needle_length > length) {
        return false;
    }
    for (size_t i = 0; i + needle_length <= length; ++i) {
        if (memcmp(haystack + i, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}
