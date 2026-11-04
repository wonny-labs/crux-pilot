/* Shared helpers for the Security event suites. The suites link with the --wrap options in
 * CMakeLists.txt, so the core's calls to Time, Logging and the Sync and Thread creators reach the
 * controls here. Each control passes through to the real function when told to. */
#ifndef CRUX_SECURITY_EVENT_SUPPORT_H
#define CRUX_SECURITY_EVENT_SUPPORT_H

#include "security_event_test.h"

#include <crux/thread.h>
#include <crux/time.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Atomic, because hooks run on real threads in some suites. */
extern atomic_uint event_test_checks, event_test_failures;

#define EVENT_CHECK(expression)                                                                    \
    do {                                                                                           \
        ++event_test_checks;                                                                       \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            ++event_test_failures;                                                                 \
        }                                                                                          \
    } while (0)

/* A suite that also uses the Access helpers includes access_test.h first, which defines CHECK. */
#ifndef CHECK
#define CHECK(expression) EVENT_CHECK(expression)
#endif

/* Reports the totals, and is the suite's exit status. */
int event_test_finish(const char *suite);

typedef void (*event_test_hook_t)(void *context);

/* Returns every control to its default and the component to the state before init. */
void event_test_reset(void);
/* As event_test_reset, then a successful init with no project identifiers. */
void event_test_boot(void);

/* Monotonic clock, starting at 1000 ms. */
void event_test_clock_set_ms(uint64_t now_ms);
void event_test_clock_advance_ms(uint64_t delta_ms);
void event_test_clock_fail(crux_status_t status);
/* For values that a clock set in milliseconds cannot reach. */
void event_test_clock_set_ns(int64_t now_ns);
/* Uses the real clock, for a suite with real sleeps. */
void event_test_clock_real(bool real);
unsigned event_test_clock_reads(void);

/* The Time authority's snapshot. It starts invalid, with source NONE. */
void event_test_wall_set(int64_t seconds, uint32_t nanoseconds, crux_realtime_source_t source,
                         bool is_valid, bool is_synchronized);
void event_test_wall_fail(crux_status_t status);
unsigned event_test_wall_reads(void);
/* Runs inside each snapshot read, until cleared with NULL. */
void event_test_wall_hook(event_test_hook_t hook, void *context);

/* crux_log_write. Each accepted line is checked against the Logging rules and kept. */
typedef struct {
    crux_log_level_t level;
    char text[CRUX_LOG_TEXT_MAX + 1];
} event_test_line_t;

void event_test_log_status(crux_status_t status);
/* Runs inside each log write, before the line is kept, until cleared with NULL. */
void event_test_log_hook(event_test_hook_t hook, void *context);
size_t event_test_line_count(void);
const event_test_line_t *event_test_line(size_t index);
void event_test_lines_clear(void);
/* The most recent kept line, or an empty one. */
const char *event_test_last_line(void);

/* Fail the next creations, which then pass through again. */
void event_test_mutex_create_fail(crux_status_t status);
void event_test_semaphore_create_fail(crux_status_t status);
void event_test_thread_create_fail(crux_status_t status);
/* What the core last asked of the Thread provider. */
crux_thread_options_t event_test_thread_options(void);

void event_test_in_isr(bool in_isr);

/* Runs the drain step until it has nothing left. Returns the lines written. */
unsigned event_test_drain_all(void);

/* A record with these fields, a web origin of 192.0.2.7, and no other field. */
void event_test_emit(crux_security_event_id_t id, crux_security_event_result_t result,
                     const char *user, const char *subject);
/* An unwanted routine or change-class record. */
void event_test_emit_routine(void);
void event_test_emit_change(void);

/* Holds a Sync mutex on another thread until released, so a lock wait really expires. */
void event_test_hold_ring_lock(void);
void event_test_release_ring_lock(void);

crux_security_event_stats_t event_test_stats(void);

/* A record to prove that a call left its output alone. */
#define EVENT_TEST_UNTOUCHED_BYTE 0x5Au
void event_test_record_fill(crux_security_event_record_t *record);
bool event_test_record_untouched(const crux_security_event_record_t *record);

/* A poll that fails means that someone holds the ring lock. */
void event_test_expect_ring_lock_free(void *context);

#endif
