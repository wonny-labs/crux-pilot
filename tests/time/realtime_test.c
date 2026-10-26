#include "crux_time_test.h"
#include "time_port.h"
#include <crux/time_authority.h>

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static atomic_int failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local bool locked;
static bool thread_context = true;
static crux_status_t init_status, read_status, write_status, lock_status;
static unsigned init_calls, read_calls, write_calls;
static crux_realtime_t native_time;
static const crux_realtime_t first = {CRUX_TIME_MIN_UNIX_SECONDS, 123};
static const crux_realtime_t second = {CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE - 1, 999999999};

bool crux_time_port_is_thread(void)
{
    return thread_context;
}
crux_status_t crux_time_port_lock(void)
{
    if (lock_status != CRUX_OK)
        return lock_status;
    if (pthread_mutex_lock(&mutex) != 0)
        return CRUX_ERR_IO;
    locked = true;
    return CRUX_OK;
}
void crux_time_port_unlock(void)
{
    locked = false;
    (void)pthread_mutex_unlock(&mutex);
}
crux_status_t crux_time_port_init(void)
{
    CHECK(locked);
    ++init_calls;
    return init_status;
}
crux_status_t crux_time_port_read(crux_realtime_t *out)
{
    CHECK(locked);
    ++read_calls;
    *out = native_time;
    return read_status;
}
crux_status_t crux_time_port_write(const crux_realtime_t *value)
{
    CHECK(locked);
    ++write_calls;
    native_time = *value;
    sched_yield();
    return write_status;
}
static bool equal(crux_realtime_t a, crux_realtime_t b)
{
    return a.seconds == b.seconds && a.nanoseconds == b.nanoseconds;
}
static crux_realtime_snapshot_t snapshot(void)
{
    crux_realtime_snapshot_t s;
    memset(&s, 0x55, sizeof(s));
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_OK);
    return s;
}
static void expect_invalid(crux_time_source_t source)
{
    unsigned before = read_calls;
    crux_realtime_snapshot_t s = snapshot();
    CHECK(!s.is_time_valid && !s.is_actively_synchronized && s.last_source == source);
    CHECK(s.timestamp.seconds == 0 && s.timestamp.nanoseconds == 0 && read_calls == before);
    crux_realtime_t out = first;
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_NO_DATA && equal(out, first));
    CHECK(read_calls == before);
}
static void *writer(void *arg)
{
    bool external = arg != NULL;
    for (unsigned i = 0; i < 10000; ++i) {
        CHECK(crux_time_set_realtime(external ? &second : &first,
                                     external ? CRUX_TIME_SOURCE_EXTERNAL : CRUX_TIME_SOURCE_MANUAL,
                                     external) == CRUX_OK);
    }
    return NULL;
}
static void *reader(void *arg)
{
    (void)arg;
    for (unsigned i = 0; i < 10000; ++i) {
        crux_realtime_snapshot_t s = snapshot();
        CHECK(s.is_time_valid);
        CHECK((equal(s.timestamp, first) && s.last_source == CRUX_TIME_SOURCE_MANUAL &&
               !s.is_actively_synchronized) ||
              (equal(s.timestamp, second) && s.last_source == CRUX_TIME_SOURCE_EXTERNAL &&
               s.is_actively_synchronized));
    }
    return NULL;
}
int main(void)
{
    crux_realtime_t out = first;
    crux_realtime_snapshot_t s, sentinel;
    memset(&s, 0x55, sizeof(s));
    sentinel = s;
    CHECK(crux_time_get_realtime(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_time_get_realtime_snapshot(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_NOT_READY && equal(out, first));
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_ERR_NOT_READY &&
          memcmp(&s, &sentinel, sizeof(s)) == 0);
    CHECK(crux_time_set_realtime(NULL, CRUX_TIME_SOURCE_MANUAL, false) ==
          CRUX_ERR_INVALID_ARGUMENT);
    crux_realtime_t bad = {first.seconds, 1000000000};
    CHECK(crux_time_set_realtime(&bad, CRUX_TIME_SOURCE_MANUAL, false) ==
          CRUX_ERR_INVALID_ARGUMENT);
    bad = first;
    bad.seconds--;
    CHECK(crux_time_set_realtime(&bad, CRUX_TIME_SOURCE_MANUAL, false) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_time_set_realtime(&first, CRUX_TIME_SOURCE_MANUAL, false) == CRUX_ERR_NOT_READY);
    CHECK(crux_time_adopt_realtime() == CRUX_ERR_NOT_READY);
    CHECK(crux_time_notify_sync_state(false) == CRUX_ERR_NOT_READY);
    CHECK(crux_time_invalidate_realtime() == CRUX_ERR_NOT_READY);
    CHECK(init_calls == 0 && read_calls == 0 && write_calls == 0);

    init_status = CRUX_ERR_IO;
    CHECK(crux_time_init() == CRUX_ERR_IO);
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_NOT_READY);
    init_status = CRUX_OK;
    CHECK(crux_time_init() == CRUX_OK && init_calls == 2);
    expect_invalid(CRUX_TIME_SOURCE_NONE);
    CHECK(crux_time_notify_sync_state(false) == CRUX_ERR_NO_DATA);
    native_time = first;
    CHECK(crux_time_adopt_realtime() == CRUX_OK && write_calls == 0);
    s = snapshot();
    CHECK(equal(s.timestamp, first) && s.is_time_valid && !s.is_actively_synchronized &&
          s.last_source == CRUX_TIME_SOURCE_UNKNOWN);
    CHECK(crux_time_notify_sync_state(true) == CRUX_ERR_INVALID_STATE);
    CHECK(crux_time_notify_sync_state(false) == CRUX_OK && write_calls == 0);

    const crux_time_source_t sources[] = {CRUX_TIME_SOURCE_RTC, CRUX_TIME_SOURCE_MANUAL,
                                          CRUX_TIME_SOURCE_EXTERNAL};
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(crux_time_set_realtime(&second, sources[i], false) == CRUX_OK);
        s = snapshot();
        CHECK(equal(s.timestamp, second) && s.last_source == sources[i]);
        CHECK(s.is_time_valid && !s.is_actively_synchronized);
        CHECK(crux_time_init() == CRUX_OK && init_calls == 2);
        CHECK(snapshot().last_source == sources[i]);
    }
    CHECK(crux_time_notify_sync_state(true) == CRUX_OK);
    CHECK(snapshot().is_actively_synchronized);
    CHECK(crux_time_notify_sync_state(false) == CRUX_OK);
    CHECK(!snapshot().is_actively_synchronized);
    CHECK(crux_time_set_realtime(&second, CRUX_TIME_SOURCE_EXTERNAL, true) == CRUX_OK);
    unsigned before = write_calls;
    const crux_time_source_t invalid_sources[] = {CRUX_TIME_SOURCE_NONE, CRUX_TIME_SOURCE_UNKNOWN,
                                                  (crux_time_source_t)99};
    for (unsigned i = 0; i < 3; ++i)
        CHECK(crux_time_set_realtime(&first, invalid_sources[i], false) ==
              CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_time_set_realtime(&first, CRUX_TIME_SOURCE_MANUAL, true) ==
          CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_time_set_realtime(&first, CRUX_TIME_SOURCE_RTC, true) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(write_calls == before && snapshot().is_actively_synchronized);
    CHECK(crux_time_invalidate_realtime() == CRUX_OK && write_calls == before);
    expect_invalid(CRUX_TIME_SOURCE_EXTERNAL);

    CHECK(crux_time_set_realtime(&first, CRUX_TIME_SOURCE_MANUAL, false) == CRUX_OK);
    write_status = CRUX_ERR_IO;
    CHECK(crux_time_set_realtime(&second, CRUX_TIME_SOURCE_EXTERNAL, true) == CRUX_ERR_IO);
    expect_invalid(CRUX_TIME_SOURCE_MANUAL);
    write_status = CRUX_OK;
    CHECK(crux_time_set_realtime(&second, CRUX_TIME_SOURCE_EXTERNAL, true) == CRUX_OK);
    read_status = CRUX_ERR_IO;
    out = first;
    s = sentinel;
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_IO && equal(out, first));
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_ERR_IO &&
          memcmp(&s, &sentinel, sizeof(s)) == 0);
    read_status = CRUX_OK;
    CHECK(snapshot().is_actively_synchronized);
    read_status = CRUX_ERR_NO_DATA;
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_OK && !s.is_time_valid &&
          s.timestamp.seconds == 0);
    expect_invalid(CRUX_TIME_SOURCE_EXTERNAL);
    read_status = CRUX_OK;
    CHECK(crux_time_set_realtime(&first, CRUX_TIME_SOURCE_RTC, false) == CRUX_OK);
    native_time.seconds = CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE;
    out = first;
    s = sentinel;
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_ERR_OUT_OF_RANGE &&
          memcmp(&s, &sentinel, sizeof(s)) == 0);
    expect_invalid(CRUX_TIME_SOURCE_RTC);
    CHECK(crux_time_adopt_realtime() == CRUX_ERR_OUT_OF_RANGE);
    native_time = first;
    read_status = CRUX_ERR_IO;
    CHECK(crux_time_adopt_realtime() == CRUX_ERR_IO);
    expect_invalid(CRUX_TIME_SOURCE_RTC);
    read_status = CRUX_OK;

    lock_status = CRUX_ERR_IO;
    CHECK(crux_time_init() == CRUX_ERR_IO);
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_IO && equal(out, first));
    CHECK(crux_time_set_realtime(&first, CRUX_TIME_SOURCE_MANUAL, false) == CRUX_ERR_IO);
    lock_status = CRUX_OK;
    thread_context = false;
    CHECK(crux_time_init() == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_time_set_realtime(&first, CRUX_TIME_SOURCE_MANUAL, false) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_time_adopt_realtime() == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_time_notify_sync_state(false) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_time_invalidate_realtime() == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_time_get_realtime(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    thread_context = true;

    CHECK(crux_time_set_realtime(&first, CRUX_TIME_SOURCE_MANUAL, false) == CRUX_OK);
    pthread_t threads[4];
    CHECK(pthread_create(&threads[0], NULL, writer, NULL) == 0);
    CHECK(pthread_create(&threads[1], NULL, writer, &threads) == 0);
    CHECK(pthread_create(&threads[2], NULL, reader, NULL) == 0);
    CHECK(pthread_create(&threads[3], NULL, reader, NULL) == 0);
    for (unsigned i = 0; i < 4; ++i)
        CHECK(pthread_join(threads[i], NULL) == 0);
    printf("realtime authority and 20000 concurrent snapshots: %d failures\n", failures);
    return failures != 0;
}
