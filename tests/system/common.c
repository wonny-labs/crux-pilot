#include "system_port.h"
#include <assert.h>
#include <crux/system.h>
#include <stdio.h>
#include <string.h>

/* Boundary double only: the real common state machine is linked below.
 * Native calls record effects and return controlled observations; no OS model. */
static bool allowed = true, locked, running, expired, start_running = true;
static bool clear_effect = true, cause_supported = true;
static crux_status_t lock_result = CRUX_OK, probe_result = CRUX_OK;
static crux_status_t start_result = CRUX_OK, feed_result = CRUX_OK;
static crux_status_t cause_result = CRUX_ERR_NOT_SUPPORTED, clear_result = CRUX_OK;
static crux_status_t reset_status = CRUX_ERR_NOT_SUPPORTED, reset_result = CRUX_ERR_IO;
static crux_reset_cause_t cause = {0x7ffu, CRUX_RESET_CAUSE_POR | CRUX_RESET_CAUSE_WATCHDOG};
static unsigned starts, feeds, clears, resets, probes, locks;
bool system_port_context_allowed(void)
{
    return allowed;
}
crux_status_t system_port_lock(void)
{
    ++locks;
    if (lock_result != CRUX_OK)
        return lock_result;
    assert(!locked);
    locked = true;
    return CRUX_OK;
}
void system_port_unlock(void)
{
    assert(locked);
    locked = false;
}
bool system_port_watchdog_emulated(void)
{
    return true;
}
crux_status_t system_port_watchdog_validate(uint32_t ms)
{
    if (ms < 500 || ms > 64000)
        return CRUX_ERR_OUT_OF_RANGE;
    return ms % 500 == 0 ? CRUX_OK : CRUX_ERR_NOT_SUPPORTED;
}
crux_status_t system_port_watchdog_probe(system_watchdog_probe_t *out)
{
    assert(locked);
    ++probes;
    if (probe_result == CRUX_OK)
        *out = (system_watchdog_probe_t){running, expired};
    return probe_result;
}
crux_status_t system_port_watchdog_start(uint32_t ms)
{
    assert(locked && ms == 2000);
    ++starts;
    running = start_running;
    return start_result;
}
crux_status_t system_port_watchdog_feed(void)
{
    assert(locked);
    ++feeds;
    return feed_result;
}
crux_status_t system_port_reset_status(void)
{
    assert(locked);
    return reset_status;
}
crux_status_t system_port_reset(void)
{
    assert(!locked);
    ++resets;
    return reset_result;
}
bool system_port_reset_cause_supported(void)
{
    return cause_supported;
}
crux_status_t system_port_reset_cause_get(crux_reset_cause_t *out)
{
    assert(locked);
    if (cause_result == CRUX_OK)
        *out = cause;
    return cause_result;
}
crux_status_t system_port_reset_cause_clear(void)
{
    assert(locked);
    ++clears;
    if (clear_result == CRUX_OK && clear_effect)
        cause.causes = 0;
    return clear_result;
}
static crux_watchdog_t start(void)
{
    crux_watchdog_t w = {0};
    assert(crux_watchdog_start(2000, &w) == CRUX_OK && w.reserved != 0);
    return w;
}
static void status_case(void)
{
    crux_system_status_t s;
    probe_result = CRUX_ERR_NOT_READY;
    cause_result = CRUX_ERR_IO;
    assert(crux_system_status_get(&s) == CRUX_OK);
    assert(s.watchdog_state == CRUX_WATCHDOG_UNAVAILABLE &&
           s.watchdog_status == CRUX_ERR_NOT_READY);
    assert(s.watchdog_emulated && s.watchdog_timeout_ms == 0);
    assert(s.reset_status == CRUX_ERR_NOT_SUPPORTED && s.reset_cause_status == CRUX_ERR_IO);
    assert(starts == 0 && feeds == 0 && clears == 0 && resets == 0);
    unsigned before = probes;
    assert(crux_system_status_get(NULL) == CRUX_ERR_INVALID_ARGUMENT && probes == before);
    memset(&s, 0xa5, sizeof(s));
    unsigned char saved[sizeof(s)];
    memcpy(saved, &s, sizeof(s));
    lock_result = CRUX_ERR_IO;
    assert(crux_system_status_get(&s) == CRUX_ERR_IO &&
           memcmp(saved, (const unsigned char *)&s, sizeof(s)) == 0);
}
static void validation(void)
{
    crux_watchdog_t w = {0};
    assert(crux_watchdog_start(0, &w) == CRUX_ERR_OUT_OF_RANGE && w.reserved == 0);
    assert(crux_watchdog_start(499, &w) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_watchdog_start(64001, &w) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_watchdog_start(750, &w) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_watchdog_start(2000, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_watchdog_feed(w) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_system_reset_cause_get(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    assert(starts == 0 && feeds == 0 && probes == 0);
}
static void lifecycle(void)
{
    crux_watchdog_t w = start(), other = {0};
    crux_system_status_t s;
    assert(crux_system_status_get(&s) == CRUX_OK && s.watchdog_state == CRUX_WATCHDOG_ACTIVE);
    assert(s.watchdog_status == CRUX_OK && s.watchdog_timeout_ms == 2000);
    assert(crux_watchdog_feed(w) == CRUX_OK && feeds == 1);
    assert(crux_watchdog_start(2000, &other) == CRUX_ERR_INVALID_STATE && other.reserved == 0);
    assert(crux_watchdog_feed((crux_watchdog_t){UINT32_MAX}) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_watchdog_feed(w) == CRUX_OK && starts == 1 && feeds == 2);
}
static void unready(void)
{
    crux_watchdog_t w = {0};
    probe_result = CRUX_ERR_NOT_READY;
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_NOT_READY && w.reserved == 0 && starts == 0);
    probe_result = CRUX_OK;
    (void)start();
    assert(starts == 1);
}
static void prearmed(void)
{
    crux_watchdog_t w = {0};
    running = true;
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_IO && w.reserved == 0 && starts == 0);
    running = false;
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_INVALID_STATE && starts == 0);
}
static void start_failure(void)
{
    crux_watchdog_t w = {0};
    start_result = CRUX_ERR_IO;
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_IO && w.reserved == 0);
    assert(running);
    start_result = CRUX_OK;
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_INVALID_STATE && starts == 1);
}
static void post_failure(void)
{
    crux_watchdog_t w = {0};
    start_running = false;
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_IO && w.reserved == 0);
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_INVALID_STATE && starts == 1);
}
static void feed_failure(void)
{
    crux_watchdog_t w = start();
    feed_result = CRUX_ERR_IO;
    assert(crux_watchdog_feed(w) == CRUX_ERR_IO);
    feed_result = CRUX_OK;
    assert(crux_watchdog_feed(w) == CRUX_ERR_IO && feeds == 1);
    crux_system_status_t s;
    assert(crux_system_status_get(&s) == CRUX_OK && s.watchdog_state == CRUX_WATCHDOG_FAULTED);
    assert(s.watchdog_status == CRUX_ERR_IO && s.watchdog_timeout_ms == 2000);
}
static void lost(void)
{
    crux_watchdog_t w = start();
    probe_result = CRUX_ERR_NOT_READY;
    crux_system_status_t s;
    assert(crux_system_status_get(&s) == CRUX_OK && s.watchdog_state == CRUX_WATCHDOG_FAULTED);
    assert(s.watchdog_status == CRUX_ERR_IO);
    assert(crux_watchdog_feed(w) == CRUX_ERR_IO && feeds == 0);
}
static void expiry(void)
{
    crux_watchdog_t w = start(), other = {0};
    expired = true;
    running = false;
    crux_system_status_t s;
    assert(crux_system_status_get(&s) == CRUX_OK && s.watchdog_state == CRUX_WATCHDOG_EXPIRED);
    assert(s.watchdog_status == CRUX_ERR_TIMEOUT);
    assert(crux_watchdog_feed(w) == CRUX_ERR_TIMEOUT && feeds == 0);
    assert(crux_watchdog_start(2000, &other) == CRUX_ERR_INVALID_STATE);
}
static void causes(void)
{
    cause_result = CRUX_OK;
    crux_system_status_t s;
    crux_reset_cause_t report;
    assert(crux_system_status_get(&s) == CRUX_OK && s.reset_cause_status == CRUX_OK);
    assert(crux_system_reset_cause_clear() == CRUX_ERR_INVALID_STATE && clears == 0);
    assert(crux_system_reset_cause_get(&report) == CRUX_OK);
    assert(report.causes == (CRUX_RESET_CAUSE_POR | CRUX_RESET_CAUSE_WATCHDOG));
    assert(crux_system_reset_cause_clear() == CRUX_OK && clears == 1);
    assert(report.causes == (CRUX_RESET_CAUSE_POR | CRUX_RESET_CAUSE_WATCHDOG));
    assert(crux_system_reset_cause_clear() == CRUX_ERR_INVALID_STATE && clears == 1);
    assert(crux_system_reset_cause_get(&report) == CRUX_OK && report.causes == 0);
    assert(crux_system_reset_cause_clear() == CRUX_OK);
}
static void unsupported_cause(void)
{
    cause_supported = false;
    assert(crux_system_reset_cause_clear() == CRUX_ERR_NOT_SUPPORTED && clears == 0);
}
static void cause_failure(void)
{
    crux_reset_cause_t report = {7, 3}, saved = report;
    assert(crux_system_reset_cause_get(&report) == CRUX_ERR_NOT_SUPPORTED);
    assert(memcmp(&report, &saved, sizeof(report)) == 0);
    cause_result = CRUX_ERR_IO;
    assert(crux_system_reset_cause_get(&report) == CRUX_ERR_IO);
    assert(memcmp(&report, &saved, sizeof(report)) == 0);
    cause_result = CRUX_OK;
    cause = (crux_reset_cause_t){CRUX_RESET_CAUSE_POR, CRUX_RESET_CAUSE_WATCHDOG};
    assert(crux_system_reset_cause_get(&report) == CRUX_ERR_IO);
    cause = (crux_reset_cause_t){0x80000000u, 0};
    assert(crux_system_reset_cause_get(&report) == CRUX_ERR_IO);
    assert(crux_system_reset_cause_clear() == CRUX_ERR_INVALID_STATE && clears == 0);
}
static void clear_failure(void)
{
    cause_result = CRUX_OK;
    crux_reset_cause_t report;
    assert(crux_system_reset_cause_get(&report) == CRUX_OK);
    clear_effect = false;
    assert(crux_system_reset_cause_clear() == CRUX_ERR_IO && clears == 1);
    assert(crux_system_reset_cause_clear() == CRUX_ERR_INVALID_STATE && clears == 1);
    assert(crux_system_reset_cause_get(&report) == CRUX_OK);
    clear_result = CRUX_ERR_IO;
    assert(crux_system_reset_cause_clear() == CRUX_ERR_IO && clears == 2);
    assert(crux_system_reset_cause_clear() == CRUX_ERR_INVALID_STATE && clears == 2);
    clear_result = CRUX_OK;
    clear_effect = true;
    assert(crux_system_reset_cause_get(&report) == CRUX_OK);
    assert(crux_system_reset_cause_clear() == CRUX_OK && clears == 3);
}
static void reset_case(void)
{
    assert(crux_system_reset() == CRUX_ERR_NOT_SUPPORTED && resets == 0);
    reset_status = CRUX_OK;
    assert(crux_system_reset() == CRUX_ERR_IO && resets == 1);
    reset_result = CRUX_OK;
    assert(crux_system_reset() == CRUX_ERR_IO && resets == 2);
}
static void context(void)
{
    allowed = false;
    crux_system_status_t s;
    crux_reset_cause_t r;
    crux_watchdog_t w = {0};
    assert(crux_system_status_get(&s) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_system_reset_cause_get(&r) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_system_reset_cause_clear() == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_system_reset() == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_watchdog_start(2000, &w) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_watchdog_feed(w) == CRUX_ERR_NOT_SUPPORTED);
    assert(locks == 0 && probes == 0 && starts == 0 && feeds == 0 && clears == 0 && resets == 0);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    struct {
        const char *name;
        void (*run)(void);
    } cases[] = {{"status", status_case},
                 {"validation", validation},
                 {"lifecycle", lifecycle},
                 {"unready", unready},
                 {"prearmed", prearmed},
                 {"start_failure", start_failure},
                 {"post_failure", post_failure},
                 {"feed_failure", feed_failure},
                 {"lost", lost},
                 {"expiry", expiry},
                 {"causes", causes},
                 {"cause_failure", cause_failure},
                 {"unsupported_cause", unsupported_cause},
                 {"clear_failure", clear_failure},
                 {"reset", reset_case},
                 {"context", context}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
        if (strcmp(argv[1], cases[i].name) == 0) {
            cases[i].run();
            assert(!locked);
            return 0;
        }
    return 2;
}
