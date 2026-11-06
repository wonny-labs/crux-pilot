/* The fake ports of the reboot service and the record of what went through them. They exist only
 * when Crux builds its own tests. */
#include "reboot_test.h"

#include "reboot_port.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t guard = PTHREAD_MUTEX_INITIALIZER;
enum { STEPS = 65536 };
static reboot_step_t steps[STEPS];
static size_t step_count;

static bool policy_allow[16];
static size_t policy_allow_count;
static size_t policy_call_count;
static char policy_reason[CRUX_REBOOT_REASON_SIZE];

static size_t source_polls_left[2];
static size_t source_poll_count[2];
static size_t events_pending_left;

static crux_status_t reset_result;
static size_t reset_call_count;
static uint64_t slept_ms;
static size_t sleep_count;
static reboot_sleep_hook_t sleep_hook_fn;
static reboot_event_hook_t event_hook_fn;
static size_t sleep_fails_from;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the kind, the value and the session differ
static void record(reboot_step_kind_t kind, int64_t value, crux_access_session_t session)
{
    pthread_mutex_lock(&guard);
    if (step_count < STEPS) {
        steps[step_count].kind = kind;
        steps[step_count].value = value;
        steps[step_count].session = session;
        ++step_count;
    }
    pthread_mutex_unlock(&guard);
}

void reboot_fakes_reset(void)
{
    pthread_mutex_lock(&guard);
    step_count = 0;
    policy_allow[0] = true;
    policy_allow_count = 1;
    policy_call_count = 0;
    memset(policy_reason, 0, sizeof(policy_reason));
    memset(source_polls_left, 0, sizeof(source_polls_left));
    memset(source_poll_count, 0, sizeof(source_poll_count));
    events_pending_left = 0;
    reset_result = CRUX_ERR_NOT_SUPPORTED;
    reset_call_count = 0;
    slept_ms = 0;
    sleep_count = 0;
    sleep_hook_fn = NULL;
    event_hook_fn = NULL;
    sleep_fails_from = 0;
    pthread_mutex_unlock(&guard);
}

void reboot_trace_clear(void)
{
    pthread_mutex_lock(&guard);
    step_count = 0;
    pthread_mutex_unlock(&guard);
}

size_t reboot_trace_count(void)
{
    return step_count;
}

const reboot_step_t *reboot_trace_get(size_t index)
{
    return index < step_count ? &steps[index] : NULL;
}

void reboot_trace_note(reboot_step_kind_t kind, int64_t value)
{
    crux_access_session_t none = {0};

    record(kind, value, none);
}

size_t reboot_trace_count_of(reboot_step_kind_t kind)
{
    size_t found = 0;

    pthread_mutex_lock(&guard);
    for (size_t index = 0; index < step_count; ++index) {
        found += steps[index].kind == kind;
    }
    pthread_mutex_unlock(&guard);
    return found;
}

size_t reboot_events_of(crux_reboot_event_t event)
{
    size_t found = 0;

    pthread_mutex_lock(&guard);
    for (size_t index = 0; index < step_count; ++index) {
        found += steps[index].kind == STEP_EVENT && steps[index].value == (int64_t)event;
    }
    pthread_mutex_unlock(&guard);
    return found;
}

crux_access_session_t reboot_event_session(crux_reboot_event_t event)
{
    crux_access_session_t found = {0};

    pthread_mutex_lock(&guard);
    for (size_t index = 0; index < step_count; ++index) {
        if (steps[index].kind == STEP_EVENT && steps[index].value == (int64_t)event) {
            found = steps[index].session;
            break;
        }
    }
    pthread_mutex_unlock(&guard);
    return found;
}

/* The policy. */

void reboot_policy_script(const bool *allow, size_t count)
{
    pthread_mutex_lock(&guard);
    policy_allow_count = count < 16u ? count : 16u;
    memcpy(policy_allow, allow, policy_allow_count * sizeof(bool));
    policy_call_count = 0;
    pthread_mutex_unlock(&guard);
}

void reboot_policy_always(bool allow)
{
    reboot_policy_script(&allow, 1u);
}

void reboot_policy_reason(const void *bytes, size_t length)
{
    pthread_mutex_lock(&guard);
    memset(policy_reason, 0, sizeof(policy_reason));
    memcpy(policy_reason, bytes, length < sizeof(policy_reason) ? length : sizeof(policy_reason));
    pthread_mutex_unlock(&guard);
}

size_t reboot_policy_calls(void)
{
    return policy_call_count;
}

bool reboot_policy_callback(void *context, crux_reboot_refusal_t *refusal)
{
    bool allow;

    (void)context;
    pthread_mutex_lock(&guard);
    allow = policy_allow[policy_call_count < policy_allow_count ? policy_call_count
                                                                : policy_allow_count - 1u];
    ++policy_call_count;
    if (!allow) {
        memcpy(refusal->reason, policy_reason, sizeof(policy_reason));
    }
    pthread_mutex_unlock(&guard);
    record(STEP_POLICY, allow ? 1 : 0, (crux_access_session_t){0});
    return allow;
}

/* The wait sources. */

void reboot_source_busy(size_t index, size_t polls)
{
    source_polls_left[index] = polls;
}

size_t reboot_source_polls(size_t index)
{
    return source_poll_count[index];
}

bool reboot_source_probe(void *context)
{
    size_t index = *(const size_t *)context;
    bool busy;

    pthread_mutex_lock(&guard);
    ++source_poll_count[index];
    busy = source_polls_left[index] > 0u;
    if (busy && source_polls_left[index] != REBOOT_FOREVER) {
        --source_polls_left[index];
    }
    pthread_mutex_unlock(&guard);
    record(STEP_BUSY, (int64_t)(2u * index + (busy ? 1u : 0u)), (crux_access_session_t){0});
    return busy;
}

void reboot_events_pending_for(size_t polls)
{
    events_pending_left = polls;
}

/* The ports. */

crux_status_t crux_reboot_port_reset(void)
{
    crux_status_t result;

    pthread_mutex_lock(&guard);
    ++reset_call_count;
    result = reset_result;
    pthread_mutex_unlock(&guard);
    record(STEP_RESET, (int64_t)result, (crux_access_session_t){0});
    return result;
}

void reboot_reset_returns(crux_status_t result)
{
    reset_result = result;
}

size_t reboot_reset_calls(void)
{
    return reset_call_count;
}

void reboot_sleep_fails_from(size_t nth)
{
    sleep_fails_from = nth;
}

bool crux_reboot_port_sleep(uint32_t milliseconds)
{
    reboot_sleep_hook_t hook;
    uint64_t elapsed;
    bool slept;

    pthread_mutex_lock(&guard);
    slept_ms += milliseconds;
    ++sleep_count;
    elapsed = slept_ms;
    hook = sleep_hook_fn;
    slept = sleep_fails_from == 0u || sleep_count < sleep_fails_from;
    pthread_mutex_unlock(&guard);
    record(STEP_SLEEP, (int64_t)milliseconds, (crux_access_session_t){0});
    if (hook != NULL && slept) {
        hook(elapsed);
    }
    return slept;
}

void reboot_sleep_hook(reboot_sleep_hook_t hook)
{
    sleep_hook_fn = hook;
}

uint64_t reboot_slept_ms(void)
{
    return slept_ms;
}

size_t reboot_sleeps(void)
{
    return sleep_count;
}

bool crux_reboot_port_events_pending(void)
{
    bool pending;

    pthread_mutex_lock(&guard);
    pending = events_pending_left > 0u;
    if (pending && events_pending_left != REBOOT_FOREVER) {
        --events_pending_left;
    }
    pthread_mutex_unlock(&guard);
    record(STEP_EVENTS_PENDING, pending ? 1 : 0, (crux_access_session_t){0});
    return pending;
}

void reboot_event_hook(reboot_event_hook_t hook)
{
    event_hook_fn = hook;
}

void crux_reboot_port_event(crux_reboot_event_t event, crux_access_session_t session)
{
    reboot_event_hook_t hook = event_hook_fn;

    if (hook != NULL) {
        hook(event);
    }
    record(STEP_EVENT, (int64_t)event, session);
}
