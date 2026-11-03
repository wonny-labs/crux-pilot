#ifndef CRUX_ACCESS_TEST_COMPOSITION
#error "The fake Access ports are a test composition and must not be selected for a product image"
#endif

#include "fake_ports.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define ACCESS_FAKE_EVENTS_MAX 8192u

static pthread_mutex_t fake_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t clock_ms;
static uint32_t random_counter;
static bool random_fails, hmac_fails, hmac_is_fixed, derive_fails;
static access_fake_hook_t derive_hook, save_hook, event_hook;
static void *derive_context, *save_context, *event_context;
static bool derive_is_forced;
static uint8_t derive_forced[CRUX_ACCESS_DERIVED_SIZE];
static unsigned derive_count, save_count;
static uint64_t derive_iterations;
static crux_access_store_state_t store_state;
static uint8_t store_record[CRUX_ACCESS_RECORD_SIZE_MAX];
static size_t store_length;
static access_fake_save_mode_t save_mode;
static crux_access_event_t events[ACCESS_FAKE_EVENTS_MAX];
static size_t event_count;

void access_fake_reset(void)
{
    pthread_mutex_lock(&fake_lock);
    clock_ms = 1000;
    random_counter = 0;
    random_fails = hmac_fails = hmac_is_fixed = derive_fails = false;
    derive_hook = save_hook = event_hook = NULL;
    derive_context = save_context = event_context = NULL;
    derive_is_forced = false;
    derive_count = save_count = 0;
    derive_iterations = 0;
    store_state = CRUX_ACCESS_STORE_EMPTY;
    store_length = 0;
    save_mode = ACCESS_FAKE_SAVE_OK;
    event_count = 0;
    pthread_mutex_unlock(&fake_lock);
}

void access_fake_clock_set(uint64_t now_ms)
{
    pthread_mutex_lock(&fake_lock);
    clock_ms = now_ms;
    pthread_mutex_unlock(&fake_lock);
}

void access_fake_clock_advance(uint64_t delta_ms)
{
    pthread_mutex_lock(&fake_lock);
    clock_ms += delta_ms;
    pthread_mutex_unlock(&fake_lock);
}

uint64_t crux_access_port_now_ms(void)
{
    pthread_mutex_lock(&fake_lock);
    uint64_t now_ms = clock_ms;
    pthread_mutex_unlock(&fake_lock);
    return now_ms;
}

void access_fake_random_fail(bool fail)
{
    pthread_mutex_lock(&fake_lock);
    random_fails = fail;
    pthread_mutex_unlock(&fake_lock);
}

bool crux_access_port_random(uint8_t *output, size_t length)
{
    pthread_mutex_lock(&fake_lock);
    bool fails = random_fails;
    if (!fails) {
        for (size_t index = 0; index < length; ++index) {
            random_counter = random_counter * 1664525u + 1013904223u;
            output[index] = (uint8_t)(random_counter >> 24);
        }
    }
    pthread_mutex_unlock(&fake_lock);
    return !fails;
}

/* Deliberately not a real hash. It needs only to be deterministic and to spread its input. */
static void fake_mix(const uint8_t *data, size_t length, uint8_t output[32], uint64_t *state)
{
    for (size_t index = 0; index < length; ++index) {
        *state = (*state ^ data[index]) * 0x100000001B3ull;
    }
    for (size_t index = 0; index < 32; ++index) {
        *state = (*state ^ (uint64_t)index) * 0x100000001B3ull;
        output[index] = (uint8_t)(*state >> 40);
    }
}

void access_fake_hmac_fail(bool fail)
{
    pthread_mutex_lock(&fake_lock);
    hmac_fails = fail;
    pthread_mutex_unlock(&fake_lock);
}

void access_fake_hmac_fixed(bool fixed)
{
    pthread_mutex_lock(&fake_lock);
    hmac_is_fixed = fixed;
    pthread_mutex_unlock(&fake_lock);
}

bool crux_access_port_hmac(const uint8_t key[CRUX_ACCESS_HASH_KEY_SIZE], const uint8_t *data,
                           size_t length, uint8_t hash[CRUX_ACCESS_HASH_SIZE])
{
    pthread_mutex_lock(&fake_lock);
    bool fails = hmac_fails;
    bool fixed = hmac_is_fixed;
    pthread_mutex_unlock(&fake_lock);
    if (fails) {
        return false;
    }
    if (fixed) {
        memset(hash, 0, CRUX_ACCESS_HASH_SIZE);
        return true;
    }
    uint64_t state = 0xCBF29CE484222325ull;
    uint8_t ignored[32];
    fake_mix(key, CRUX_ACCESS_HASH_KEY_SIZE, ignored, &state);
    fake_mix(data, length, hash, &state);
    return true;
}

void access_fake_derive_fail(bool fail)
{
    pthread_mutex_lock(&fake_lock);
    derive_fails = fail;
    pthread_mutex_unlock(&fake_lock);
}

void access_fake_derive_hook(access_fake_hook_t hook, void *context)
{
    pthread_mutex_lock(&fake_lock);
    derive_hook = hook;
    derive_context = context;
    pthread_mutex_unlock(&fake_lock);
}

void access_fake_derive_force(const uint8_t *derived)
{
    pthread_mutex_lock(&fake_lock);
    derive_is_forced = derived != NULL;
    if (derived != NULL) {
        memcpy(derive_forced, derived, sizeof(derive_forced));
    }
    pthread_mutex_unlock(&fake_lock);
}

unsigned access_fake_derive_count(void)
{
    pthread_mutex_lock(&fake_lock);
    unsigned count = derive_count;
    pthread_mutex_unlock(&fake_lock);
    return count;
}

uint64_t access_fake_derive_iterations(void)
{
    pthread_mutex_lock(&fake_lock);
    uint64_t iterations = derive_iterations;
    pthread_mutex_unlock(&fake_lock);
    return iterations;
}

void access_fake_derive_counters_clear(void)
{
    pthread_mutex_lock(&fake_lock);
    derive_count = 0;
    derive_iterations = 0;
    pthread_mutex_unlock(&fake_lock);
}

bool crux_access_port_derive(uint32_t algorithm, uint32_t iterations,
                             const uint8_t salt[CRUX_ACCESS_SALT_SIZE], const char *password,
                             size_t length, uint8_t derived[CRUX_ACCESS_DERIVED_SIZE])
{
    pthread_mutex_lock(&fake_lock);
    bool fails = derive_fails;
    access_fake_hook_t hook = derive_hook;
    void *context = derive_context;
    derive_hook = NULL;
    derive_context = NULL;
    derive_count += 1;
    derive_iterations += iterations;
    pthread_mutex_unlock(&fake_lock);
    if (hook != NULL) {
        hook(context);
    }
    if (fails) {
        return false;
    }
    pthread_mutex_lock(&fake_lock);
    bool forced = derive_is_forced;
    if (forced) {
        memcpy(derived, derive_forced, CRUX_ACCESS_DERIVED_SIZE);
    }
    pthread_mutex_unlock(&fake_lock);
    if (forced) {
        return true;
    }
    uint64_t state = 0x84222325CBF29CE4ull ^ ((uint64_t)algorithm << 32) ^ iterations;
    uint8_t ignored[32];
    fake_mix(salt, CRUX_ACCESS_SALT_SIZE, ignored, &state);
    fake_mix((const uint8_t *)password, length, derived, &state);
    return true;
}

void access_fake_store_state(crux_access_store_state_t state)
{
    pthread_mutex_lock(&fake_lock);
    store_state = state;
    pthread_mutex_unlock(&fake_lock);
}

void access_fake_store_set_record(const uint8_t *record, size_t length)
{
    if (length > sizeof(store_record)) {
        abort();
    }
    pthread_mutex_lock(&fake_lock);
    memcpy(store_record, record, length);
    store_length = length;
    store_state = CRUX_ACCESS_STORE_VALID;
    pthread_mutex_unlock(&fake_lock);
}

const uint8_t *access_fake_store_record(size_t *length)
{
    pthread_mutex_lock(&fake_lock);
    *length = store_length;
    pthread_mutex_unlock(&fake_lock);
    return store_record;
}

void access_fake_store_save_mode(access_fake_save_mode_t mode)
{
    pthread_mutex_lock(&fake_lock);
    save_mode = mode;
    pthread_mutex_unlock(&fake_lock);
}

void access_fake_store_save_hook(access_fake_hook_t hook, void *context)
{
    pthread_mutex_lock(&fake_lock);
    save_hook = hook;
    save_context = context;
    pthread_mutex_unlock(&fake_lock);
}

unsigned access_fake_store_saves(void)
{
    pthread_mutex_lock(&fake_lock);
    unsigned saves = save_count;
    pthread_mutex_unlock(&fake_lock);
    return saves;
}

crux_access_store_state_t crux_access_port_store_load(uint8_t *record, size_t capacity,
                                                      size_t *length)
{
    pthread_mutex_lock(&fake_lock);
    crux_access_store_state_t state = store_state;
    if (state == CRUX_ACCESS_STORE_VALID) {
        if (store_length > capacity) {
            state = CRUX_ACCESS_STORE_UNUSABLE;
        } else {
            memcpy(record, store_record, store_length);
            *length = store_length;
        }
    }
    pthread_mutex_unlock(&fake_lock);
    return state;
}

bool crux_access_port_store_save(const uint8_t *record, size_t length)
{
    pthread_mutex_lock(&fake_lock);
    access_fake_hook_t hook = save_hook;
    void *context = save_context;
    save_hook = NULL;
    save_context = NULL;
    pthread_mutex_unlock(&fake_lock);
    if (hook != NULL) {
        hook(context);
    }
    if (length > sizeof(store_record)) {
        abort();
    }
    pthread_mutex_lock(&fake_lock);
    save_count += 1;
    access_fake_save_mode_t mode = save_mode;
    if (mode == ACCESS_FAKE_SAVE_OK || mode == ACCESS_FAKE_SAVE_POWER_LOSS_AFTER) {
        memcpy(store_record, record, length);
        store_length = length;
        store_state = CRUX_ACCESS_STORE_VALID;
    }
    pthread_mutex_unlock(&fake_lock);
    return mode == ACCESS_FAKE_SAVE_OK;
}

void access_fake_event_hook(access_fake_hook_t hook, void *context)
{
    pthread_mutex_lock(&fake_lock);
    event_hook = hook;
    event_context = context;
    pthread_mutex_unlock(&fake_lock);
}

void crux_access_port_event(const crux_access_event_t *event)
{
    pthread_mutex_lock(&fake_lock);
    access_fake_hook_t hook = event_hook;
    void *context = event_context;
    pthread_mutex_unlock(&fake_lock);
    if (hook != NULL) {
        hook(context);
    }
    pthread_mutex_lock(&fake_lock);
    if (event_count < ACCESS_FAKE_EVENTS_MAX) {
        events[event_count] = *event;
        event_count += 1;
    }
    pthread_mutex_unlock(&fake_lock);
}

size_t access_fake_event_count(void)
{
    pthread_mutex_lock(&fake_lock);
    size_t count = event_count;
    pthread_mutex_unlock(&fake_lock);
    return count;
}

const crux_access_event_t *access_fake_event(size_t index)
{
    return index < access_fake_event_count() ? &events[index] : NULL;
}

size_t access_fake_event_count_of(crux_access_event_type_t type)
{
    size_t count = 0;
    pthread_mutex_lock(&fake_lock);
    for (size_t index = 0; index < event_count; ++index) {
        count += events[index].type == type ? 1u : 0u;
    }
    pthread_mutex_unlock(&fake_lock);
    return count;
}

void access_fake_events_clear(void)
{
    pthread_mutex_lock(&fake_lock);
    event_count = 0;
    pthread_mutex_unlock(&fake_lock);
}
