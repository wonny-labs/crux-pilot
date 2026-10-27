#include <crux/logging.h>
#include <crux/sync.h>
#include <crux/system.h>
#include <crux/thread.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#if defined(CONFIG_BOARD_NATIVE_SIM)
#include <zephyr/devicetree.h>
#include <zephyr/drivers/serial/uart_emul.h>
#endif

#define OPERATION_TIMEOUT_MS 5000U
#define EXPECTED_WORKER_VALUE UINT32_C(0x4d354d35)
#define LOG_RECORD "worker-complete sync-handoff=ok system-status=ok"

typedef enum {
    STAGE_NONE,
    STAGE_LOG_LEVEL,
    STAGE_MUTEX_CREATE,
    STAGE_SEMAPHORE_CREATE,
    STAGE_EMPTY_POLL,
    STAGE_THREAD_CREATE,
    STAGE_HANDOFF_WAIT,
    STAGE_THREAD_JOIN,
    STAGE_WORKER_MUTEX_LOCK,
    STAGE_WORKER_MUTEX_UNLOCK,
    STAGE_SYSTEM_STATUS,
    STAGE_LOG_WRITE,
    STAGE_HANDOFF_GIVE,
    STAGE_MAIN_MUTEX_LOCK,
    STAGE_MAIN_MUTEX_UNLOCK,
    STAGE_WORKER_VALUE,
    STAGE_SEMAPHORE_DESTROY,
    STAGE_MUTEX_DESTROY
} operation_stage_t;

typedef struct {
    operation_stage_t failure_stage;
    crux_status_t failure_status;
    crux_status_t log_level_status;
    crux_status_t empty_poll_status;
    crux_status_t handoff_wait_status;
    crux_status_t handoff_give_status;
    operation_stage_t worker_failure_stage;
    crux_status_t worker_status;
    crux_status_t system_query_status;
    crux_status_t log_write_status;
    crux_system_status_t system;
    uint32_t value;
} integration_observation_t;

typedef struct {
    crux_mutex_t mutex;
    crux_semaphore_t handoff;
    crux_thread_t thread;
    bool mutex_created;
    bool semaphore_created;
    bool thread_created;
    uint32_t value;
    integration_observation_t observation;
} integration_state_t;

static integration_state_t state;

static void fail_once(integration_observation_t *observation, operation_stage_t stage,
                      crux_status_t status)
{
    if (observation->failure_status == CRUX_OK) {
        observation->failure_stage = stage;
        observation->failure_status = status;
    }
}

static void worker_entry(void *argument)
{
    integration_state_t *worker_state = argument;
    integration_observation_t *observation = &worker_state->observation;
    crux_status_t status = crux_mutex_lock(worker_state->mutex, OPERATION_TIMEOUT_MS);

    if (status != CRUX_OK) {
        observation->worker_failure_stage = STAGE_WORKER_MUTEX_LOCK;
        observation->worker_status = status;
        goto signal_completion;
    }
    worker_state->value = EXPECTED_WORKER_VALUE;
    status = crux_mutex_unlock(worker_state->mutex);
    if (status != CRUX_OK) {
        observation->worker_failure_stage = STAGE_WORKER_MUTEX_UNLOCK;
        observation->worker_status = status;
        goto signal_completion;
    }

    observation->system_query_status = crux_system_status_get(&observation->system);
    if (observation->system_query_status != CRUX_OK) {
        observation->worker_failure_stage = STAGE_SYSTEM_STATUS;
        observation->worker_status = observation->system_query_status;
        goto signal_completion;
    }

    observation->log_write_status = crux_log_write(CRUX_LOG_INFO, "m5-integration", LOG_RECORD);
    if (observation->log_write_status != CRUX_OK) {
        observation->worker_failure_stage = STAGE_LOG_WRITE;
        observation->worker_status = observation->log_write_status;
        goto signal_completion;
    }
    observation->worker_status = CRUX_OK;

signal_completion:
    observation->handoff_give_status = crux_semaphore_give(worker_state->handoff);
    if (observation->handoff_give_status != CRUX_OK && observation->worker_status == CRUX_OK) {
        observation->worker_failure_stage = STAGE_HANDOFF_GIVE;
        observation->worker_status = observation->handoff_give_status;
    }
}

static crux_status_t run_integration(integration_observation_t *out)
{
    crux_thread_options_t options = {0};
    crux_status_t status;

    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;

    state = (integration_state_t){0};
    state.observation.failure_status = CRUX_OK;
    state.observation.worker_status = CRUX_ERR_INVALID_STATE;
    state.observation.empty_poll_status = CRUX_ERR_INVALID_STATE;
    state.observation.handoff_wait_status = CRUX_ERR_INVALID_STATE;
    state.observation.handoff_give_status = CRUX_ERR_INVALID_STATE;
    state.observation.system_query_status = CRUX_ERR_INVALID_STATE;
    state.observation.log_write_status = CRUX_ERR_INVALID_STATE;

    state.observation.log_level_status = crux_log_set_level(CRUX_LOG_INFO);
    if (state.observation.log_level_status != CRUX_OK) {
        fail_once(&state.observation, STAGE_LOG_LEVEL, state.observation.log_level_status);
        goto cleanup;
    }

    status = crux_mutex_create(&state.mutex);
    if (status != CRUX_OK) {
        fail_once(&state.observation, STAGE_MUTEX_CREATE, status);
        goto cleanup;
    }
    state.mutex_created = true;

    status = crux_semaphore_create(0, 1, &state.handoff);
    if (status != CRUX_OK) {
        fail_once(&state.observation, STAGE_SEMAPHORE_CREATE, status);
        goto cleanup;
    }
    state.semaphore_created = true;

    state.observation.empty_poll_status = crux_semaphore_take(state.handoff, 0);
    if (state.observation.empty_poll_status != CRUX_ERR_BUSY) {
        fail_once(&state.observation, STAGE_EMPTY_POLL, state.observation.empty_poll_status);
        goto cleanup;
    }

    options.priority = CRUX_THREAD_PRIORITY_NORMAL;
    status = crux_thread_create(&options, worker_entry, &state, &state.thread);
    if (status != CRUX_OK) {
        fail_once(&state.observation, STAGE_THREAD_CREATE, status);
        goto cleanup;
    }
    state.thread_created = true;

    state.observation.handoff_wait_status =
        crux_semaphore_take(state.handoff, OPERATION_TIMEOUT_MS);
    if (state.observation.handoff_wait_status != CRUX_OK) {
        fail_once(&state.observation, STAGE_HANDOFF_WAIT, state.observation.handoff_wait_status);
    }

    status = crux_thread_join(&state.thread, OPERATION_TIMEOUT_MS);
    if (status != CRUX_OK) {
        fail_once(&state.observation, STAGE_THREAD_JOIN, status);
        *out = state.observation;
        return state.observation.failure_status;
    }
    state.thread_created = false;

    if (state.observation.worker_status != CRUX_OK) {
        fail_once(&state.observation, state.observation.worker_failure_stage,
                  state.observation.worker_status);
    }
    if (state.observation.handoff_give_status != CRUX_OK) {
        fail_once(&state.observation, STAGE_HANDOFF_GIVE, state.observation.handoff_give_status);
    }

    if (state.observation.failure_status == CRUX_OK) {
        status = crux_mutex_lock(state.mutex, OPERATION_TIMEOUT_MS);
        if (status != CRUX_OK) {
            fail_once(&state.observation, STAGE_MAIN_MUTEX_LOCK, status);
        } else {
            state.observation.value = state.value;
            status = crux_mutex_unlock(state.mutex);
            if (status != CRUX_OK)
                fail_once(&state.observation, STAGE_MAIN_MUTEX_UNLOCK, status);
            else if (state.observation.value != EXPECTED_WORKER_VALUE)
                fail_once(&state.observation, STAGE_WORKER_VALUE, CRUX_ERR_IO);
        }
    }

cleanup:
    if (!state.thread_created && state.semaphore_created) {
        status = crux_semaphore_destroy(&state.handoff);
        state.semaphore_created = false;
        if (status != CRUX_OK)
            fail_once(&state.observation, STAGE_SEMAPHORE_DESTROY, status);
    }
    if (!state.thread_created && state.mutex_created) {
        status = crux_mutex_destroy(&state.mutex);
        state.mutex_created = false;
        if (status != CRUX_OK)
            fail_once(&state.observation, STAGE_MUTEX_DESTROY, status);
    }

    *out = state.observation;
    return state.observation.failure_status;
}

static const char *stage_name(operation_stage_t stage)
{
    static const char *const names[] = {
        "none",          "log-level",         "mutex-create", "semaphore-create", "empty-poll",
        "thread-create", "handoff-wait",      "thread-join",  "worker-lock",      "worker-unlock",
        "system-status", "log-write",         "handoff-give", "main-lock",        "main-unlock",
        "worker-value",  "semaphore-destroy", "mutex-destroy"};
    return (size_t)stage < ARRAY_SIZE(names) ? names[stage] : "unknown";
}

#if defined(CONFIG_BOARD_NATIVE_SIM)
static const struct device *const logging_uart = DEVICE_DT_GET(DT_NODELABEL(m5_logging_uart));
static struct k_mutex capture_mutex;
static char log_capture[1024];
static size_t log_capture_size;
static bool log_capture_overflow;

static void collect_log_bytes(const struct device *device, size_t size, void *unused)
{
    uint8_t bytes[128];
    uint32_t count;

    ARG_UNUSED(size);
    ARG_UNUSED(unused);
    k_mutex_lock(&capture_mutex, K_FOREVER);
    while ((count = uart_emul_get_tx_data(device, bytes, sizeof(bytes))) != 0) {
        if (log_capture_size + count >= sizeof(log_capture)) {
            log_capture_overflow = true;
            break;
        }
        memcpy(&log_capture[log_capture_size], bytes, count);
        log_capture_size += count;
        log_capture[log_capture_size] = '\0';
    }
    k_mutex_unlock(&capture_mutex);
}

static bool has_complete_record(void)
{
    bool complete;
    k_mutex_lock(&capture_mutex, K_FOREVER);
    const char *record = strstr(log_capture, LOG_RECORD);
    complete = record != NULL && strchr(record, '\n') != NULL;
    k_mutex_unlock(&capture_mutex);
    return complete;
}

static bool capture_overflowed(void)
{
    k_mutex_lock(&capture_mutex, K_FOREVER);
    bool overflowed = log_capture_overflow;
    k_mutex_unlock(&capture_mutex);
    return overflowed;
}

static void *setup(void)
{
    k_mutex_init(&capture_mutex);
    zassert_true(device_is_ready(logging_uart), "native Logging UART emulator is not ready");
    uart_emul_callback_tx_data_ready_set(logging_uart, collect_log_bytes, NULL);
    return NULL;
}
#endif

ZTEST(m5_integrated, test_thread_sync_logging_and_system_coexist)
{
    integration_observation_t observation;
    crux_status_t status = run_integration(&observation);
    zassert_equal(status, CRUX_OK, "integration failed at %s: status=%d",
                  stage_name(observation.failure_stage), (int)observation.failure_status);
    zassert_equal(observation.log_level_status, CRUX_OK);
    zassert_equal(observation.empty_poll_status, CRUX_ERR_BUSY,
                  "empty semaphore poll must return BUSY immediately");
    zassert_equal(observation.handoff_wait_status, CRUX_OK, "worker did not signal within %u ms",
                  OPERATION_TIMEOUT_MS);
    zassert_equal(observation.handoff_give_status, CRUX_OK);
    zassert_equal(observation.worker_status, CRUX_OK, "worker failed at %s: status=%d",
                  stage_name(observation.worker_failure_stage), (int)observation.worker_status);
    zassert_equal(observation.system_query_status, CRUX_OK,
                  "non-destructive System status query failed: %d",
                  (int)observation.system_query_status);
    zassert_equal(observation.system.watchdog_status, CRUX_ERR_NOT_SUPPORTED,
                  "native_sim has no watchdog device");
    zassert_equal(observation.system.watchdog_state, CRUX_WATCHDOG_UNAVAILABLE);
    zassert_false(observation.system.watchdog_emulated);
    zassert_equal(observation.system.watchdog_timeout_ms, 0);
    zassert_equal(observation.system.reset_status, CRUX_OK);
    zassert_equal(observation.system.reset_cause_status, CRUX_OK);
    zassert_equal(observation.value, EXPECTED_WORKER_VALUE,
                  "mutex-protected worker result was not handed back");
    zassert_equal(observation.log_write_status, CRUX_OK, "portable Logging submission failed: %d",
                  (int)observation.log_write_status);

#if defined(CONFIG_BOARD_NATIVE_SIM)
    bool complete = false;
    for (unsigned attempt = 0; attempt < 200 && !complete; ++attempt) {
        complete = has_complete_record();
        if (!complete)
            k_sleep(K_MSEC(10));
    }
    zassert_false(capture_overflowed(), "native Logging capture overflowed");
    zassert_true(complete, "missing complete native UART record: %s", LOG_RECORD);
#endif
}

#if defined(CONFIG_BOARD_NATIVE_SIM)
ZTEST_SUITE(m5_integrated, NULL, setup, NULL, NULL, NULL);
#else
ZTEST_SUITE(m5_integrated, NULL, NULL, NULL, NULL, NULL);
#endif
