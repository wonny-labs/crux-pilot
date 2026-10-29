#include <crux/logging.h>
#include <crux/queue.h>
#include <crux/sync.h>
#include <crux/system.h>
#include <crux/thread.h>
#include <crux/time.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#if defined(CONFIG_BOARD_NATIVE_SIM)
#include <zephyr/devicetree.h>
#include <zephyr/drivers/serial/uart_emul.h>
#endif

#define OPERATION_TIMEOUT_MS 5000U
#define EXPECTED_WORKER_VALUE UINT32_C(0x4d354d35)
#define LOG_RECORD "worker-progress queue-copy=queued deadline=observed system-query=ok"
#if defined(CONFIG_BOARD_NATIVE_SIM)
#define TARGET_NAME "native_sim"
#elif defined(CONFIG_BOARD_MIMXRT1060_EVK)
#define TARGET_NAME "evkb_b"
#else
#error "M5 smoke supports only native_sim and EVKB Revision B"
#endif

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
    STAGE_MUTEX_DESTROY,
    STAGE_CLOCK_START,
    STAGE_CLOCK_DEADLINE,
    STAGE_CLOCK_SLEEP,
    STAGE_CLOCK_CHECK,
    STAGE_CLOCK_END,
    STAGE_QUEUE_CREATE,
    STAGE_QUEUE_SEND,
    STAGE_QUEUE_RECEIVE,
    STAGE_QUEUE_FIFO,
    STAGE_QUEUE_DESTROY,
    STAGE_SYSTEM_EXPECTATION,
    STAGE_STACK_OBSERVATION,
    STAGE_LOG_BEGIN,
    STAGE_LOG_CAPTURE,
    STAGE_LOG_OBSERVATION,
    STAGE_LOG_RESULT,
    STAGE_COUNT
} operation_stage_t;

typedef struct {
    uint32_t sequence;
    uint32_t value;
} queue_record_t;

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
    crux_monotonic_time_t started;
    int64_t elapsed_ns;
    int64_t worker_wait_ns;
    int stack_status;
    size_t stack_unused_bytes;
    size_t stack_size_bytes;
} integration_observation_t;

typedef struct {
    crux_mutex_t mutex;
    crux_semaphore_t handoff;
    crux_thread_t thread;
    crux_queue_t queue;
    bool mutex_created;
    bool semaphore_created;
    bool thread_created;
    bool queue_created;
    uint32_t value;
    integration_observation_t observation;
} integration_state_t;

static integration_state_t state;

static void fail_once(integration_observation_t *observation, operation_stage_t stage,
                      crux_status_t status)
{
    if (observation->failure_status == CRUX_OK) {
        observation->failure_stage = stage;
        observation->failure_status = status == CRUX_OK ? CRUX_ERR_INVALID_STATE : status;
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

    queue_record_t record = {1, EXPECTED_WORKER_VALUE};
    for (uint32_t sequence = 1; sequence <= 2; ++sequence) {
        record.sequence = sequence;
        record.value = EXPECTED_WORKER_VALUE + sequence;
        status =
            crux_queue_send(worker_state->queue, &record, sizeof(record), OPERATION_TIMEOUT_MS);
        if (status != CRUX_OK) {
            observation->worker_failure_stage = STAGE_QUEUE_SEND;
            observation->worker_status = status;
            goto signal_completion;
        }
    }
    /* The queued values must survive both source-buffer mutation and worker exit. */
    memset(&record, 0, sizeof(record));

    crux_monotonic_time_t before, deadline, after;
    status = crux_monotonic_now(&before);
    if (status == CRUX_OK)
        status = crux_monotonic_deadline_after(crux_duration_from_ms(2), &deadline);
    if (status != CRUX_OK) {
        observation->worker_failure_stage = STAGE_CLOCK_DEADLINE;
        observation->worker_status = status;
        goto signal_completion;
    }
    status = crux_thread_sleep_until(deadline);
    if (status != CRUX_OK) {
        observation->worker_failure_stage = STAGE_CLOCK_SLEEP;
        observation->worker_status = status;
        goto signal_completion;
    }
    status = crux_monotonic_now(&after);
    if (status != CRUX_OK || !crux_monotonic_is_at_or_before(deadline, after)) {
        observation->worker_failure_stage = STAGE_CLOCK_CHECK;
        observation->worker_status = status == CRUX_OK ? CRUX_ERR_IO : status;
        goto signal_completion;
    }
    observation->worker_wait_ns = crux_duration_to_ns(crux_monotonic_diff(after, before));

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
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
    /* Sample the current worker before join can release/reuse its stack. */
    observation->stack_size_bytes = k_current_get()->stack_info.size;
    observation->stack_status =
        k_thread_stack_space_get(k_current_get(), &observation->stack_unused_bytes);
    if (observation->worker_status == CRUX_OK && observation->stack_status == 0 &&
        (observation->stack_size_bytes == 0 ||
         observation->stack_unused_bytes > observation->stack_size_bytes)) {
        observation->worker_failure_stage = STAGE_STACK_OBSERVATION;
        observation->worker_status = CRUX_ERR_IO;
    }
#endif
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
    state.observation.stack_status = -ENOTSUP;
    state.observation.elapsed_ns = -1;
    state.observation.worker_wait_ns = -1;

    status = crux_monotonic_now(&state.observation.started);
    if (status != CRUX_OK) {
        fail_once(&state.observation, STAGE_CLOCK_START, status);
        goto cleanup;
    }

    state.observation.log_level_status = crux_log_set_level(CRUX_LOG_INFO);
    if (state.observation.log_level_status != CRUX_OK) {
        fail_once(&state.observation, STAGE_LOG_LEVEL, state.observation.log_level_status);
        goto cleanup;
    }
    status =
        crux_log_write(CRUX_LOG_INFO, "m5-integration", "M5_SMOKE_BEGIN_V1 target=" TARGET_NAME);
    if (status != CRUX_OK) {
        fail_once(&state.observation, STAGE_LOG_BEGIN, status);
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

    status = crux_queue_create(2, sizeof(queue_record_t), &state.queue);
    if (status != CRUX_OK) {
        fail_once(&state.observation, STAGE_QUEUE_CREATE, status);
        goto cleanup;
    }
    state.queue_created = true;

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
        /* A failed join retains all resources. Do not race a still-live worker
         * by copying its observations or reclaiming its argument/stack. */
        *out = (integration_observation_t){.failure_stage = state.observation.failure_stage,
                                           .failure_status = state.observation.failure_status,
                                           .system_query_status = CRUX_ERR_INVALID_STATE,
                                           .elapsed_ns = -1,
                                           .worker_wait_ns = -1,
                                           .stack_status = -ENOTSUP};
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
        for (uint32_t sequence = 1; sequence <= 2; ++sequence) {
            queue_record_t record = {0};
            status = crux_queue_receive(state.queue, &record, sizeof(record), OPERATION_TIMEOUT_MS);
            if (status != CRUX_OK) {
                fail_once(&state.observation, STAGE_QUEUE_RECEIVE, status);
                break;
            }
            if (record.sequence != sequence || record.value != EXPECTED_WORKER_VALUE + sequence) {
                fail_once(&state.observation, STAGE_QUEUE_FIFO, CRUX_ERR_IO);
                break;
            }
        }
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
    if (!state.thread_created && state.queue_created) {
        status = crux_queue_destroy(&state.queue);
        if (status == CRUX_OK)
            state.queue_created = false;
        else
            fail_once(&state.observation, STAGE_QUEUE_DESTROY, status);
    }
    if (!state.thread_created && state.semaphore_created) {
        status = crux_semaphore_destroy(&state.handoff);
        if (status == CRUX_OK)
            state.semaphore_created = false;
        else
            fail_once(&state.observation, STAGE_SEMAPHORE_DESTROY, status);
    }
    if (!state.thread_created && state.mutex_created) {
        status = crux_mutex_destroy(&state.mutex);
        if (status == CRUX_OK)
            state.mutex_created = false;
        else
            fail_once(&state.observation, STAGE_MUTEX_DESTROY, status);
    }

    if (state.observation.failure_status == CRUX_OK) {
        crux_monotonic_time_t finished;
        status = crux_monotonic_now(&finished);
        if (status == CRUX_OK &&
            crux_monotonic_is_at_or_before(state.observation.started, finished))
            state.observation.elapsed_ns =
                crux_duration_to_ns(crux_monotonic_diff(finished, state.observation.started));
        else
            fail_once(&state.observation, STAGE_CLOCK_END,
                      status == CRUX_OK ? CRUX_ERR_IO : status);
    }

    *out = state.observation;
    return state.observation.failure_status;
}

static const char *stage_name(operation_stage_t stage)
{
    static const char *const names[] = {"none",
                                        "log-level",
                                        "mutex-create",
                                        "semaphore-create",
                                        "empty-poll",
                                        "thread-create",
                                        "handoff-wait",
                                        "thread-join",
                                        "worker-lock",
                                        "worker-unlock",
                                        "system-status",
                                        "log-write",
                                        "handoff-give",
                                        "main-lock",
                                        "main-unlock",
                                        "worker-value",
                                        "semaphore-destroy",
                                        "mutex-destroy",
                                        "clock-start",
                                        "clock-deadline",
                                        "clock-sleep",
                                        "clock-check",
                                        "clock-end",
                                        "queue-create",
                                        "queue-send",
                                        "queue-receive",
                                        "queue-fifo",
                                        "queue-destroy",
                                        "system-expectation",
                                        "stack-observation",
                                        "log-begin",
                                        "log-capture",
                                        "log-observation",
                                        "log-result"};
    BUILD_ASSERT(ARRAY_SIZE(names) == STAGE_COUNT);
    return (size_t)stage < ARRAY_SIZE(names) ? names[stage] : "unknown";
}

static bool system_report_is_expected(const crux_system_status_t *report, bool native_sim)
{
    return report->watchdog_status == (native_sim ? CRUX_ERR_NOT_SUPPORTED : CRUX_OK) &&
           report->watchdog_state ==
               (native_sim ? CRUX_WATCHDOG_UNAVAILABLE : CRUX_WATCHDOG_IDLE) &&
           !report->watchdog_emulated && report->watchdog_timeout_ms == 0 &&
           report->reset_status == CRUX_OK && report->reset_cause_status == CRUX_OK;
}

#if defined(CONFIG_BOARD_NATIVE_SIM)
static void show_native_capture(void);
#endif

static crux_status_t emit_result(bool passed, operation_stage_t stage, crux_status_t status,
                                 const integration_observation_t *observation)
{
    char record[CRUX_LOG_TEXT_MAX + 1];
    int length = snprintf(
        record, sizeof(record),
        "M5_SMOKE_RESULT_V1 result=%s target=%s stage=%s status=%d "
        "system_query=%d wd_status=%d wd_state=%d reset_status=%d cause_status=%d",
        passed ? "PASS" : "FAIL", TARGET_NAME, passed ? "complete" : stage_name(stage), (int)status,
        (int)observation->system_query_status, (int)observation->system.watchdog_status,
        (int)observation->system.watchdog_state, (int)observation->system.reset_status,
        (int)observation->system.reset_cause_status);
    if (length < 0 || (size_t)length >= sizeof(record))
        return CRUX_ERR_OUT_OF_RANGE;
    return crux_log_write(passed ? CRUX_LOG_INFO : CRUX_LOG_ERROR, "m5-integration", record);
}

static void require_observation(bool accepted, operation_stage_t stage, crux_status_t status,
                                const integration_observation_t *observation)
{
    if (accepted)
        return;
    crux_status_t failure = status == CRUX_OK ? CRUX_ERR_INVALID_STATE : status;
    if (emit_result(false, stage, failure, observation) == CRUX_OK)
        log_flush();
#if defined(CONFIG_BOARD_NATIVE_SIM)
    show_native_capture();
#endif
    zassert_true(accepted, "M5 smoke failed at %s: status=%d", stage_name(stage), (int)failure);
}

static crux_status_t emit_observations(const integration_observation_t *observation)
{
    char record[CRUX_LOG_TEXT_MAX + 1];
    int length = snprintf(record, sizeof(record),
                          "M5_SMOKE_OBS_V1 target=%s elapsed_ns=%" PRId64 " wait_ns=%" PRId64
                          " stack=%s stack_status=%d "
                          "unused_bytes=%zu usable_bytes=%zu",
                          TARGET_NAME, observation->elapsed_ns, observation->worker_wait_ns,
                          observation->stack_status == 0 ? "observed" : "unavailable",
                          observation->stack_status,
                          observation->stack_status == 0 ? observation->stack_unused_bytes : 0,
                          observation->stack_status == 0 ? observation->stack_size_bytes : 0);
    if (length < 0 || (size_t)length >= sizeof(record))
        return CRUX_ERR_OUT_OF_RANGE;
    return crux_log_write(CRUX_LOG_INFO, "m5-integration", record);
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

static bool has_complete_record(const char *text)
{
    bool complete;
    k_mutex_lock(&capture_mutex, K_FOREVER);
    const char *record = strstr(log_capture, text);
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

static void show_native_capture(void)
{
    k_mutex_lock(&capture_mutex, K_FOREVER);
    TC_PRINT("Captured native Logging UART:\n%s", log_capture);
    k_mutex_unlock(&capture_mutex);
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
    require_observation(status == CRUX_OK, observation.failure_stage, status, &observation);
    require_observation(observation.log_level_status == CRUX_OK, STAGE_LOG_LEVEL,
                        observation.log_level_status, &observation);
    require_observation(observation.empty_poll_status == CRUX_ERR_BUSY, STAGE_EMPTY_POLL,
                        observation.empty_poll_status, &observation);
    require_observation(observation.handoff_wait_status == CRUX_OK, STAGE_HANDOFF_WAIT,
                        observation.handoff_wait_status, &observation);
    require_observation(observation.handoff_give_status == CRUX_OK, STAGE_HANDOFF_GIVE,
                        observation.handoff_give_status, &observation);
    require_observation(observation.worker_status == CRUX_OK, observation.worker_failure_stage,
                        observation.worker_status, &observation);
    require_observation(observation.system_query_status == CRUX_OK, STAGE_SYSTEM_STATUS,
                        observation.system_query_status, &observation);
    require_observation(
        system_report_is_expected(&observation.system, IS_ENABLED(CONFIG_BOARD_NATIVE_SIM)),
        STAGE_SYSTEM_EXPECTATION, CRUX_ERR_INVALID_STATE, &observation);
    require_observation(observation.value == EXPECTED_WORKER_VALUE, STAGE_WORKER_VALUE, CRUX_ERR_IO,
                        &observation);
    require_observation(observation.log_write_status == CRUX_OK, STAGE_LOG_WRITE,
                        observation.log_write_status, &observation);

#if defined(CONFIG_BOARD_NATIVE_SIM)
    log_flush();
    require_observation(!capture_overflowed() && has_complete_record(LOG_RECORD) &&
                            has_complete_record("M5_SMOKE_BEGIN_V1 target=" TARGET_NAME),
                        STAGE_LOG_CAPTURE, CRUX_ERR_IO, &observation);
#endif

    status = emit_observations(&observation);
    require_observation(status == CRUX_OK, STAGE_LOG_OBSERVATION, status, &observation);
    /* Test-only native drain: process deferred records before checking capture
     * or returning. This has no hard latency bound and is not physical UART
     * delivery acknowledgement; the later board run still needs raw capture. */
    log_flush();
#if defined(CONFIG_BOARD_NATIVE_SIM)
    require_observation(!capture_overflowed() && has_complete_record("M5_SMOKE_OBS_V1"),
                        STAGE_LOG_OBSERVATION, CRUX_ERR_IO, &observation);
#endif
    /* All functional assertions and resource cleanup precede PASS submission.
     * CRUX_OK still does not establish UART delivery. Physical validation must
     * capture the complete BEGIN/OBS/PASS records and reject faults or resets. */
    status = emit_result(true, STAGE_NONE, CRUX_OK, &observation);
    require_observation(status == CRUX_OK, STAGE_LOG_RESULT, status, &observation);
    log_flush();
#if defined(CONFIG_BOARD_NATIVE_SIM)
    show_native_capture();
#endif
}

#if defined(CONFIG_BOARD_NATIVE_SIM)
ZTEST(m5_integrated, test_system_profile_rejects_unready_or_active_watchdog)
{
    crux_system_status_t simulator = {.watchdog_status = CRUX_ERR_NOT_SUPPORTED,
                                      .watchdog_state = CRUX_WATCHDOG_UNAVAILABLE,
                                      .reset_status = CRUX_OK,
                                      .reset_cause_status = CRUX_OK};
    crux_system_status_t evkb = {.watchdog_status = CRUX_OK,
                                 .watchdog_state = CRUX_WATCHDOG_IDLE,
                                 .reset_status = CRUX_OK,
                                 .reset_cause_status = CRUX_OK};
    zassert_true(system_report_is_expected(&simulator, true));
    zassert_true(system_report_is_expected(&evkb, false), "ready, idle EVKB watchdog rejected");
    zassert_false(system_report_is_expected(&simulator, false));
    zassert_false(system_report_is_expected(&evkb, true));
    evkb.watchdog_status = CRUX_ERR_NOT_READY;
    zassert_false(system_report_is_expected(&evkb, false));
    evkb.watchdog_status = CRUX_ERR_IO;
    evkb.watchdog_state = CRUX_WATCHDOG_FAULTED;
    zassert_false(system_report_is_expected(&evkb, false));
    evkb.watchdog_status = CRUX_OK;
    evkb.watchdog_state = CRUX_WATCHDOG_ACTIVE;
    zassert_false(system_report_is_expected(&evkb, false));
    evkb.watchdog_state = CRUX_WATCHDOG_IDLE;
    evkb.watchdog_emulated = true;
    zassert_false(system_report_is_expected(&evkb, false));
    evkb.watchdog_emulated = false;
    evkb.watchdog_timeout_ms = 2000;
    zassert_false(system_report_is_expected(&evkb, false));
    evkb.watchdog_timeout_ms = 0;
    evkb.reset_cause_status = CRUX_ERR_IO;
    zassert_false(system_report_is_expected(&evkb, false));
    evkb.reset_cause_status = CRUX_OK;
    evkb.reset_status = CRUX_ERR_IO;
    zassert_false(system_report_is_expected(&evkb, false));
}

ZTEST(m5_integrated, test_unexpected_success_stays_a_failure)
{
    integration_observation_t observation = {0};
    fail_once(&observation, STAGE_EMPTY_POLL, CRUX_OK);
    zassert_equal(observation.failure_status, CRUX_ERR_INVALID_STATE);
    zassert_equal(observation.failure_stage, STAGE_EMPTY_POLL);
    fail_once(&observation, STAGE_QUEUE_DESTROY, CRUX_ERR_IO);
    zassert_equal(observation.failure_status, CRUX_ERR_INVALID_STATE);
    zassert_equal(observation.failure_stage, STAGE_EMPTY_POLL);
}

ZTEST_SUITE(m5_integrated, NULL, setup, NULL, NULL, NULL);
#else
ZTEST_SUITE(m5_integrated, NULL, NULL, NULL, NULL, NULL);
#endif
