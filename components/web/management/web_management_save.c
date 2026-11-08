/* The one worker that saves configuration, and the state that the list shows of it.
 *
 * crux_config_service_save() holds the service lock across storage and waits a finite time for it,
 * so it never runs on the server thread. The server thread queues one save and reads its state. The
 * lock here guards only the state, and nothing holds it across the save, so the server thread waits
 * at most CRUX_WEB_MANAGEMENT_LOCK_WAIT_MS for it. One save is queued or running at a time. */
#include "web_management_internal.h"

#include <crux/sync.h>
#include <crux/thread.h>
#include <string.h>

typedef struct {
    web_save_state_t state;
    const char *code;
    crux_access_session_t session;
} save_t;

static save_t save;
static crux_mutex_t save_lock;
static crux_semaphore_t save_signal;
static bool sync_created;
static bool worker_started;
static crux_thread_t worker_thread;
static crux_web_management_stats_t stats;

static bool lock(void)
{
    return crux_mutex_lock(save_lock, CRUX_WEB_MANAGEMENT_LOCK_WAIT_MS) == CRUX_OK;
}

static void unlock(void)
{
    (void)crux_mutex_unlock(save_lock);
}

crux_web_result_t crux_web_management_init(void)
{
    if (!sync_created) {
        if (crux_mutex_create(&save_lock) != CRUX_OK) {
            return CRUX_WEB_ERR_FAILED;
        }
        if (crux_semaphore_create(0u, 1u, &save_signal) != CRUX_OK) {
            (void)crux_mutex_destroy(&save_lock);
            return CRUX_WEB_ERR_FAILED;
        }
        sync_created = true;
    }
    /* Before the server starts, on one thread, or in a Host test with no worker running. */
    memset(&save, 0, sizeof(save));
    memset(&stats, 0, sizeof(stats));
    return CRUX_WEB_OK;
}

web_save_queue_t web_mgmt_save_queue(crux_access_session_t session)
{
    web_save_queue_t result = WEB_SAVE_QUEUE_OK;

    if (!sync_created) {
        return WEB_SAVE_QUEUE_NOT_READY;
    }
    /* A lock that does not come in time is the worker copying, which is brief, so the answer is
     * busy and the client tries again. */
    if (!lock()) {
        return WEB_SAVE_QUEUE_BUSY;
    }
    if (save.state == WEB_SAVE_QUEUED || save.state == WEB_SAVE_RUNNING) {
        ++stats.saves_busy;
        result = WEB_SAVE_QUEUE_BUSY;
    } else {
        save.state = WEB_SAVE_QUEUED;
        save.code = NULL;
        save.session = session;
        ++stats.saves_queued;
    }
    unlock();
    if (result == WEB_SAVE_QUEUE_OK) {
        (void)crux_semaphore_give(save_signal);
    }
    return result;
}

web_save_view_t web_mgmt_save_view(void)
{
    web_save_view_t view = {WEB_SAVE_QUEUED, NULL};

    if (!sync_created) {
        view.state = WEB_SAVE_NONE;
        return view;
    }
    if (lock()) {
        view.state = save.state;
        view.code = save.code;
        unlock();
    }
    return view;
}

bool crux_web_management_save_pending(void)
{
    web_save_view_t view = web_mgmt_save_view();

    return view.state == WEB_SAVE_QUEUED || view.state == WEB_SAVE_RUNNING;
}

const char *web_mgmt_save_code(crux_config_service_result_t result,
                               crux_config_result_t failed_result)
{
    switch (result) {
    case CRUX_CONFIG_SERVICE_ERR_STORE:
        return failed_result == CRUX_CONFIG_ERR_NOT_SUPPORTED ? "not_supported" : "storage_failed";
    case CRUX_CONFIG_SERVICE_ERR_BUSY:
        return "busy";
    case CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID:
        return "unauthorized";
    case CRUX_CONFIG_SERVICE_ERR_DENIED:
        return "permission_denied";
    case CRUX_CONFIG_SERVICE_OK:
        return NULL;
    case CRUX_CONFIG_SERVICE_END:
    case CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT:
    case CRUX_CONFIG_SERVICE_ERR_NOT_READY:
    case CRUX_CONFIG_SERVICE_ERR_NOT_FOUND:
    case CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY:
    case CRUX_CONFIG_SERVICE_ERR_REJECTED:
    case CRUX_CONFIG_SERVICE_ERR_TOO_SMALL:
    default:
        return "not_ready";
    }
}

bool web_mgmt_work_once(void)
{
    crux_access_session_t session;
    crux_config_service_save_report_t report;
    crux_config_service_result_t result;

    memset(&report, 0, sizeof(report));
    if (!sync_created ||
        crux_mutex_lock(save_lock, CRUX_WEB_MANAGEMENT_WORKER_WAIT_MS) != CRUX_OK) {
        return false;
    }
    if (save.state != WEB_SAVE_QUEUED) {
        unlock();
        return false;
    }
    save.state = WEB_SAVE_RUNNING;
    session = save.session;
    unlock();

    /* The service checks config.save again, and waits for its own lock outside this one. */
    result = crux_config_service_save(session, &report);

    /* Nothing else ends a running save, so the state is still the worker's. The server thread only
     * ever waits briefly, and the worker may wait longer, since nothing waits on it. */
    while (crux_mutex_lock(save_lock, CRUX_WEB_MANAGEMENT_WORKER_WAIT_MS) != CRUX_OK) {
    }
    save.code = web_mgmt_save_code(result, report.failed_result);
    save.state = result == CRUX_CONFIG_SERVICE_OK ? WEB_SAVE_SAVED : WEB_SAVE_FAILED;
    save.session.value = 0u;
    ++stats.saves_finished;
    unlock();
    return true;
}

static void worker_main(void *argument)
{
    (void)argument;
    for (;;) {
        (void)crux_semaphore_take(save_signal, CRUX_WEB_MANAGEMENT_WORKER_WAIT_MS);
        while (web_mgmt_work_once()) {
        }
    }
}

crux_web_result_t crux_web_management_start_worker(void)
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

void crux_web_management_stats_get(crux_web_management_stats_t *stats_out)
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
