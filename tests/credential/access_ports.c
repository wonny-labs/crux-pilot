/*
 * The Access ports other than its store, for the suite that runs Access on the real store. TEST
 * ONLY: nothing here derives or hashes for real, and the clock is a constant that moves when asked.
 */
#include "access_port.h"

#include <string.h>

static uint32_t random_state = 0x178u;
static uint64_t clock_ms = 1000u;

static void fake_mix(const uint8_t *data, size_t length, uint8_t output[32], uint64_t *state)
{
    for (size_t index = 0; index < length; ++index) {
        *state = (*state ^ data[index]) * 0x100000001B3ull;
    }
    for (size_t index = 0; index < 32u; ++index) {
        *state = (*state ^ (uint64_t)index) * 0x100000001B3ull;
        output[index] = (uint8_t)(*state >> 40);
    }
}

bool crux_access_port_derive(uint32_t algorithm, uint32_t iterations,
                             const uint8_t salt[CRUX_ACCESS_SALT_SIZE], const char *password,
                             size_t length, uint8_t derived[CRUX_ACCESS_DERIVED_SIZE])
{
    uint64_t state = 0x84222325CBF29CE4ull ^ ((uint64_t)algorithm << 32) ^ iterations;
    uint8_t ignored[32];
    fake_mix(salt, CRUX_ACCESS_SALT_SIZE, ignored, &state);
    fake_mix((const uint8_t *)password, length, derived, &state);
    return true;
}

bool crux_access_port_random(uint8_t *output, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        random_state = random_state * 1664525u + 1013904223u;
        output[index] = (uint8_t)(random_state >> 24);
    }
    return true;
}

bool crux_access_port_hmac(const uint8_t key[CRUX_ACCESS_HASH_KEY_SIZE], const uint8_t *data,
                           size_t length, uint8_t hash[CRUX_ACCESS_HASH_SIZE])
{
    uint64_t state = 0xCBF29CE484222325ull;
    uint8_t ignored[32];
    fake_mix(key, CRUX_ACCESS_HASH_KEY_SIZE, ignored, &state);
    fake_mix(data, length, hash, &state);
    return true;
}

uint64_t crux_access_port_now_ms(void)
{
    /* Each read moves on, so that delay windows pass without any test waiting for them. */
    clock_ms += 120000u;
    return clock_ms;
}

void crux_access_port_event(const crux_access_event_t *event)
{
    (void)event;
}
