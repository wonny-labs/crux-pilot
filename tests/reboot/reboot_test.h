/* What the reboot suites share: the real Access core on its fake ports, the real reboot service,
 * and fake ports for the reset, the sleep, the pending events and the event record. Each case runs
 * in a forked child, so that every case starts from nothing.
 *
 * The sleep is virtual: it adds to a clock and returns, so that the delay of 1 s and a wait of 15 s
 * cost no time, and the suites can hold the bounds to exact numbers. */
#ifndef REBOOT_TEST_H
#define REBOOT_TEST_H

#include "access_test.h"
#include "reboot_port.h"

#include <crux/reboot.h>

#include <stdlib.h>

void reboot_run_case(const char *name, void (*body)(void));
int reboot_finish(const char *suite);

/* The sessions. Each account has the password ACCESS_TEST_PASSWORD.
 *   admin     access.manage, system.reboot, network.read, network.write, config.save
 *   rebooter  system.reboot
 *   reader    network.read
 *   stranger  other.thing
 *   forced    system.reboot, under a forced password change
 */
extern crux_access_session_t s_admin, s_rebooter, s_reader, s_stranger, s_forced;
extern const crux_access_session_t s_never_valid;

/* Boots Access and the accounts, and resets every fake. Starts no service. */
void reboot_boot(void);
/* The test policy's configuration: a callback, the two fake wait sources, both blocking. */
crux_reboot_config_t reboot_config(void);
/* reboot_boot(), then crux_reboot_start() on reboot_config(), expecting success. */
void reboot_start(void);

/* What the policy callback decides, one entry for each call in order. The last entry repeats. */
void reboot_policy_script(const bool *allow, size_t count);
void reboot_policy_always(bool allow);
/* The bytes that a refusal writes, copied as given, with no terminator added. At most 64. */
void reboot_policy_reason(const void *bytes, size_t length);
size_t reboot_policy_calls(void);
bool reboot_policy_callback(void *context, crux_reboot_refusal_t *refusal);

/* The two wait sources. Each reports busy for this many polls and then not, or for ever. */
#define REBOOT_FOREVER ((size_t) - 1)
void reboot_source_busy(size_t index, size_t polls);
/* Whether the source blocks the reset, which the case's own configuration decides. */
size_t reboot_source_polls(size_t index);

/* The pending-events port: true for this many polls, then false, or for ever. */
void reboot_events_pending_for(size_t polls);

/* What the reset port returns. It is called, and it returns. */
void reboot_reset_returns(crux_status_t result);
size_t reboot_reset_calls(void);

/* The nth sleep of the worker, counted from 1, and every one after it, fails: the sleep port could
 * not sleep. 0 for none. */
void reboot_sleep_fails_from(size_t nth);

/* A hook that runs inside every sleep of the worker, with the virtual time so far in ms. */
typedef void (*reboot_sleep_hook_t)(uint64_t elapsed_ms);
void reboot_sleep_hook(reboot_sleep_hook_t hook);
uint64_t reboot_slept_ms(void);
size_t reboot_sleeps(void);

/* A hook that runs inside the next event of this kind, before it is recorded. */
typedef void (*reboot_event_hook_t)(crux_reboot_event_t event);
void reboot_event_hook(reboot_event_hook_t hook);

/* The record of everything the service did through the ports, in order. */
typedef enum {
    STEP_POLICY,         /* value: 1 allowed, 0 refused */
    STEP_EVENT,          /* value: the crux_reboot_event_t; session */
    STEP_SLEEP,          /* value: milliseconds */
    STEP_EVENTS_PENDING, /* value: the answer */
    STEP_BUSY,           /* value: 2 * source index + the answer */
    STEP_RESET,          /* value: the result returned */
    STEP_RESPOND         /* value: the status. Added by the web suites. */
} reboot_step_kind_t;

typedef struct {
    int64_t value;
    reboot_step_kind_t kind;
    crux_access_session_t session;
} reboot_step_t;

void reboot_trace_clear(void);
size_t reboot_trace_count(void);
const reboot_step_t *reboot_trace_get(size_t index);
void reboot_trace_note(reboot_step_kind_t kind, int64_t value);
size_t reboot_trace_count_of(reboot_step_kind_t kind);
/* How many events of this kind were recorded. */
size_t reboot_events_of(crux_reboot_event_t event);
/* The session of the first event of this kind, or the never-valid one. */
crux_access_session_t reboot_event_session(crux_reboot_event_t event);

size_t reboot_denials(void);

/* The state, expecting a successful read as this session. */
crux_reboot_state_t reboot_state_of(crux_access_session_t session);

#endif
