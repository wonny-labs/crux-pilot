/* The status and configuration services on native_sim, with the real System, Time and Sync. Host
 * reports reset causes unsupported, so only here can a status poll be shown to leave the System
 * contract's one permission to clear alone. No hardware is operated. */
#include "config_service_port.h"
#include "fault_provider.h"
#include "mgmt_status_port.h"

#include <crux/config_service.h>
#include <crux/management_status.h>
#include <crux/time.h>

#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/* What the reset cause hardware reports, and how many times it was cleared. */
static uint32_t supported_causes = RESET_POR | RESET_SOFTWARE;
static uint32_t reported_causes = RESET_POR;
static unsigned clears;

int __wrap_z_impl_hwinfo_get_reset_cause(uint32_t *out)
{
    *out = reported_causes;
    return 0;
}

int __wrap_z_impl_hwinfo_get_supported_reset_cause(uint32_t *out)
{
    *out = supported_causes;
    return 0;
}

int __wrap_z_impl_hwinfo_clear_reset_cause(void)
{
    ++clears;
    reported_causes = 0;
    return 0;
}

/* Counts the real System's observations, to show the rate limit on the real clock. */
static unsigned system_reads;
crux_status_t __real_crux_system_status_get(crux_system_status_t *out);
crux_status_t __wrap_crux_system_status_get(crux_system_status_t *out);

crux_status_t __wrap_crux_system_status_get(crux_system_status_t *out)
{
    ++system_reads;
    return __real_crux_system_status_get(out);
}

/* Sessions of the access double. Access has no Zephyr composition yet. */
static const crux_access_session_t session_both = {1};
static const crux_access_session_t session_reader = {2};
static const crux_access_session_t session_none = {3};
static const crux_access_session_t session_invalid = {0};
static unsigned denials;

static bool holds(crux_access_session_t session, const char *permission)
{
    if (session.value == session_both.value) {
        return true;
    }
    return session.value == session_reader.value &&
           (strcmp(permission, "cfg.read") == 0 ||
            strcmp(permission, CRUX_MGMT_STATUS_PERMISSION_READ) == 0);
}

crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission)
{
    if (session.value == session_invalid.value) {
        return CRUX_ACCESS_ERR_SESSION_INVALID;
    }
    if (holds(session, permission)) {
        return CRUX_ACCESS_OK;
    }
    ++denials;
    return CRUX_ACCESS_ERR_DENIED;
}

crux_access_result_t crux_access_permitted(crux_access_session_t session, const char *permission,
                                           bool *permitted)
{
    if (session.value == session_invalid.value) {
        return CRUX_ACCESS_ERR_SESSION_INVALID;
    }
    *permitted = holds(session, permission);
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_session_user(crux_access_session_t session, char *name,
                                              size_t size)
{
    ARG_UNUSED(size);
    if (session.value == session_invalid.value) {
        return CRUX_ACCESS_ERR_SESSION_INVALID;
    }
    name[0] = '\0';
    return CRUX_ACCESS_OK;
}

/* The network source. A composition with no network would leave it failing like this. */
crux_status_t crux_mgmt_status_port_net(crux_mgmt_status_net_t *out)
{
    ARG_UNUSED(out);
    return CRUX_ERR_NOT_SUPPORTED;
}

static unsigned config_events;

void crux_config_service_port_event(crux_config_service_event_t event,
                                    crux_access_session_t session, const char *name)
{
    ARG_UNUSED(event);
    ARG_UNUSED(session);
    ARG_UNUSED(name);
    ++config_events;
}

/* The table: a string, a number with a range, and a sensitive string. */
#define STRING_VALUE(v)                                                                            \
    {                                                                                              \
        .type = CRUX_CONFIG_STRING, .data.string = {.text = (v), .length = sizeof(v) - 1u }        \
    }
static const crux_config_spec_t spec_host = {"host_name", 1, STRING_VALUE("crux"), NULL, NULL};
static const crux_config_spec_t spec_secret = {"link_secret", 1, STRING_VALUE(""), NULL, NULL};
static const crux_config_spec_t spec_count = {
    "count", 1, {.type = CRUX_CONFIG_U32, .data.u32 = 5}, NULL, NULL};
static crux_config_param_t param_host, param_secret, param_count;
static char storage_host[CRUX_CONFIG_SERVICE_TEXT_SIZE];
static char storage_secret[CRUX_CONFIG_SERVICE_TEXT_SIZE];
static const crux_config_service_entry_t table[] = {
    {&param_count, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_host, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_RESTART},
    {&param_secret, "cfg.read", "cfg.write", true, CRUX_CONFIG_ACTIVATION_SAFE_STATE},
};

static void *suite_setup(void)
{
    config_fault_reset();
    zassert_equal(crux_realtime_init(), CRUX_OK);
    zassert_equal(crux_config_init(&param_count, &spec_count), CRUX_CONFIG_OK);
    zassert_equal(
        crux_config_init_string(&param_host, &spec_host, storage_host, sizeof(storage_host)),
        CRUX_CONFIG_OK);
    zassert_equal(crux_config_init_string(&param_secret, &spec_secret, storage_secret,
                                          sizeof(storage_secret)),
                  CRUX_CONFIG_OK);
    zassert_equal(crux_config_service_start(table, ARRAY_SIZE(table)), CRUX_CONFIG_SERVICE_OK);

    const crux_mgmt_status_config_t config = {
        .platform_version = "1.2.3",
        .project_version = "native",
        /* The boot owner has not read the cause in this suite, until the case below does. */
        .boot_reset_cause_status = CRUX_ERR_NO_DATA,
    };
    zassert_equal(crux_mgmt_status_start(&config), CRUX_OK);
    return NULL;
}

static crux_mgmt_status_entry_t entry_named(crux_access_session_t session, const char *name)
{
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    while (crux_mgmt_status_next(session, &cursor, &entry) == CRUX_OK) {
        if (strcmp(entry.name, name) == 0) {
            return entry;
        }
    }
    zassert_unreachable("%s is not listed", name);
    return entry;
}

ZTEST(mgmt_services, test_status_polls_leave_the_clear_permission_alone)
{
    /* Any number of polls, across several snapshot refreshes, with the real System behind them. */
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned j = 0; j < 20; ++j) {
            size_t cursor = 0;
            crux_mgmt_status_entry_t entry;
            while (crux_mgmt_status_next(session_both, &cursor, &entry) == CRUX_OK) {
            }
        }
        k_msleep(CRUX_MGMT_STATUS_REFRESH_MS + 10);
    }

    /* The status service never read the cause, so nothing has given the permission to clear. */
    zassert_equal(crux_system_reset_cause_clear(), CRUX_ERR_INVALID_STATE);
    zassert_equal(clears, 0u);

    /* The boot owner's own read does, once. */
    crux_reset_cause_t cause = {0};
    zassert_equal(crux_system_reset_cause_get(&cause), CRUX_OK);
    zassert_equal(cause.causes, CRUX_RESET_CAUSE_POR);
    zassert_equal(crux_system_reset_cause_clear(), CRUX_OK);
    zassert_equal(clears, 1u);
    zassert_equal(crux_system_reset_cause_clear(), CRUX_ERR_INVALID_STATE);

    /* More polls still give nothing back. */
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    while (crux_mgmt_status_next(session_both, &cursor, &entry) == CRUX_OK) {
    }
    zassert_equal(crux_system_reset_cause_clear(), CRUX_ERR_INVALID_STATE);
}

ZTEST(mgmt_services, test_status_items_on_the_real_sources)
{
    crux_mgmt_status_entry_t entry = entry_named(session_reader, "system.uptime_ms");
    zassert_equal(entry.status, CRUX_OK);
    zassert_equal(entry.value.type, CRUX_MGMT_STATUS_U64);

    /* The realtime clock was initialised and never set. */
    entry = entry_named(session_reader, "system.clock");
    zassert_equal(entry.status, CRUX_OK);
    zassert_str_equal(entry.value.data.text, "unset");

    /* This simulator composition claims no watchdog, and observing it arms nothing. */
    entry = entry_named(session_reader, "system.watchdog_state");
    zassert_equal(entry.status, CRUX_ERR_NOT_SUPPORTED);

    /* What the boot owner passed, and not what the hardware reports. */
    zassert_equal(entry_named(session_reader, "reset.causes").status, CRUX_ERR_NO_DATA);
    zassert_equal(entry_named(session_reader, "net.link").status, CRUX_ERR_NOT_SUPPORTED);
    zassert_str_equal(entry_named(session_reader, "version.project").value.data.text, "native");

    /* A session that may see nothing gets the empty list, and an invalid one is refused. */
    char json[64];
    size_t length = 0;
    zassert_equal(crux_mgmt_status_report_json(session_none, json, sizeof(json), &length), CRUX_OK);
    zassert_str_equal(json, "{\"items\":[]}");
    zassert_equal(crux_mgmt_status_report_json(session_invalid, json, sizeof(json), &length),
                  CRUX_ERR_INVALID_STATE);
}

static void poll_status(unsigned times)
{
    for (unsigned i = 0; i < times; ++i) {
        size_t cursor = 0;
        crux_mgmt_status_entry_t entry;
        while (crux_mgmt_status_next(session_reader, &cursor, &entry) == CRUX_OK) {
        }
    }
}

ZTEST(mgmt_services, test_snapshot_reads_are_rate_limited_on_the_real_clock)
{
    /* Start from a fresh snapshot, so that the next period has only just begun. */
    k_msleep(CRUX_MGMT_STATUS_REFRESH_MS + 10);
    poll_status(1);
    unsigned reads = system_reads;

    /* Any number of polls inside a period read nothing more, while uptime moves. */
    poll_status(100);
    zassert_equal(system_reads, reads);
    crux_mgmt_status_entry_t first = entry_named(session_reader, "system.uptime_ms");
    k_msleep(20);
    crux_mgmt_status_entry_t second = entry_named(session_reader, "system.uptime_ms");
    zassert_true(second.value.data.u64 > first.value.data.u64);
    zassert_equal(system_reads, reads);

    /* The next period reads once. */
    k_msleep(CRUX_MGMT_STATUS_REFRESH_MS + 10);
    poll_status(100);
    zassert_equal(system_reads, reads + 1u);
}

static void busy_checks(void *first, void *second, void *third)
{
    ARG_UNUSED(second);
    ARG_UNUSED(third);
    crux_config_service_result_t *results = first;
    size_t cursor = 0;
    crux_config_service_item_t item;
    results[0] = crux_config_service_next(session_both, &cursor, &item);
    crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 7};
    results[1] = crux_config_service_try_update("count", &value);
    results[2] = crux_config_service_try_read("count", &value, NULL, 0);
}

static K_THREAD_STACK_DEFINE(busy_stack, 2048);
static struct k_thread busy_thread;
static crux_config_service_result_t busy_results[3];

static void hook_during_save(void *context)
{
    ARG_UNUSED(context);
    config_fault_write_hook(NULL, NULL);
    k_thread_create(&busy_thread, busy_stack, K_THREAD_STACK_SIZEOF(busy_stack), busy_checks,
                    busy_results, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
    zassert_equal(k_thread_join(&busy_thread, K_SECONDS(5)), 0);
}

ZTEST(mgmt_services, test_config_service_on_real_sync)
{
    /* Text forms and the permission table, on the real Sync. */
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    size_t length = 0;
    crux_config_type_t type = CRUX_CONFIG_BOOL;
    zassert_equal(
        crux_config_service_read(session_reader, "host_name", text, sizeof(text), &length, &type),
        CRUX_CONFIG_SERVICE_OK);
    zassert_str_equal(text, "crux");
    zassert_equal(type, CRUX_CONFIG_STRING);

    crux_config_service_item_t applied;
    zassert_equal(crux_config_service_update(session_reader, "host_name", "x", 1, &applied),
                  CRUX_CONFIG_SERVICE_ERR_DENIED);
    zassert_equal(denials, 1u);
    zassert_equal(
        crux_config_service_read(session_none, "host_name", text, sizeof(text), &length, &type),
        CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    zassert_equal(crux_config_service_update(session_both, "host_name", "caf\xc3\xa9", 5, &applied),
                  CRUX_CONFIG_SERVICE_OK);
    zassert_equal(applied.activation, CRUX_CONFIG_ACTIVATION_RESTART);
    zassert_true(applied.unsaved);
    zassert_equal(crux_config_service_update(session_both, "host_name", "a\nb", 3, &applied),
                  CRUX_CONFIG_SERVICE_ERR_REJECTED);
    zassert_equal(crux_config_service_update(session_both, "count", "01", 2, &applied),
                  CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);

    /* Sensitive is write-only. */
    zassert_equal(crux_config_service_update(session_both, "link_secret", "secret", 6, &applied),
                  CRUX_CONFIG_SERVICE_OK);
    zassert_equal(
        crux_config_service_read(session_both, "link_secret", text, sizeof(text), &length, &type),
        CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY);

    /* While a save holds the lock across storage, a poll returns busy at once, on real Sync. */
    config_fault_write_hook(hook_during_save, NULL);
    crux_config_service_save_report_t report;
    zassert_equal(crux_config_service_save(session_both, &report), CRUX_CONFIG_SERVICE_OK);
    zassert_equal(report.saved, 2u);
    zassert_equal(busy_results[0], CRUX_CONFIG_SERVICE_ERR_BUSY);
    zassert_equal(busy_results[1], CRUX_CONFIG_SERVICE_ERR_BUSY);
    zassert_equal(busy_results[2], CRUX_CONFIG_SERVICE_ERR_BUSY);
    zassert_true(config_events > 0u);
}

ZTEST_SUITE(mgmt_services, NULL, suite_setup, NULL, NULL, NULL);
