/* Attempts and the one worker that verifies passwords (decision D10).
 *
 * The server thread queues an attempt and polls it. The worker takes one attempt, copies its
 * secrets out and clears them from the table, verifies with crux_access outside every lock, clears
 * its copies, and stores the result. The lock is held only to copy, so the server thread waits at
 * most CRUX_WEB_SESSION_LOCK_WAIT_MS for it, and gives a busy or pending answer rather than wait
 * longer. One attempt is queued or running at a time.
 */
#include "web_session_internal.h"

#include "web_port.h"

#include <crux/sync.h>
#include <crux/thread.h>
#include <string.h>

typedef enum { ATTEMPT_FREE = 0, ATTEMPT_QUEUED, ATTEMPT_RUNNING, ATTEMPT_DONE } attempt_state_t;

typedef struct {
    uint64_t changed_ms; /* When it was queued, or when it finished. */
    /* Input, cleared when the worker takes it. */
    size_t secret_length;
    size_t new_secret_length;
    attempt_state_t state;
    crux_access_origin_t origin;
    /* Result, cleared when the poll takes it. */
    web_attempt_result_t result;
    char user[CRUX_ACCESS_NAME_SIZE];
    char id[WEB_SESSION_TOKEN_SIZE];
    char secret[WEB_SESSION_SECRET_SIZE];
    char new_secret[WEB_SESSION_SECRET_SIZE];
} web_attempt_t;

/* What the worker copies out, on its own stack, and clears after the verification. */
typedef struct {
    web_attempt_t input;
    size_t index;
} web_job_t;

static web_attempt_t attempts[CRUX_WEB_SESSION_ATTEMPTS];
static crux_mutex_t attempt_lock;
static crux_semaphore_t attempt_signal;
static bool sync_created;
static bool worker_started;
static crux_thread_t worker_thread;
static crux_web_session_stats_t stats;

static bool lock(void)
{
    return crux_mutex_lock(attempt_lock, CRUX_WEB_SESSION_LOCK_WAIT_MS) == CRUX_OK;
}

static void unlock(void)
{
    (void)crux_mutex_unlock(attempt_lock);
}

static void clear_attempt(web_attempt_t *attempt)
{
    web_session_wipe(attempt, sizeof(*attempt));
}

void web_session_attempts_reset(void)
{
    if (!sync_created) {
        if (crux_mutex_create(&attempt_lock) != CRUX_OK) {
            return;
        }
        if (crux_semaphore_create(0u, 1u, &attempt_signal) != CRUX_OK) {
            (void)crux_mutex_destroy(&attempt_lock);
            return;
        }
        sync_created = true;
    }
    /* Before the server starts, on one thread, or in a Host test with no worker running. */
    for (size_t index = 0; index < CRUX_WEB_SESSION_ATTEMPTS; ++index) {
        if (attempts[index].state == ATTEMPT_DONE && attempts[index].result.session.value != 0u) {
            crux_access_logout(&attempts[index].result.session);
        }
        clear_attempt(&attempts[index]);
    }
    memset(&stats, 0, sizeof(stats));
}

bool web_session_attempts_ready(void)
{
    return sync_created;
}

static bool expired(const web_attempt_t *attempt, uint64_t now_ms)
{
    return attempt->state != ATTEMPT_FREE && attempt->state != ATTEMPT_RUNNING &&
           now_ms - attempt->changed_ms > CRUX_WEB_SESSION_ATTEMPT_TTL_MS;
}

/* Under the lock. Moves sessions that nobody collected out, to log out after the lock. */
static size_t expire_locked(crux_access_session_t *orphans, size_t capacity)
{
    uint64_t now_ms = crux_web_port_now_ms();
    size_t count = 0;

    for (size_t index = 0; index < CRUX_WEB_SESSION_ATTEMPTS; ++index) {
        web_attempt_t *attempt = &attempts[index];

        if (!expired(attempt, now_ms)) {
            continue;
        }
        if (attempt->result.session.value != 0u && count < capacity) {
            orphans[count++] = attempt->result.session;
        }
        ++stats.expired;
        clear_attempt(attempt);
    }
    return count;
}

static void log_out_all(crux_access_session_t *sessions, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        crux_access_logout(&sessions[index]);
    }
}

void web_session_attempts_expire(void)
{
    crux_access_session_t orphans[CRUX_WEB_SESSION_ATTEMPTS];
    size_t count;

    if (!sync_created || !lock()) {
        return;
    }
    count = expire_locked(orphans, CRUX_WEB_SESSION_ATTEMPTS);
    unlock();
    log_out_all(orphans, count);
}

static void copy_secret(char destination[WEB_SESSION_SECRET_SIZE], size_t *destination_length,
                        const char *source, size_t length)
{
    /* One byte past the accepted maximum is enough for crux_access to refuse it unread. */
    size_t kept = length > CRUX_ACCESS_PASSWORD_MAX + 1u ? CRUX_ACCESS_PASSWORD_MAX + 1u : length;

    if (kept != 0u) {
        memcpy(destination, source, kept);
    }
    destination[kept] = '\0';
    *destination_length = kept;
}

web_queue_t web_session_attempt_queue(const web_attempt_request_t *request,
                                      char id[WEB_SESSION_TOKEN_SIZE])
{
    crux_access_session_t orphans[CRUX_WEB_SESSION_ATTEMPTS];
    web_attempt_t *slot = NULL;
    size_t orphan_count;
    bool busy = false;

    if (!sync_created || !lock()) {
        ++stats.busy;
        return WEB_QUEUE_BUSY;
    }
    orphan_count = expire_locked(orphans, CRUX_WEB_SESSION_ATTEMPTS);
    for (size_t index = 0; index < CRUX_WEB_SESSION_ATTEMPTS; ++index) {
        if (attempts[index].state == ATTEMPT_QUEUED || attempts[index].state == ATTEMPT_RUNNING) {
            busy = true;
        } else if (attempts[index].state == ATTEMPT_FREE && slot == NULL) {
            slot = &attempts[index];
        }
    }
    /* A full table refuses rather than drop a result that a client is still polling for. */
    if (busy || slot == NULL || !web_session_token_draw(slot->id)) {
        if (slot != NULL) {
            clear_attempt(slot);
        }
        ++stats.busy;
        unlock();
        log_out_all(orphans, orphan_count);
        return WEB_QUEUE_BUSY;
    }
    slot->result.kind = request->kind;
    slot->result.session = request->session;
    if (request->kind == WEB_ATTEMPT_LOGIN) {
        size_t user_length = 0;

        while (user_length < CRUX_ACCESS_NAME_SIZE && request->user[user_length] != '\0') {
            ++user_length;
        }
        /* A name too long for any account becomes the empty name, which none can hold either. */
        if (user_length < CRUX_ACCESS_NAME_SIZE) {
            memcpy(slot->user, request->user, user_length + 1u);
        }
    } else {
        copy_secret(slot->new_secret, &slot->new_secret_length, request->new_secret,
                    request->new_secret_length);
    }
    copy_secret(slot->secret, &slot->secret_length, request->secret, request->secret_length);
    slot->origin = request->origin;
    slot->changed_ms = crux_web_port_now_ms();
    slot->state = ATTEMPT_QUEUED;
    memcpy(id, slot->id, WEB_SESSION_TOKEN_SIZE);
    ++stats.attempts;
    unlock();
    log_out_all(orphans, orphan_count);
    (void)crux_semaphore_give(attempt_signal);
    return WEB_QUEUE_OK;
}

web_poll_t web_session_attempt_poll(const char *id, web_attempt_result_t *result)
{
    crux_access_session_t orphans[CRUX_WEB_SESSION_ATTEMPTS];
    web_attempt_t *found = NULL;
    size_t orphan_count;
    web_poll_t answer = WEB_POLL_NOT_FOUND;

    if (!web_session_token_form(id)) {
        return WEB_POLL_NOT_FOUND;
    }
    if (!sync_created || !lock()) {
        /* The worker holds the lock only to copy. Saying pending leaves the attempt as it was. */
        return WEB_POLL_PENDING;
    }
    orphan_count = expire_locked(orphans, CRUX_WEB_SESSION_ATTEMPTS);
    /* Every identifier is compared, so the time does not say which one matched. */
    for (size_t index = 0; index < CRUX_WEB_SESSION_ATTEMPTS; ++index) {
        bool match = web_session_equal(attempts[index].id, id, CRUX_WEB_SESSION_TOKEN_TEXT);

        if (match && attempts[index].state != ATTEMPT_FREE && found == NULL) {
            found = &attempts[index];
        }
    }
    if (found != NULL && found->state == ATTEMPT_DONE) {
        *result = found->result;
        clear_attempt(found);
        answer = WEB_POLL_DONE;
    } else if (found != NULL) {
        answer = WEB_POLL_PENDING;
    }
    unlock();
    log_out_all(orphans, orphan_count);
    return answer;
}

/* Takes the queued attempt's input into the job, and clears it from the table. Under the lock. */
static bool take_job(web_job_t *job)
{
    for (size_t index = 0; index < CRUX_WEB_SESSION_ATTEMPTS; ++index) {
        web_attempt_t *attempt = &attempts[index];

        if (attempt->state == ATTEMPT_QUEUED) {
            job->input = *attempt;
            job->index = index;
            web_session_wipe(attempt->user, sizeof(attempt->user));
            web_session_wipe(attempt->secret, sizeof(attempt->secret));
            web_session_wipe(attempt->new_secret, sizeof(attempt->new_secret));
            attempt->state = ATTEMPT_RUNNING;
            return true;
        }
    }
    return false;
}

/* After a successful login: the cookies' values and their hashes, drawn here and not on the server
 * thread. On a failure the session is logged out and the attempt reports busy. */
static void issue_tokens(web_attempt_result_t *result)
{
    if (web_session_token_draw(result->token) && web_session_token_draw(result->csrf) &&
        web_session_token_hash(result->token, result->token_hash) &&
        web_session_token_hash(result->csrf, result->csrf_hash)) {
        return;
    }
    crux_access_logout(&result->session);
    web_session_wipe(result->token, sizeof(result->token));
    web_session_wipe(result->csrf, sizeof(result->csrf));
    web_session_wipe(result->token_hash, sizeof(result->token_hash));
    web_session_wipe(result->csrf_hash, sizeof(result->csrf_hash));
    result->result = CRUX_ACCESS_ERR_BUSY;
}

static void run_job(web_job_t *job, web_attempt_result_t *result)
{
    web_attempt_t *input = &job->input;

    *result = input->result;
    if (input->result.kind == WEB_ATTEMPT_LOGIN) {
        crux_access_session_t session = {0};

        /* The empty name is refused as one that no account can hold, with its event. */
        result->result = crux_access_login_password(input->user, input->secret,
                                                    input->secret_length, &input->origin, &session,
                                                    &result->retry_after_ms);
        result->session = session;
        if (result->result == CRUX_ACCESS_OK) {
            issue_tokens(result);
        }
    } else {
        result->result = crux_access_account_change_password(
            input->result.session, input->secret, input->secret_length, input->new_secret,
            input->new_secret_length, &result->retry_after_ms);
        result->session.value = 0u;
    }
}

bool web_session_work_once(void)
{
    web_job_t job;
    web_attempt_result_t result;
    uint64_t started;
    uint64_t elapsed;

    memset(&job, 0, sizeof(job));
    if (!sync_created || crux_mutex_lock(attempt_lock, CRUX_WEB_SESSION_WORKER_WAIT_MS) != CRUX_OK) {
        return false;
    }
    if (!take_job(&job)) {
        unlock();
        return false;
    }
    unlock();

    started = crux_web_port_now_ms();
    run_job(&job, &result);
    web_session_wipe(&job, sizeof(job));
    elapsed = crux_web_port_now_ms() - started;

    /* The attempt cannot have been taken meanwhile: nothing ends a running attempt. The worker may
     * wait longer than the server thread, since nothing waits on it. */
    while (crux_mutex_lock(attempt_lock, CRUX_WEB_SESSION_WORKER_WAIT_MS) != CRUX_OK) {
    }
    for (size_t index = 0; index < CRUX_WEB_SESSION_ATTEMPTS; ++index) {
        if (attempts[index].state == ATTEMPT_RUNNING) {
            attempts[index].result = result;
            attempts[index].state = ATTEMPT_DONE;
            attempts[index].changed_ms = crux_web_port_now_ms();
        }
    }
    ++stats.verified;
    if (elapsed > stats.worker_max_ms) {
        stats.worker_max_ms = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
    }
    unlock();
    web_session_wipe(&result, sizeof(result));
    return true;
}

static void worker_main(void *argument)
{
    (void)argument;
    for (;;) {
        (void)crux_semaphore_take(attempt_signal, CRUX_WEB_SESSION_WORKER_WAIT_MS);
        while (web_session_work_once()) {
        }
        web_session_attempts_expire();
    }
}

crux_web_result_t crux_web_session_start_worker(void)
{
    static const crux_thread_options_t options = {0u, CRUX_THREAD_PRIORITY_LOW};

    if (!sync_created) {
        return CRUX_WEB_ERR_FAILED;
    }
    if (worker_started) {
        return CRUX_WEB_OK;
    }
    if (crux_thread_create(&options, worker_main, NULL, &worker_thread) != CRUX_OK) {
        return CRUX_WEB_ERR_FAILED;
    }
    worker_started = true;
    return CRUX_WEB_OK;
}

void web_session_count_busy(void)
{
    if (sync_created && lock()) {
        ++stats.busy;
        unlock();
    }
}

void web_session_count_session(void)
{
    if (sync_created && lock()) {
        ++stats.sessions;
        unlock();
    }
}

void crux_web_session_stats_get(crux_web_session_stats_t *stats_out)
{
    if (stats_out == NULL) {
        return;
    }
    memset(stats_out, 0, sizeof(*stats_out));
    if (sync_created && lock()) {
        *stats_out = stats;
        unlock();
    }
}

#ifdef CRUX_WEB_SESSION_TESTING
bool web_session_attempts_hold_bytes(const void *bytes, size_t length);

bool web_session_attempts_hold_bytes(const void *bytes, size_t length)
{
    const uint8_t *memory = (const uint8_t *)attempts;

    for (size_t offset = 0; offset + length <= sizeof(attempts); ++offset) {
        if (memcmp(memory + offset, bytes, length) == 0) {
            return true;
        }
    }
    return false;
}
#endif
