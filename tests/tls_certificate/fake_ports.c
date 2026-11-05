#include "fakes.h"
#include "tls_test.h"

#include <string.h>

#define EVENTS_MAX 64u

static bool hash_fails, sign_fails, random_fails;
static uint8_t last_key[32], last_digest[32];
static unsigned sign_count;
static uint32_t random_counter;
static crux_tls_certificate_event_t events[EVENTS_MAX];
static size_t event_count;
static void (*event_hook)(const crux_tls_certificate_event_t *event);

void fake_ports_reset(void)
{
    hash_fails = sign_fails = random_fails = false;
    memset(last_key, 0, sizeof(last_key));
    memset(last_digest, 0, sizeof(last_digest));
    sign_count = 0;
    random_counter = 0;
    event_count = 0;
    event_hook = NULL;
}

void fake_ports_hash_fail(bool fail)
{
    hash_fails = fail;
}

void fake_ports_sign_fail(bool fail)
{
    sign_fails = fail;
}

void fake_ports_random_fail(bool fail)
{
    random_fails = fail;
}

const uint8_t *fake_ports_last_sign_key(void)
{
    return last_key;
}

const uint8_t *fake_ports_last_sign_digest(void)
{
    return last_digest;
}

unsigned fake_ports_sign_count(void)
{
    return sign_count;
}

bool crux_tls_certificate_port_sha256(const uint8_t *data, size_t length, uint8_t digest[32])
{
    if (hash_fails) {
        return false;
    }
    tls_test_sha256(data, length, digest);
    return true;
}

bool crux_tls_certificate_port_sign(const uint8_t digest[32], uint8_t signature[64],
                                    const uint8_t private_key[32])
{
    if (sign_fails) {
        return false;
    }
    memcpy(last_key, private_key, 32);
    memcpy(last_digest, digest, 32);
    sign_count += 1u;
    return tls_test_sign(NULL, digest, signature);
}

bool crux_tls_certificate_port_random(uint8_t *buffer, size_t length)
{
    if (random_fails) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = (uint8_t)((size_t)random_counter * 31u + i);
    }
    random_counter += 1u;
    return true;
}

void crux_tls_certificate_port_event(const crux_tls_certificate_event_t *event)
{
    events[event_count % EVENTS_MAX] = *event;
    event_count += 1u;
    if (event_hook != NULL) {
        event_hook(event);
    }
}

size_t fake_events_count(void)
{
    return event_count;
}

const crux_tls_certificate_event_t *fake_event(size_t index)
{
    return index < event_count && index + EVENTS_MAX >= event_count ? &events[index % EVENTS_MAX]
                                                                    : NULL;
}

const crux_tls_certificate_event_t *fake_event_last(void)
{
    return event_count > 0u ? &events[(event_count - 1u) % EVENTS_MAX] : NULL;
}

void fake_events_hook(void (*hook)(const crux_tls_certificate_event_t *event))
{
    event_hook = hook;
}
