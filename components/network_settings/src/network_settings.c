/* The network settings service: the boot's decision and apply, the state, and the save with its
 * order. See docs/network-settings-service.md.
 *
 * The five parameters belong to the Configuration service once it has started, so this service
 * reads, updates and saves them through its application calls and never through the mechanism. A
 * save has three phases, each a save of its own, and so each a point where the power can fail:
 *
 *   1. the address, prefix length and gateway,
 *   2. the check key over those three,
 *   3. the `use_saved` flag, set to true.
 *
 * The check key and the flag are updated in memory and written only after the phase before them
 * has been acknowledged, whatever the order of the table. A set that is made of parts of two sets
 * therefore never has a check key that matches it, whether the cause is a power loss, a failed
 * write, or another session's save of the configuration landing between two steps. */
#include "network_settings_internal.h"
#include "network_settings_port.h"

#include <crux/sync.h>
#include <crux/thread.h>

#include <string.h>

#ifndef CRUX_NETWORK_SETTINGS_LOCK_WAIT_MS
/* The longest a server-thread call waits for the state's lock, which is held only to copy. */
#define CRUX_NETWORK_SETTINGS_LOCK_WAIT_MS 20u
#endif
#ifndef CRUX_NETWORK_SETTINGS_BUSY_WAIT_MS
/* How long a save waits for the Configuration service while another session's save holds it. A
 * save waits up to CRUX_CONFIG_SERVICE_SAVE_WAIT_MS, 5 s, so this is longer than one save. */
#define CRUX_NETWORK_SETTINGS_BUSY_WAIT_MS 7000u
#endif
#ifndef CRUX_NETWORK_SETTINGS_BUSY_STEP_MS
#define CRUX_NETWORK_SETTINGS_BUSY_STEP_MS 25u
#endif
#ifndef CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS
/* How long the worker waits for its signal, and for the operation lock when it needs it. */
#define CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS 1000u
#endif
#ifndef CRUX_NETWORK_SETTINGS_OP_WAIT_MS
/* How long a recovery waits for the save that holds the operation lock. */
#define CRUX_NETWORK_SETTINGS_OP_WAIT_MS 15000u
#endif

#define SCHEMA_VERSION 1u

enum { P_ADDRESS, P_PREFIX, P_GATEWAY, P_CHECK, P_FLAG, P_COUNT };
_Static_assert(P_COUNT == CRUX_NETWORK_SETTINGS_ENTRY_COUNT, "the entry count names the five");

#define U32_ZERO {.type = CRUX_CONFIG_U32, .data.u32 = 0u}
/* Every parameter's default is "nothing saved": zero, and false. */
static const crux_config_spec_t specs[P_COUNT] = {
    {CRUX_NETWORK_SETTINGS_KEY_ADDRESS, SCHEMA_VERSION, U32_ZERO, NULL, NULL},
    {CRUX_NETWORK_SETTINGS_KEY_PREFIX, SCHEMA_VERSION, U32_ZERO, NULL, NULL},
    {CRUX_NETWORK_SETTINGS_KEY_GATEWAY, SCHEMA_VERSION, U32_ZERO, NULL, NULL},
    {CRUX_NETWORK_SETTINGS_KEY_CHECK, SCHEMA_VERSION, U32_ZERO, NULL, NULL},
    {CRUX_NETWORK_SETTINGS_KEY_USE_SAVED,
     SCHEMA_VERSION,
     {.type = CRUX_CONFIG_BOOL, .data.boolean = false},
     NULL,
     NULL},
};

static crux_config_param_t params[P_COUNT];

/* The order is part of the contract, and the saves do not depend on it. The permission is one that
 * no role holds, so that no session lists or changes a parameter alone. */
static const crux_config_service_entry_t entries[P_COUNT] = {
#define ENTRY(index)                                                                               \
    {&params[index], CRUX_NETWORK_SETTINGS_PERMISSION_PRIVATE,                                     \
     CRUX_NETWORK_SETTINGS_PERMISSION_PRIVATE, false, CRUX_CONFIG_ACTIVATION_RESTART}
    ENTRY(P_ADDRESS), ENTRY(P_PREFIX), ENTRY(P_GATEWAY), ENTRY(P_CHECK), ENTRY(P_FLAG),
#undef ENTRY
};

typedef enum { JOB_NONE, JOB_QUEUED, JOB_RUNNING, JOB_FAILED } job_t;

static struct {
    bool booted;
    bool started;
    bool persistence;
    crux_network_settings_config_t config;
    crux_network_boot_report_t boot;
    bool stored_valid; /* The next boot would apply `stored`. */
    crux_network_settings_t stored;
    job_t job;
    bool recovering;
    crux_network_failure_t failure;
    crux_network_settings_t pending;
    crux_access_session_t session;
} svc;

/* The lock of `svc`, held only to copy. The operation lock serialises what writes: a save and a
 * recovery. Neither is held across a call that waits, except the operation lock across storage. */
static crux_mutex_t state_lock;
static crux_mutex_t op_lock;
static crux_semaphore_t work_signal;
static crux_semaphore_t sleeper; /* Never given, so that taking it with a timeout is a sleep. */
static bool sync_created;
static bool worker_started;
static crux_thread_t worker_thread;

const crux_config_service_entry_t *crux_network_settings_entries(size_t *count)
{
    if (count != NULL) {
        *count = P_COUNT;
    }
    return entries;
}

/* The set as it is stored, with zero for no gateway, and as a client sees it. */
static crux_network_settings_t normalised(const crux_network_settings_t *settings)
{
    crux_network_settings_t result = *settings;

    if (!result.has_gateway) {
        result.gateway = 0u;
    }
    return result;
}

static bool same(const crux_network_settings_t *a, const crux_network_settings_t *b)
{
    crux_network_settings_t x = normalised(a);
    crux_network_settings_t y = normalised(b);

    return x.address == y.address && x.prefix_length == y.prefix_length &&
           x.has_gateway == y.has_gateway && x.gateway == y.gateway;
}

/* The decision. */

static bool missing(crux_config_result_t load)
{
    return load == CRUX_CONFIG_DEFAULT_MISSING;
}

crux_network_boot_reason_t network_decide(const network_stored_t *stored,
                                          crux_network_settings_t *values)
{
    const crux_config_result_t loads[P_COUNT] = {stored->address_load, stored->prefix_load,
                                                 stored->gateway_load, stored->check_load,
                                                 stored->flag_load};
    size_t missing_count = 0u;
    bool rejected = false;
    bool error = false;
    crux_network_settings_t candidate;

    for (size_t index = 0; index < P_COUNT; ++index) {
        if (loads[index] == CRUX_CONFIG_ERR_NOT_SUPPORTED) {
            return CRUX_NETWORK_BOOT_NOT_SUPPORTED;
        }
        error = error || loads[index] < 0;
        rejected = rejected || loads[index] == CRUX_CONFIG_DEFAULT_REJECTED;
        missing_count += missing(loads[index]) ? 1u : 0u;
    }
    if (error) {
        return CRUX_NETWORK_BOOT_STORE_ERROR;
    }
    if (missing_count == P_COUNT) {
        return CRUX_NETWORK_BOOT_NOT_STORED;
    }
    if (rejected) {
        return CRUX_NETWORK_BOOT_RECORD_INVALID;
    }
    /* The recovery command's flag, which is a record like the others. */
    if (stored->flag_load == CRUX_CONFIG_OK && !stored->use_saved) {
        return CRUX_NETWORK_BOOT_DISABLED;
    }
    if (missing_count != 0u) {
        return CRUX_NETWORK_BOOT_INCOMPLETE;
    }
    candidate.address = stored->address;
    candidate.prefix_length = stored->prefix_length;
    candidate.has_gateway = stored->gateway != 0u;
    candidate.gateway = stored->gateway;
    if (!crux_network_settings_validate(&candidate, NULL)) {
        return CRUX_NETWORK_BOOT_VALUES_INVALID;
    }
    if (crux_network_settings_check_key(&candidate) != stored->check) {
        return CRUX_NETWORK_BOOT_CHECK_MISMATCH;
    }
    *values = candidate;
    return CRUX_NETWORK_BOOT_SAVED;
}

/* Loads the five parameters and reads what they then hold. A load that falls back leaves the
 * default, and one that fails leaves what the parameter held, which is the default too. */
static void load_stored(crux_config_param_t *set, network_stored_t *stored)
{
    crux_config_value_t value;
    uint32_t numbers[P_COUNT] = {0};
    bool flag = false;

    memset(stored, 0, sizeof(*stored));
    stored->address_load = crux_config_load(&set[P_ADDRESS]);
    stored->prefix_load = crux_config_load(&set[P_PREFIX]);
    stored->gateway_load = crux_config_load(&set[P_GATEWAY]);
    stored->check_load = crux_config_load(&set[P_CHECK]);
    stored->flag_load = crux_config_load(&set[P_FLAG]);
    for (size_t index = 0; index < P_COUNT; ++index) {
        if (crux_config_read(&set[index], &value) == CRUX_CONFIG_OK) {
            if (value.type == CRUX_CONFIG_U32) {
                numbers[index] = value.data.u32;
            } else if (value.type == CRUX_CONFIG_BOOL) {
                flag = value.data.boolean;
            }
        }
    }
    stored->address = numbers[P_ADDRESS];
    stored->prefix_length = numbers[P_PREFIX];
    stored->gateway = numbers[P_GATEWAY];
    stored->check = numbers[P_CHECK];
    stored->use_saved = flag;
}

static bool init_params(crux_config_param_t *set)
{
    memset(set, 0, sizeof(set[0]) * P_COUNT);
    for (size_t index = 0; index < P_COUNT; ++index) {
        if (crux_config_init(&set[index], &specs[index]) != CRUX_CONFIG_OK) {
            return false;
        }
    }
    return true;
}

#ifdef CRUX_NETWORK_SETTINGS_TESTING
crux_network_boot_reason_t network_test_reboot(crux_network_settings_t *values)
{
    static crux_config_param_t scratch[P_COUNT];
    network_stored_t stored;
    crux_network_settings_t decided = {0};
    crux_network_boot_reason_t reason;

    if (!init_params(scratch)) {
        return CRUX_NETWORK_BOOT_STORE_ERROR;
    }
    load_stored(scratch, &stored);
    reason = network_decide(&stored, &decided);
    if (values != NULL) {
        *values = decided;
    }
    return reason;
}
#endif

crux_network_result_t crux_network_settings_boot(const crux_network_settings_config_t *config,
                                                 crux_network_boot_report_t *report)
{
    network_stored_t stored;
    crux_network_settings_t decided = {0};
    crux_network_settings_t defaults;
    crux_network_boot_reason_t reason;

    if (config == NULL || !crux_network_settings_validate(&config->defaults, NULL)) {
        return CRUX_NETWORK_ERR_INVALID_ARGUMENT;
    }
    defaults = normalised(&config->defaults);
    /* Only a Host test boots twice, and before the Configuration service starts. */
    memset(&svc, 0, sizeof(svc));
    svc.config.defaults = defaults;
    if (!init_params(params)) {
        return CRUX_NETWORK_ERR_NOT_READY;
    }
    load_stored(params, &stored);
    reason = network_decide(&stored, &decided);
    svc.persistence = reason != CRUX_NETWORK_BOOT_NOT_SUPPORTED;
    svc.stored_valid = reason == CRUX_NETWORK_BOOT_SAVED;
    svc.stored = decided;

    svc.boot.source = CRUX_NETWORK_SOURCE_DEFAULT;
    svc.boot.persistence = svc.persistence;
    svc.boot.reason = reason;
    svc.boot.values = defaults;
    if (reason == CRUX_NETWORK_BOOT_SAVED) {
        svc.boot.values = decided;
        if (crux_network_settings_port_apply(&decided)) {
            svc.boot.source = CRUX_NETWORK_SOURCE_SAVED;
            svc.boot.applied = true;
        } else {
            /* The interface refused them, so the defaults run. The values stay saved. */
            svc.boot.reason = CRUX_NETWORK_BOOT_APPLY_FAILED;
            svc.boot.values = defaults;
            svc.boot.applied = crux_network_settings_port_apply(&defaults);
        }
    } else {
        svc.boot.applied = crux_network_settings_port_apply(&defaults);
    }
    svc.booted = true;
    if (report != NULL) {
        *report = svc.boot;
    }
    return CRUX_NETWORK_OK;
}

void crux_network_settings_boot_report(crux_network_boot_report_t *report)
{
    if (report != NULL) {
        *report = svc.boot;
    }
}

/* Start. */

static bool expect_parameter(const char *key, crux_config_type_t type)
{
    crux_config_value_t value;

    return crux_config_service_try_read(key, &value, NULL, 0u) == CRUX_CONFIG_SERVICE_OK &&
           value.type == type;
}

crux_network_result_t crux_network_settings_start(void)
{
    if (!svc.booted) {
        return CRUX_NETWORK_ERR_NOT_READY;
    }
    if (svc.started) {
        return CRUX_NETWORK_OK;
    }
    for (size_t index = 0; index < P_COUNT; ++index) {
        if (!expect_parameter(specs[index].key, specs[index].default_value.type)) {
            return CRUX_NETWORK_ERR_NOT_READY;
        }
    }
    if (!sync_created) {
        if (crux_mutex_create(&state_lock) != CRUX_OK) {
            return CRUX_NETWORK_ERR_NOT_READY;
        }
        if (crux_mutex_create(&op_lock) != CRUX_OK ||
            crux_semaphore_create(0u, 1u, &work_signal) != CRUX_OK ||
            crux_semaphore_create(0u, 1u, &sleeper) != CRUX_OK) {
            return CRUX_NETWORK_ERR_NOT_READY;
        }
        sync_created = true;
    }
    svc.started = true;
    return CRUX_NETWORK_OK;
}

/* Permission. A session that lacks it is denied by Access, which records the denial. */

static crux_network_result_t check_permission(crux_access_session_t session, const char *permission)
{
    switch (crux_access_check(session, permission)) {
    case CRUX_ACCESS_OK:
        return CRUX_NETWORK_OK;
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        return CRUX_NETWORK_ERR_SESSION_INVALID;
    case CRUX_ACCESS_ERR_NOT_READY:
        return CRUX_NETWORK_ERR_NOT_READY;
    default:
        return CRUX_NETWORK_ERR_DENIED;
    }
}

static bool lock_state(uint32_t wait_ms)
{
    return crux_mutex_lock(state_lock, wait_ms) == CRUX_OK;
}

static void unlock_state(void)
{
    (void)crux_mutex_unlock(state_lock);
}

/* GET. */

crux_network_result_t crux_network_settings_get(crux_access_session_t session,
                                                crux_network_state_t *state)
{
    crux_network_state_t result;
    crux_network_settings_t next;
    crux_network_result_t allowed;

    if (!svc.started) {
        return CRUX_NETWORK_ERR_NOT_READY;
    }
    allowed = check_permission(session, CRUX_NETWORK_SETTINGS_PERMISSION_READ);
    if (allowed != CRUX_NETWORK_OK) {
        return allowed;
    }
    if (state == NULL) {
        return CRUX_NETWORK_ERR_INVALID_ARGUMENT;
    }
    memset(&result, 0, sizeof(result));
    if (!lock_state(CRUX_NETWORK_SETTINGS_LOCK_WAIT_MS)) {
        return CRUX_NETWORK_ERR_BUSY;
    }
    result.source = svc.boot.source;
    result.has_saved = svc.stored_valid;
    result.saved = svc.stored;
    result.save = svc.job == JOB_FAILED ? CRUX_NETWORK_SAVE_FAILED
                  : (svc.job == JOB_QUEUED || svc.job == JOB_RUNNING || svc.recovering)
                      ? CRUX_NETWORK_SAVE_PENDING
                      : CRUX_NETWORK_SAVE_NONE;
    result.failure = svc.job == JOB_FAILED ? svc.failure : CRUX_NETWORK_FAILURE_NONE;
    next = svc.stored_valid ? svc.stored : svc.config.defaults;
    unlock_state();

    /* The interface answers outside the lock: the port is native code of the composition. */
    result.has_running = crux_network_settings_port_running(&result.running);
    if (result.has_running) {
        result.running = normalised(&result.running);
    } else {
        memset(&result.running, 0, sizeof(result.running));
    }
    result.reboot_required = result.has_running && !same(&next, &result.running);
    *state = result;
    return CRUX_NETWORK_OK;
}

/* PUT. */

crux_network_result_t crux_network_settings_request(crux_access_session_t session,
                                                    const crux_network_settings_t *settings,
                                                    crux_network_findings_t *findings)
{
    crux_network_result_t allowed;

    if (findings != NULL) {
        findings->count = 0u;
    }
    if (!svc.started) {
        return CRUX_NETWORK_ERR_NOT_READY;
    }
    allowed = check_permission(session, CRUX_NETWORK_SETTINGS_PERMISSION_WRITE);
    if (allowed != CRUX_NETWORK_OK) {
        return allowed;
    }
    if (settings == NULL) {
        return CRUX_NETWORK_ERR_INVALID_ARGUMENT;
    }
    if (!crux_network_settings_validate(settings, findings)) {
        return CRUX_NETWORK_ERR_REJECTED;
    }
    if (!svc.persistence) {
        return CRUX_NETWORK_ERR_NOT_SUPPORTED;
    }
    if (!lock_state(CRUX_NETWORK_SETTINGS_LOCK_WAIT_MS)) {
        return CRUX_NETWORK_ERR_BUSY;
    }
    if (svc.job == JOB_QUEUED || svc.job == JOB_RUNNING || svc.recovering) {
        unlock_state();
        return CRUX_NETWORK_ERR_BUSY;
    }
    svc.job = JOB_QUEUED;
    svc.failure = CRUX_NETWORK_FAILURE_NONE;
    svc.pending = normalised(settings);
    svc.session = session;
    unlock_state();
    (void)crux_semaphore_give(work_signal);
    return CRUX_NETWORK_OK;
}

/* The save. */

static crux_network_failure_t failure_of_update(crux_config_service_result_t result)
{
    return result == CRUX_CONFIG_SERVICE_ERR_BUSY ? CRUX_NETWORK_FAILURE_BUSY
                                                  : CRUX_NETWORK_FAILURE_NOT_READY;
}

/* Updates one parameter in memory, waiting out another session's save for a bounded time. Nothing
 * is lost by waiting: no earlier step of this save is unsaved when a phase begins. */
static crux_config_service_result_t update_parameter(size_t index, uint32_t number, bool flag)
{
    crux_config_value_t candidate = {.type = specs[index].default_value.type};
    uint32_t waited = 0u;
    crux_config_service_result_t result;

    if (candidate.type == CRUX_CONFIG_BOOL) {
        candidate.data.boolean = flag;
    } else {
        candidate.data.u32 = number;
    }
    for (;;) {
        result = crux_config_service_try_update(specs[index].key, &candidate);
        if (result != CRUX_CONFIG_SERVICE_ERR_BUSY ||
            waited >= CRUX_NETWORK_SETTINGS_BUSY_WAIT_MS) {
            return result;
        }
        (void)crux_semaphore_take(sleeper, CRUX_NETWORK_SETTINGS_BUSY_STEP_MS);
        waited += CRUX_NETWORK_SETTINGS_BUSY_STEP_MS;
    }
}

/* Saves every mark, which is what the Configuration service offers: the marks of this phase, and
 * any that another part of the image made. */
static crux_network_failure_t save_marks(void)
{
    crux_config_service_save_report_t report;
    crux_config_service_result_t result = crux_config_service_save_local(&report);

    switch (result) {
    case CRUX_CONFIG_SERVICE_OK:
        return CRUX_NETWORK_FAILURE_NONE;
    case CRUX_CONFIG_SERVICE_ERR_STORE:
        return report.failed_result == CRUX_CONFIG_ERR_NOT_SUPPORTED
                   ? CRUX_NETWORK_FAILURE_NOT_SUPPORTED
                   : CRUX_NETWORK_FAILURE_STORAGE;
    case CRUX_CONFIG_SERVICE_ERR_BUSY:
        return CRUX_NETWORK_FAILURE_BUSY;
    default:
        return CRUX_NETWORK_FAILURE_NOT_READY;
    }
}

/* Updates the named parameters in memory, then saves. A failed update stops before any save. */
static crux_network_failure_t phase(const size_t *indexes, const uint32_t *numbers, size_t count,
                                    bool flag)
{
    for (size_t position = 0; position < count; ++position) {
        crux_config_service_result_t result =
            update_parameter(indexes[position], numbers[position], flag);

        if (result != CRUX_CONFIG_SERVICE_OK) {
            return failure_of_update(result);
        }
    }
    return save_marks();
}

static crux_network_failure_t save_set(const crux_network_settings_t *set)
{
    const size_t values_at[3] = {P_ADDRESS, P_PREFIX, P_GATEWAY};
    const uint32_t values[3] = {set->address, set->prefix_length, set->gateway};
    const size_t check_at[1] = {P_CHECK};
    const uint32_t check[1] = {crux_network_settings_check_key(set)};
    const size_t flag_at[1] = {P_FLAG};
    const uint32_t flag_number[1] = {0u};
    crux_network_failure_t failure;

    failure = phase(values_at, values, 3u, false);
    if (failure != CRUX_NETWORK_FAILURE_NONE) {
        return failure;
    }
    failure = phase(check_at, check, 1u, false);
    if (failure != CRUX_NETWORK_FAILURE_NONE) {
        return failure;
    }
    return phase(flag_at, flag_number, 1u, true);
}

bool crux_network_settings_work_once(void)
{
    crux_network_settings_t set;
    crux_access_session_t session;
    crux_network_failure_t failure;

    if (!svc.started || crux_mutex_lock(op_lock, CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS) != CRUX_OK) {
        return false;
    }
    if (!lock_state(CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS)) {
        (void)crux_mutex_unlock(op_lock);
        return false;
    }
    if (svc.job != JOB_QUEUED) {
        unlock_state();
        (void)crux_mutex_unlock(op_lock);
        return false;
    }
    svc.job = JOB_RUNNING;
    set = svc.pending;
    session = svc.session;
    unlock_state();

    failure = save_set(&set);

    /* Nothing else ends a running save, so the state is still this thread's. A server-thread call
     * waits briefly, and this thread may wait longer, because nothing waits on it. */
    while (!lock_state(CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS)) {
    }
    if (failure == CRUX_NETWORK_FAILURE_NONE) {
        svc.job = JOB_NONE;
        svc.stored_valid = true;
        svc.stored = set;
    } else {
        /* A failed save may have changed stored bytes, so the stored set is not known to be whole.
         */
        svc.job = JOB_FAILED;
        svc.stored_valid = false;
    }
    svc.failure = failure;
    svc.session.value = 0u;
    unlock_state();
    (void)crux_mutex_unlock(op_lock);

    crux_network_settings_port_event(failure == CRUX_NETWORK_FAILURE_NONE
                                         ? CRUX_NETWORK_SETTINGS_EVENT_CHANGED
                                         : CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED,
                                     session);
    return true;
}

static void worker_main(void *argument)
{
    (void)argument;
    for (;;) {
        (void)crux_semaphore_take(work_signal, CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS);
        while (crux_network_settings_work_once()) {
        }
    }
}

crux_network_result_t crux_network_settings_start_worker(void)
{
    static const crux_thread_options_t options = {0u, CRUX_THREAD_PRIORITY_LOW};

    if (!svc.started) {
        return CRUX_NETWORK_ERR_NOT_READY;
    }
    if (worker_started) {
        return CRUX_NETWORK_OK;
    }
    if (crux_thread_create(&options, worker_main, NULL, &worker_thread) != CRUX_OK) {
        return CRUX_NETWORK_ERR_NOT_READY;
    }
    worker_started = true;
    return CRUX_NETWORK_OK;
}

/* Recovery. */

crux_network_result_t crux_network_settings_recover(crux_access_session_t session)
{
    crux_network_failure_t failure;
    crux_config_service_result_t updated;
    crux_network_result_t result;

    if (!svc.started) {
        return CRUX_NETWORK_ERR_NOT_READY;
    }
    result = check_permission(session, CRUX_NETWORK_SETTINGS_PERMISSION_WRITE);
    if (result != CRUX_NETWORK_OK) {
        return result;
    }
    if (!svc.persistence) {
        return CRUX_NETWORK_ERR_NOT_SUPPORTED;
    }
    if (!lock_state(CRUX_NETWORK_SETTINGS_LOCK_WAIT_MS)) {
        return CRUX_NETWORK_ERR_BUSY;
    }
    if (svc.job == JOB_QUEUED || svc.job == JOB_RUNNING || svc.recovering) {
        unlock_state();
        return CRUX_NETWORK_ERR_BUSY;
    }
    svc.recovering = true;
    unlock_state();

    if (crux_mutex_lock(op_lock, CRUX_NETWORK_SETTINGS_OP_WAIT_MS) != CRUX_OK) {
        while (!lock_state(CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS)) {
        }
        svc.recovering = false;
        unlock_state();
        return CRUX_NETWORK_ERR_BUSY;
    }
    /* Only the flag. The values stay stored and are not used until a save sets the flag again. */
    updated = update_parameter(P_FLAG, 0u, false);
    failure = updated == CRUX_CONFIG_SERVICE_OK ? save_marks() : failure_of_update(updated);
    while (!lock_state(CRUX_NETWORK_SETTINGS_WORKER_WAIT_MS)) {
    }
    svc.recovering = false;
    if (failure == CRUX_NETWORK_FAILURE_NONE) {
        svc.stored_valid = false;
    }
    unlock_state();
    (void)crux_mutex_unlock(op_lock);

    crux_network_settings_port_event(failure == CRUX_NETWORK_FAILURE_NONE
                                         ? CRUX_NETWORK_SETTINGS_EVENT_SAVED_IGNORED
                                         : CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED,
                                     session);
    if (failure == CRUX_NETWORK_FAILURE_NONE) {
        return CRUX_NETWORK_OK;
    }
    return failure == CRUX_NETWORK_FAILURE_BUSY ? CRUX_NETWORK_ERR_BUSY : CRUX_NETWORK_ERR_STORE;
}
