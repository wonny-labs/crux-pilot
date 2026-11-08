/*
 * crux_access ports for this test image: TEST ONLY, and declared as such in
 * docs/ssh-wolf-provider.md. The store is RAM, so accounts last one run. Random numbers come from
 * native_sim's test generator. The derivation and the keyed hash are real wolfCrypt, because a
 * client on the host needs a real login path, and neither is the PSA provider that #242 supplies.
 * Events print, so that a test on the host can read them.
 */
#include "test_support.h"

#include "access_port.h"

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>

#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/pwdbased.h>

#include <string.h>

static uint8_t record_bytes[CRUX_ACCESS_RECORD_SIZE_MAX];
static size_t record_length;
static int32_t derive_delay_ms;
static unsigned derive_count;

void test_access_derive_delay_ms(int32_t delay_ms)
{
    derive_delay_ms = delay_ms;
}

unsigned test_access_derive_count(void)
{
    return derive_count;
}

bool crux_access_port_derive(uint32_t algorithm, uint32_t iterations,
                             const uint8_t salt[CRUX_ACCESS_SALT_SIZE], const char *password,
                             size_t length, uint8_t derived[CRUX_ACCESS_DERIVED_SIZE])
{
    ++derive_count;
    printk("ACCESS derive count=%u\n", derive_count);
    if (algorithm != CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256) {
        return false;
    }
    /* CPU work takes no simulated time, so a slow verifier on a board is modelled by a sleep. */
    if (derive_delay_ms > 0) {
        k_msleep(derive_delay_ms);
    }
    return wc_PBKDF2(derived, (const byte *)password, (int)length, salt, CRUX_ACCESS_SALT_SIZE,
                     (int)iterations, CRUX_ACCESS_DERIVED_SIZE, WC_SHA256) == 0;
}

bool crux_access_port_random(uint8_t *output, size_t length)
{
    sys_rand_get(output, length);
    return true;
}

bool crux_access_port_hmac(const uint8_t key[CRUX_ACCESS_HASH_KEY_SIZE], const uint8_t *data,
                           size_t length, uint8_t hash[CRUX_ACCESS_HASH_SIZE])
{
    Hmac hmac;
    int result = wc_HmacInit(&hmac, NULL, INVALID_DEVID);

    if (result == 0) {
        result = wc_HmacSetKey(&hmac, WC_SHA256, key, CRUX_ACCESS_HASH_KEY_SIZE);
    }
    if (result == 0) {
        result = wc_HmacUpdate(&hmac, data, (word32)length);
    }
    if (result == 0) {
        result = wc_HmacFinal(&hmac, hash);
    }
    wc_HmacFree(&hmac);
    return result == 0;
}

uint64_t crux_access_port_now_ms(void)
{
    return (uint64_t)k_uptime_get();
}

crux_access_store_state_t crux_access_port_store_load(uint8_t *record, size_t capacity,
                                                      size_t *length)
{
    if (record_length == 0u) {
        return CRUX_ACCESS_STORE_EMPTY;
    }
    if (record_length > capacity) {
        return CRUX_ACCESS_STORE_UNUSABLE;
    }
    memcpy(record, record_bytes, record_length);
    *length = record_length;
    return CRUX_ACCESS_STORE_VALID;
}

bool crux_access_port_store_save(const uint8_t *record, size_t length)
{
    if (length > sizeof(record_bytes)) {
        return false;
    }
    memcpy(record_bytes, record, length);
    record_length = length;
    return true;
}

static const char *event_name(crux_access_event_type_t type)
{
    switch (type) {
    case CRUX_ACCESS_EVENT_LOGIN_SUCCESS:
        return "LOGIN_SUCCESS";
    case CRUX_ACCESS_EVENT_LOGIN_FAILURE:
        return "LOGIN_FAILURE";
    case CRUX_ACCESS_EVENT_KEY_FAILURE:
        return "KEY_FAILURE";
    case CRUX_ACCESS_EVENT_DELAY_APPLIED:
        return "DELAY_APPLIED";
    case CRUX_ACCESS_EVENT_LOGOUT:
        return "LOGOUT";
    case CRUX_ACCESS_EVENT_LOGIN_BUSY:
        return "LOGIN_BUSY";
    case CRUX_ACCESS_EVENT_LOGIN_NO_SESSION:
        return "LOGIN_NO_SESSION";
    case CRUX_ACCESS_EVENT_LOGIN_REFUSED:
        return "LOGIN_REFUSED";
    default:
        return "OTHER";
    }
}

void crux_access_port_event(const crux_access_event_t *event)
{
    printk("ACCESS event type=%s result=%d channel=%d source_length=%u account=%s\n",
           event_name(event->type), (int)event->result, (int)event->origin.channel,
           (unsigned)event->origin.source_length, event->account);
}
