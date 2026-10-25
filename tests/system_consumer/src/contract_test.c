/* Public-symbol doubles test consumer decisions, not provider conformance. */
#include "runtime.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static crux_status_t report_status;
static crux_status_t cause_status;
static crux_status_t feed_status;
static uint32_t reported_causes;
static crux_system_status_t service_report;
static unsigned observations;
static unsigned cause_reads;
static unsigned feeds;
static unsigned lifecycle_effects;

crux_status_t crux_system_status_get(crux_system_status_t *out) {
    ++observations;
    if (report_status == CRUX_OK)
        *out = service_report;
    return report_status;
}

crux_status_t crux_system_reset_cause_get(crux_reset_cause_t *out) {
    ++cause_reads;
    if (cause_status == CRUX_OK)
        *out = (crux_reset_cause_t){CRUX_RESET_CAUSE_WATCHDOG | CRUX_RESET_CAUSE_OTHER,
                                    reported_causes};
    return cause_status;
}

crux_status_t crux_watchdog_feed(crux_watchdog_t watchdog) {
    ++feeds;
    assert(watchdog.reserved == 42);
    return feed_status;
}

/* These deliberately visible side effects catch accidental lifecycle policy. */
crux_status_t crux_watchdog_start(uint32_t timeout_ms, crux_watchdog_t *out) {
    (void)timeout_ms;
    (void)out;
    ++lifecycle_effects;
    return CRUX_ERR_IO;
}
crux_status_t crux_system_reset_cause_clear(void) {
    ++lifecycle_effects;
    return CRUX_ERR_IO;
}
crux_status_t crux_system_reset(void) {
    ++lifecycle_effects;
    return CRUX_ERR_NOT_SUPPORTED;
}

typedef crux_status_t (*observe_fn)(system_runtime_observation_t *out);
typedef crux_status_t (*feed_fn)(crux_watchdog_t watchdog, crux_status_t work_status);

static void check_consumer(observe_fn observe, feed_fn feed) {
    system_runtime_observation_t result;
    unsigned char saved[sizeof(result)];
    const crux_watchdog_t token = {42};
    const crux_status_t work_errors[] = {CRUX_ERR_IO, CRUX_ERR_TIMEOUT, CRUX_ERR_NOT_READY};
    const crux_status_t cause_errors[] = {CRUX_ERR_NOT_SUPPORTED, CRUX_ERR_IO, CRUX_ERR_NOT_READY};
    const crux_status_t report_errors[] = {CRUX_ERR_NOT_SUPPORTED, CRUX_ERR_IO};

    observations = cause_reads = feeds = lifecycle_effects = 0;
    report_status = CRUX_OK;
    cause_status = CRUX_OK;
    feed_status = CRUX_OK;
    reported_causes = CRUX_RESET_CAUSE_WATCHDOG | CRUX_RESET_CAUSE_OTHER;
    service_report = (crux_system_status_t){
        CRUX_ERR_IO, CRUX_WATCHDOG_FAULTED, true, 2000, CRUX_ERR_NOT_SUPPORTED, CRUX_ERR_NOT_READY};

    assert(observe(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    assert(observations == 0 && cause_reads == 0);
    assert(observe(&result) == CRUX_OK);
    assert(result.services.watchdog_status == CRUX_ERR_IO);
    assert(result.services.watchdog_state == CRUX_WATCHDOG_FAULTED);
    assert(result.services.watchdog_emulated && result.services.watchdog_timeout_ms == 2000);
    assert(result.services.reset_status == CRUX_ERR_NOT_SUPPORTED);
    assert(result.services.reset_cause_status == CRUX_ERR_NOT_READY);
    assert(result.cause_status == CRUX_OK);
    assert(result.cause.supported == (CRUX_RESET_CAUSE_WATCHDOG | CRUX_RESET_CAUSE_OTHER));
    assert(result.cause.causes == (CRUX_RESET_CAUSE_WATCHDOG | CRUX_RESET_CAUSE_OTHER));
    assert(feeds == 0 && lifecycle_effects == 0);

    for (size_t i = 0; i < sizeof(cause_errors) / sizeof(cause_errors[0]); ++i) {
        cause_status = cause_errors[i];
        assert(observe(&result) == CRUX_OK);
        assert(result.cause_status == cause_errors[i]);
        assert(result.cause.supported == 0 && result.cause.causes == 0);
        assert(result.services.watchdog_status == CRUX_ERR_IO);
    }
    cause_status = CRUX_OK;
    reported_causes = 0;
    assert(observe(&result) == CRUX_OK);
    assert(result.cause_status == CRUX_OK && result.cause.causes == 0);
    assert(result.cause.supported == (CRUX_RESET_CAUSE_WATCHDOG | CRUX_RESET_CAUSE_OTHER));
    memcpy(saved, &result, sizeof(result));
    const unsigned reads_before = cause_reads;
    for (size_t i = 0; i < sizeof(report_errors) / sizeof(report_errors[0]); ++i) {
        report_status = report_errors[i];
        assert(observe(&result) == report_errors[i]);
        assert(memcmp(saved, (const unsigned char *)&result, sizeof(result)) == 0);
        assert(cause_reads == reads_before);
    }
    assert(feeds == 0 && lifecycle_effects == 0);

    for (size_t i = 0; i < sizeof(work_errors) / sizeof(work_errors[0]); ++i) {
        assert(feed(token, work_errors[i]) == work_errors[i]);
        assert(feeds == 0);
    }
    assert(feed(token, CRUX_OK) == CRUX_OK);
    assert(feeds == 1);
    feed_status = CRUX_ERR_TIMEOUT;
    assert(feed(token, CRUX_OK) == CRUX_ERR_TIMEOUT);
    assert(feeds == 2 && lifecycle_effects == 0);
}

int main(void) {
    check_consumer(system_runtime_observe, system_runtime_feed_after_work);
    check_consumer(system_cpp_observe, system_cpp_feed_after_work);
    puts("System C17/C++17 consumer policy checks passed");
    return 0;
}
