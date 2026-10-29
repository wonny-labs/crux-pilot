#define _POSIX_C_SOURCE 200809L // NOLINT(bugprone-reserved-identifier): POSIX feature selector
#include <assert.h>
#include <crux/system.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int64_t now_ms;
static bool fail_clock, late_clock;
static unsigned late_reads;
// NOLINTNEXTLINE(bugprone-reserved-identifier): GNU linker wrapping ABI
int __wrap_clock_gettime(clockid_t clock, struct timespec *value)
{
    assert(clock == CLOCK_MONOTONIC);
    if (fail_clock) {
        errno = EIO;
        return -1;
    }
    int64_t now = late_clock ? (late_reads++ == 0 ? 1999 : 2000) : now_ms;
    value->tv_sec = (time_t)(now / 1000);
    value->tv_nsec = (long)((now % 1000) * 1000000);
    return 0;
}
static crux_watchdog_t start(uint32_t timeout)
{
    crux_watchdog_t value;
    assert(crux_watchdog_start(timeout, &value) == CRUX_OK && value.reserved != 0);
    return value;
}
static void profile(uint32_t timeout)
{
    crux_watchdog_t value = {42};
    assert(crux_watchdog_start(UINT32_MAX, &value) == CRUX_ERR_OUT_OF_RANGE && value.reserved == 42);
    assert(crux_watchdog_start(499, &value) == CRUX_ERR_OUT_OF_RANGE && value.reserved == 42);
    assert(crux_watchdog_start(750, &value) == CRUX_ERR_NOT_SUPPORTED && value.reserved == 42);
    value = start(timeout);
    crux_system_status_t report;
    assert(crux_system_status_get(&report) == CRUX_OK);
    assert(report.watchdog_status == CRUX_OK && report.watchdog_state == CRUX_WATCHDOG_ACTIVE);
    assert(report.watchdog_emulated && report.watchdog_timeout_ms == timeout);
    assert(crux_watchdog_start(timeout, &value) == CRUX_ERR_INVALID_STATE && value.reserved == 1);
    assert(crux_watchdog_feed(value) == CRUX_OK);
}
static void expiry(void)
{
    crux_watchdog_t w = start(2000), other = {0};
    now_ms = 1999;
    assert(crux_watchdog_feed(w) == CRUX_OK);
    now_ms = 3999;
    crux_system_status_t report;
    assert(crux_system_status_get(&report) == CRUX_OK);
    assert(report.watchdog_state == CRUX_WATCHDOG_EXPIRED &&
           report.watchdog_status == CRUX_ERR_TIMEOUT);
    assert(crux_watchdog_feed(w) == CRUX_ERR_TIMEOUT);
    assert(crux_watchdog_start(2000, &other) == CRUX_ERR_INVALID_STATE);
}
static void late(void)
{
    crux_watchdog_t w = start(2000);
    late_clock = true;
    assert(crux_watchdog_feed(w) == CRUX_ERR_TIMEOUT);
    crux_system_status_t report;
    assert(crux_system_status_get(&report) == CRUX_OK);
    assert(report.watchdog_state == CRUX_WATCHDOG_EXPIRED);
}
static void clock_failure(void)
{
    fail_clock = true;
    crux_watchdog_t w = {42};
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_IO && w.reserved == 42);
    fail_clock = false;
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_INVALID_STATE && w.reserved == 42);
}
static void backwards(void)
{
    now_ms = 1000;
    crux_watchdog_t w = start(2000);
    now_ms = 999;
    assert(crux_watchdog_feed(w) == CRUX_ERR_IO);
    now_ms = 1001;
    assert(crux_watchdog_feed(w) == CRUX_ERR_IO);
}
static void unsupported(void)
{
    crux_system_status_t report;
    assert(crux_system_status_get(&report) == CRUX_OK);
    assert(report.watchdog_emulated && report.watchdog_state == CRUX_WATCHDOG_IDLE);
    assert(report.reset_status == CRUX_ERR_NOT_SUPPORTED &&
           report.reset_cause_status == CRUX_ERR_NOT_SUPPORTED);
    crux_reset_cause_t cause = {7, 3}, saved = cause;
    assert(crux_system_reset_cause_get(&cause) == CRUX_ERR_NOT_SUPPORTED);
    assert(memcmp(&cause, &saved, sizeof(cause)) == 0);
    /* The normal Host advertises no cause service; it never clears anything. */
    assert(crux_system_reset_cause_clear() == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_system_reset() == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_system_status_get(&report) == CRUX_OK &&
           report.watchdog_state == CRUX_WATCHDOG_IDLE);
}
static pthread_barrier_t gate;
static crux_status_t results[2];
static crux_watchdog_t owners[2];
static void *starter(void *argument)
{
    size_t index = *(const size_t *)argument;
    int result = pthread_barrier_wait(&gate);
    assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
    results[index] = crux_watchdog_start(2000, &owners[index]);
    return NULL;
}
static void concurrent(void)
{
    pthread_t threads[2];
    owners[0] = (crux_watchdog_t){42};
    owners[1] = (crux_watchdog_t){43};
    const size_t indices[] = {0, 1};
    assert(pthread_barrier_init(&gate, NULL, 3) == 0);
    for (size_t i = 0; i < 2; ++i)
        assert(pthread_create(&threads[i], NULL, starter, (void *)&indices[i]) == 0);
    int result = pthread_barrier_wait(&gate);
    assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
    for (size_t i = 0; i < 2; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    assert((results[0] == CRUX_OK && results[1] == CRUX_ERR_INVALID_STATE) ||
           (results[1] == CRUX_OK && results[0] == CRUX_ERR_INVALID_STATE));
    unsigned winner = results[0] == CRUX_OK ? 0u : 1u;
    assert(owners[winner].reserved == 1);
    assert(owners[1 - winner].reserved == (winner == 0 ? 43u : 42u));
    assert(crux_watchdog_feed(owners[winner]) == CRUX_OK);
    assert(pthread_barrier_destroy(&gate) == 0);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "low") == 0)
        profile(500);
    else if (strcmp(argv[1], "mid") == 0)
        profile(2000);
    else if (strcmp(argv[1], "high") == 0)
        profile(64000);
    else if (strcmp(argv[1], "expiry") == 0)
        expiry();
    else if (strcmp(argv[1], "late") == 0)
        late();
    else if (strcmp(argv[1], "clock_failure") == 0)
        clock_failure();
    else if (strcmp(argv[1], "backwards") == 0)
        backwards();
    else if (strcmp(argv[1], "unsupported") == 0)
        unsupported();
    else if (strcmp(argv[1], "concurrent") == 0)
        concurrent();
    else
        return 2;
    return 0;
}
