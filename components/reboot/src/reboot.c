/* The reboot service: the permission, the policy asked twice, the event, and the worker that waits
 * a bounded time for events and saves before it resets. See docs/reboot-service.md.
 *
 * One request is carried out at a time. A request made while one is pending is accepted and starts
 * nothing, and it runs no policy. The worker holds no lock while it sleeps, polls or calls the
 * policy, the sources or the reset, so a request or a read is never kept waiting by it.
 *
 * The order for the worker, which the suites hold to:
 *
 *   1. sleep for CRUX_REBOOT_DELAY_MS, so that the answer to the request gets out. A sleep that
 *      cannot sleep ends the request as failed, with no reset,
 *   2. poll events and the wait sources, sleeping CRUX_REBOOT_POLL_MS between polls, until nothing
 *      is pending or CRUX_REBOOT_SETTLE_WAIT_MS has passed,
 *   3. if a source that blocks the reset is still pending, end the request as failed,
 *   4. ask the policy again, and end the request as refused if it refuses,
 *   5. reset. It returns only on an error, and that ends the request as failed.
 */
#include "reboot_port.h"

#include <crux/configuration.h>
#include <crux/sync.h>
#include <crux/thread.h>

#include <stdatomic.h>
#include <string.h>

#ifndef CRUX_REBOOT_LOCK_WAIT_MS
/* The longest a server-thread call waits for the state's lock, which is held only to copy. */
#define CRUX_REBOOT_LOCK_WAIT_MS 20u
#endif
#ifndef CRUX_REBOOT_WORKER_WAIT_MS
/* How long the worker waits for its signal, and for the state's lock when it needs it. */
#define CRUX_REBOOT_WORKER_WAIT_MS 1000u
#endif
#ifndef CRUX_REBOOT_DELAY_MS
/* The pause before the wait, so that the HTTP answer of the request has left the device. */
#define CRUX_REBOOT_DELAY_MS 1000u
#endif
#ifndef CRUX_REBOOT_SETTLE_WAIT_MS
/* The longest the worker waits for events and saves. A save of the Configuration service waits up
 * to 5 s for its own lock and then writes, so this is longer than one save. It is a bound and not a
 * promise that everything finished: see docs/reboot-service.md. */
#define CRUX_REBOOT_SETTLE_WAIT_MS 15000u
#endif
#ifndef CRUX_REBOOT_POLL_MS
/* The step of the wait. The wait makes CRUX_REBOOT_SETTLE_WAIT_MS / this + 1 polls at most. */
#define CRUX_REBOOT_POLL_MS 50u
#endif

_Static_assert(CRUX_REBOOT_POLL_MS > 0u, "the wait must step");
_Static_assert(CRUX_REBOOT_REASON_SIZE == 64u,
               "the reason holds 63 bytes of text, as a status text");

static struct {
    bool started;
    crux_reboot_policy_kind_t kind;
    crux_reboot_policy_t policy;
    void *policy_context;
    crux_reboot_wait_source_t sources[CRUX_REBOOT_WAIT_SOURCE_MAX];
    size_t source_count;

    crux_reboot_state_kind_t state;
    char reason[CRUX_REBOOT_REASON_SIZE];
    crux_reboot_failure_t failure;
    bool running; /* The worker has the job. */
    _Atomic bool
        announced; /* The `requested` event of the job is recorded: the worker may take it. */
    crux_access_session_t session;
} svc;

static crux_mutex_t state_lock;
static crux_semaphore_t work_signal;
static bool sync_created;
static bool worker_started;
static crux_thread_t worker_thread;

static bool lock_state(uint32_t wait_ms)
{
    return crux_mutex_lock(state_lock, wait_ms) == CRUX_OK;
}

static void unlock_state(void)
{
    (void)crux_mutex_unlock(state_lock);
}

/* Start. */

static crux_reboot_result_t check_config(const crux_reboot_config_t *config)
{
    if (config == NULL) {
        return CRUX_REBOOT_ERR_INVALID_ARGUMENT;
    }
    /* The statement of the policy comes first: an image must not run with it unset, whatever else
     * its configuration holds. */
    if (config->policy_kind == CRUX_REBOOT_POLICY_CALLBACK) {
        if (config->policy == NULL) {
            return CRUX_REBOOT_ERR_NO_POLICY;
        }
    } else if (config->policy_kind == CRUX_REBOOT_POLICY_ALWAYS_ALLOW) {
        if (config->policy != NULL) {
            return CRUX_REBOOT_ERR_INVALID_ARGUMENT;
        }
    } else {
        return CRUX_REBOOT_ERR_NO_POLICY;
    }
    if (config->wait_source_count > CRUX_REBOOT_WAIT_SOURCE_MAX ||
        (config->wait_source_count > 0u && config->wait_sources == NULL)) {
        return CRUX_REBOOT_ERR_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < config->wait_source_count; ++index) {
        if (config->wait_sources[index].busy == NULL) {
            return CRUX_REBOOT_ERR_INVALID_ARGUMENT;
        }
    }
    return CRUX_REBOOT_OK;
}

crux_reboot_result_t crux_reboot_start(const crux_reboot_config_t *config)
{
    crux_reboot_result_t checked;

    if (svc.started) {
        return CRUX_REBOOT_OK;
    }
    checked = check_config(config);
    if (checked != CRUX_REBOOT_OK) {
        return checked;
    }
    if (!sync_created) {
        if (crux_mutex_create(&state_lock) != CRUX_OK ||
            crux_semaphore_create(0u, 1u, &work_signal) != CRUX_OK) {
            return CRUX_REBOOT_ERR_NOT_READY;
        }
        sync_created = true;
    }
    memset(&svc, 0, sizeof(svc));
    atomic_init(&svc.announced, false);
    svc.kind = config->policy_kind;
    svc.policy = config->policy;
    svc.policy_context = config->policy_context;
    svc.source_count = config->wait_source_count;
    for (size_t index = 0; index < svc.source_count; ++index) {
        svc.sources[index] = config->wait_sources[index];
    }
    svc.state = CRUX_REBOOT_STATE_NONE;
    svc.started = true;
    return CRUX_REBOOT_OK;
}

/* Permission. A session that lacks it is denied by Access, which records the denial. */

static crux_reboot_result_t check_permission(crux_access_session_t session)
{
    switch (crux_access_check(session, CRUX_REBOOT_PERMISSION)) {
    case CRUX_ACCESS_OK:
        return CRUX_REBOOT_OK;
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        return CRUX_REBOOT_ERR_SESSION_INVALID;
    case CRUX_ACCESS_ERR_NOT_READY:
        return CRUX_REBOOT_ERR_NOT_READY;
    default:
        return CRUX_REBOOT_ERR_DENIED;
    }
}

/* Read. */

crux_reboot_result_t crux_reboot_get(crux_access_session_t session, crux_reboot_state_t *state)
{
    crux_reboot_result_t allowed;

    if (!svc.started) {
        return CRUX_REBOOT_ERR_NOT_READY;
    }
    allowed = check_permission(session);
    if (allowed != CRUX_REBOOT_OK) {
        return allowed;
    }
    if (state == NULL) {
        return CRUX_REBOOT_ERR_INVALID_ARGUMENT;
    }
    if (!lock_state(CRUX_REBOOT_LOCK_WAIT_MS)) {
        return CRUX_REBOOT_ERR_BUSY;
    }
    memset(state, 0, sizeof(*state));
    state->state = svc.state;
    state->failure = svc.state == CRUX_REBOOT_STATE_FAILED ? svc.failure : CRUX_REBOOT_FAILURE_NONE;
    if (svc.state == CRUX_REBOOT_STATE_REFUSED) {
        memcpy(state->reason, svc.reason, sizeof(state->reason));
    }
    unlock_state();
    return CRUX_REBOOT_OK;
}

/* The policy. */

/* The reason that a client may see: one to 63 bytes of well-formed UTF-8 with no control character,
 * ended inside the buffer. Anything else is the fixed default, so that no refusal shows nothing and
 * no byte of a callback's mistake reaches a client. */
static void take_reason(const char *raw, char shown[CRUX_REBOOT_REASON_SIZE])
{
    const void *end = memchr(raw, '\0', CRUX_REBOOT_REASON_SIZE);
    size_t length = end == NULL ? 0u : (size_t)((const char *)end - raw);

    memset(shown, 0, CRUX_REBOOT_REASON_SIZE);
    if (end == NULL || length == 0u || !crux_config_text_valid(raw, length)) {
        memcpy(shown, CRUX_REBOOT_DEFAULT_REASON, sizeof(CRUX_REBOOT_DEFAULT_REASON));
        return;
    }
    memcpy(shown, raw, length);
}

/* Asks the policy, on the calling thread, with no lock held. On a refusal fills `shown`. */
static bool policy_allows(char shown[CRUX_REBOOT_REASON_SIZE])
{
    crux_reboot_refusal_t refusal;

    memset(shown, 0, CRUX_REBOOT_REASON_SIZE);
    if (svc.kind == CRUX_REBOOT_POLICY_ALWAYS_ALLOW) {
        return true;
    }
    memset(&refusal, 0, sizeof(refusal));
    if (svc.policy(svc.policy_context, &refusal)) {
        return true;
    }
    take_reason(refusal.reason, shown);
    return false;
}

/* Request. */

crux_reboot_result_t crux_reboot_request(crux_access_session_t session,
                                         char reason[CRUX_REBOOT_REASON_SIZE])
{
    crux_reboot_result_t allowed;
    char shown[CRUX_REBOOT_REASON_SIZE];
    bool pending;

    if (!svc.started) {
        return CRUX_REBOOT_ERR_NOT_READY;
    }
    allowed = check_permission(session);
    if (allowed != CRUX_REBOOT_OK) {
        return allowed;
    }
    if (!lock_state(CRUX_REBOOT_LOCK_WAIT_MS)) {
        return CRUX_REBOOT_ERR_BUSY;
    }
    pending = svc.state == CRUX_REBOOT_STATE_PENDING;
    unlock_state();

    /* A request made while another is pending runs no policy: the pending one's second check
     * decides. Otherwise the application decides, with no lock held, because it is the
     * application's code. */
    if (!pending && !policy_allows(shown)) {
        if (!lock_state(CRUX_REBOOT_LOCK_WAIT_MS)) {
            return CRUX_REBOOT_ERR_BUSY;
        }
        /* A request that was accepted meanwhile keeps its place: a refusal does not cancel it. */
        if (svc.state != CRUX_REBOOT_STATE_PENDING) {
            svc.state = CRUX_REBOOT_STATE_REFUSED;
            memcpy(svc.reason, shown, sizeof(svc.reason));
            svc.failure = CRUX_REBOOT_FAILURE_NONE;
        }
        unlock_state();
        crux_reboot_port_event(CRUX_REBOOT_EVENT_REFUSED, session);
        if (reason != NULL) {
            memcpy(reason, shown, CRUX_REBOOT_REASON_SIZE);
        }
        return CRUX_REBOOT_ERR_REFUSED;
    }

    if (!lock_state(CRUX_REBOOT_LOCK_WAIT_MS)) {
        return CRUX_REBOOT_ERR_BUSY;
    }
    if (svc.state == CRUX_REBOOT_STATE_PENDING) {
        /* Accepted, and nothing new starts. It is still a request that someone made. */
        unlock_state();
        crux_reboot_port_event(CRUX_REBOOT_EVENT_REQUESTED, session);
        return CRUX_REBOOT_OK;
    }
    svc.state = CRUX_REBOOT_STATE_PENDING;
    memset(svc.reason, 0, sizeof(svc.reason));
    svc.failure = CRUX_REBOOT_FAILURE_NONE;
    svc.running = false;
    atomic_store_explicit(&svc.announced, false, memory_order_release);
    svc.session = session;
    unlock_state();

    /* The event is recorded before the job can be taken, so nothing that the worker does can come
     * before the record of the request. */
    crux_reboot_port_event(CRUX_REBOOT_EVENT_REQUESTED, session);
    atomic_store_explicit(&svc.announced, true, memory_order_release);
    (void)crux_semaphore_give(work_signal);
    return CRUX_REBOOT_OK;
}

/* The worker. */

/* Polls events and the sources, once. Returns whether anything is pending, and whether a source
 * that blocks the reset is. Every source is asked on every poll, so that none is starved by
 * another. */
static bool poll_pending(bool *blocking_pending)
{
    bool pending = crux_reboot_port_events_pending();

    *blocking_pending = false;
    for (size_t index = 0; index < svc.source_count; ++index) {
        if (svc.sources[index].busy(svc.sources[index].context)) {
            pending = true;
            *blocking_pending = *blocking_pending || svc.sources[index].blocks_reset;
        }
    }
    return pending;
}

typedef enum {
    WAIT_DONE,    /* Nothing is pending, or only what the reset may cut. */
    WAIT_BLOCKED, /* A source that blocks the reset was still pending when the wait ended. */
    WAIT_NO_TIMER /* The sleep port could not sleep, so the bound is not one. */
} wait_result_t;

/* Waits for events and sources for at most CRUX_REBOOT_SETTLE_WAIT_MS of sleep. */
static wait_result_t wait_for_work(void)
{
    uint32_t waited = 0u;
    bool blocking;

    for (;;) {
        if (!poll_pending(&blocking)) {
            return WAIT_DONE;
        }
        if (waited >= CRUX_REBOOT_SETTLE_WAIT_MS) {
            return blocking ? WAIT_BLOCKED : WAIT_DONE;
        }
        if (!crux_reboot_port_sleep(CRUX_REBOOT_POLL_MS)) {
            return WAIT_NO_TIMER;
        }
        waited += CRUX_REBOOT_POLL_MS;
    }
}

/* Ends the job: the event first, with no lock held, then the state, so that a state that a client
 * reads has its record behind it. */
static void end_job(crux_reboot_state_kind_t state, crux_reboot_failure_t failure,
                    const char *reason, crux_access_session_t session)
{
    crux_reboot_port_event(state == CRUX_REBOOT_STATE_REFUSED ? CRUX_REBOOT_EVENT_REFUSED
                                                              : CRUX_REBOOT_EVENT_FAILED,
                           session);
    /* Nothing else changes a running job, so the state is still this thread's. A server-thread call
     * waits briefly, and this thread may wait longer, because nothing waits on it. */
    while (!lock_state(CRUX_REBOOT_WORKER_WAIT_MS)) {
    }
    svc.state = state;
    svc.failure = failure;
    memset(svc.reason, 0, sizeof(svc.reason));
    if (reason != NULL) {
        memcpy(svc.reason, reason, sizeof(svc.reason));
    }
    svc.running = false;
    svc.session.value = 0u;
    unlock_state();
}

bool crux_reboot_work_once(void)
{
    crux_access_session_t session;
    char reason[CRUX_REBOOT_REASON_SIZE];

    if (!svc.started || !lock_state(CRUX_REBOOT_WORKER_WAIT_MS)) {
        return false;
    }
    if (svc.state != CRUX_REBOOT_STATE_PENDING || svc.running ||
        !atomic_load_explicit(&svc.announced, memory_order_acquire)) {
        unlock_state();
        return false;
    }
    svc.running = true;
    session = svc.session;
    unlock_state();

    /* A sleep that cannot sleep makes the delay and the bound of the wait return at once, so the
     * request fails instead, and the reset is not called. */
    if (!crux_reboot_port_sleep(CRUX_REBOOT_DELAY_MS)) {
        end_job(CRUX_REBOOT_STATE_FAILED, CRUX_REBOOT_FAILURE_NO_TIMER, NULL, session);
        return true;
    }
    switch (wait_for_work()) {
    case WAIT_BLOCKED:
        end_job(CRUX_REBOOT_STATE_FAILED, CRUX_REBOOT_FAILURE_SAVE_IN_FLIGHT, NULL, session);
        return true;
    case WAIT_NO_TIMER:
        end_job(CRUX_REBOOT_STATE_FAILED, CRUX_REBOOT_FAILURE_NO_TIMER, NULL, session);
        return true;
    case WAIT_DONE:
    default:
        break;
    }
    /* The machine may have started while the request waited, so the application is asked again. */
    if (!policy_allows(reason)) {
        end_job(CRUX_REBOOT_STATE_REFUSED, CRUX_REBOOT_FAILURE_NONE, reason, session);
        return true;
    }
    /* On a target this does not return. Whatever it returns is a restart that did not happen. */
    (void)crux_reboot_port_reset();
    end_job(CRUX_REBOOT_STATE_FAILED, CRUX_REBOOT_FAILURE_RESET_RETURNED, NULL, session);
    return true;
}

static void worker_main(void *argument)
{
    (void)argument;
    for (;;) {
        (void)crux_semaphore_take(work_signal, CRUX_REBOOT_WORKER_WAIT_MS);
        while (crux_reboot_work_once()) {
        }
    }
}

crux_reboot_result_t crux_reboot_start_worker(void)
{
    static const crux_thread_options_t options = {0u, CRUX_THREAD_PRIORITY_LOW};

    if (!svc.started) {
        return CRUX_REBOOT_ERR_NOT_READY;
    }
    if (worker_started) {
        return CRUX_REBOOT_OK;
    }
    if (crux_thread_create(&options, worker_main, NULL, &worker_thread) != CRUX_OK) {
        return CRUX_REBOOT_ERR_NOT_READY;
    }
    worker_started = true;
    return CRUX_REBOOT_OK;
}
