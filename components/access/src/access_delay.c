#include "access_internal.h"

#include <string.h>

static access_bucket_t *bucket_from_hash(const uint8_t *data, size_t length,
                                         access_bucket_t *buckets, size_t count)
{
    uint8_t hash[CRUX_ACCESS_HASH_SIZE];
    if (!crux_access_port_hmac(crux_access_state.hash_key, data, length, hash)) {
        return NULL;
    }
    uint32_t value = ((uint32_t)hash[0] << 24) | ((uint32_t)hash[1] << 16) |
                     ((uint32_t)hash[2] << 8) | (uint32_t)hash[3];
    return &buckets[value % count];
}

bool crux_access_delay_buckets(const crux_access_origin_t *origin, const char *user,
                               access_delay_buckets_t *buckets)
{
    access_state_t *state = &crux_access_state;
    buckets->source = NULL;
    buckets->name = NULL;
    if (origin->channel == CRUX_ACCESS_CHANNEL_SERIAL) {
        buckets->source = &state->serial_bucket;
        return true;
    }
    uint8_t source[2u + sizeof(origin->source)];
    source[0] = (uint8_t)origin->channel;
    source[1] = origin->source_length;
    memcpy(&source[2], origin->source, origin->source_length);
    buckets->source = bucket_from_hash(source, 2u + (size_t)origin->source_length,
                                       state->source_buckets, CRUX_ACCESS_SOURCE_BUCKETS);
    if (buckets->source == NULL) {
        return false;
    }
    if (user != NULL) {
        buckets->name = bucket_from_hash((const uint8_t *)user, strlen(user), state->name_buckets,
                                         CRUX_ACCESS_NAME_BUCKETS);
        if (buckets->name == NULL) {
            return false;
        }
    }
    return true;
}

/* A bucket with no failure for the quiet period returns to zero, restoring its free attempts. A
 * clock that ran backwards keeps the state, because clearing it would help an attacker. */
static void bucket_decay(access_bucket_t *bucket, uint64_t now_ms)
{
    if (bucket->failures != 0 && now_ms >= bucket->last_failure_ms &&
        now_ms - bucket->last_failure_ms >= CRUX_ACCESS_DELAY_QUIET_MS) {
        memset(bucket, 0, sizeof(*bucket));
    }
}

static uint32_t bucket_remaining_ms(access_bucket_t *bucket, uint64_t now_ms)
{
    if (bucket == NULL) {
        return 0;
    }
    bucket_decay(bucket, now_ms);
    if (bucket->window_end_ms <= now_ms) {
        return 0;
    }
    uint64_t remaining_ms = bucket->window_end_ms - now_ms;
    return remaining_ms > CRUX_ACCESS_DELAY_CAP_MS ? CRUX_ACCESS_DELAY_CAP_MS
                                                   : (uint32_t)remaining_ms;
}

uint32_t crux_access_delay_remaining_ms(access_delay_buckets_t buckets, uint64_t now_ms)
{
    uint32_t source_ms = bucket_remaining_ms(buckets.source, now_ms);
    uint32_t name_ms = bucket_remaining_ms(buckets.name, now_ms);
    return source_ms > name_ms ? source_ms : name_ms;
}

static uint64_t window_for_failures(uint32_t failures)
{
    uint32_t doublings = failures - CRUX_ACCESS_DELAY_FREE_ATTEMPTS - 1u;
    uint64_t window_ms = CRUX_ACCESS_DELAY_BASE_MS;
    while (doublings > 0 && window_ms < CRUX_ACCESS_DELAY_CAP_MS) {
        window_ms *= 2u;
        doublings -= 1u;
    }
    return window_ms > CRUX_ACCESS_DELAY_CAP_MS ? CRUX_ACCESS_DELAY_CAP_MS : window_ms;
}

static void bucket_record_failure(access_bucket_t *bucket, uint64_t now_ms)
{
    if (bucket == NULL) {
        return;
    }
    bucket_decay(bucket, now_ms);
    if (bucket->failures < UINT32_MAX) {
        bucket->failures += 1;
    }
    bucket->last_failure_ms = now_ms;
    if (bucket->failures > CRUX_ACCESS_DELAY_FREE_ATTEMPTS) {
        uint64_t window_ms = window_for_failures(bucket->failures);
        bucket->window_end_ms = now_ms > UINT64_MAX - window_ms ? UINT64_MAX : now_ms + window_ms;
    }
}

void crux_access_delay_record_failure(access_delay_buckets_t buckets, uint64_t now_ms)
{
    bucket_record_failure(buckets.source, now_ms);
    bucket_record_failure(buckets.name, now_ms);
}
