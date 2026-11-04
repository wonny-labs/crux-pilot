/* Lock failures the Sync contract can report. This suite uses a small double for access in place
 * of the real core, because the double takes no lock of its own: every failure here is the
 * service's lock, reached through --wrap=crux_mutex_lock. */
#include "fake_events.h"
#include "fault_provider.h"

#include <crux/config_service.h>
#include <crux/sync.h>

#include <stdio.h>
#include <string.h>

/* The double of access, in access_double.c. */

static unsigned failures;
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #expression);                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_status_t __real_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t __real_crux_mutex_create(crux_mutex_t *out);
crux_status_t __wrap_crux_mutex_create(crux_mutex_t *out);

static crux_status_t lock_result = CRUX_OK;
static crux_status_t create_result = CRUX_OK;
static unsigned save_waits;

crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    if (timeout_ms > 0) {
        ++save_waits;
    }
    return lock_result == CRUX_OK ? __real_crux_mutex_lock(mutex, timeout_ms) : lock_result;
}

crux_status_t __wrap_crux_mutex_create(crux_mutex_t *out)
{
    return create_result == CRUX_OK ? __real_crux_mutex_create(out) : create_result;
}
// NOLINTEND(bugprone-reserved-identifier)

static const crux_config_spec_t spec = {
    "value", 1, {.type = CRUX_CONFIG_U32, .data.u32 = 1}, NULL, NULL};
static crux_config_param_t parameter;
static const crux_config_service_entry_t table[] = {
    {&parameter, "p.read", "p.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION}};
static const crux_access_session_t session = {1};

static void expect_polls_give(crux_config_service_result_t expected)
{
    size_t cursor = 0;
    crux_config_service_item_t item;
    CHECK(crux_config_service_next(session, &cursor, &item) == expected);
    CHECK(cursor == (expected == CRUX_CONFIG_SERVICE_OK ? 1u : 0u));
    char text[16];
    size_t length;
    crux_config_type_t type;
    CHECK(crux_config_service_read(session, "value", text, sizeof(text), &length, &type) ==
          expected);
    crux_config_service_item_t applied;
    CHECK(crux_config_service_update(session, "value", "2", 1, &applied) == expected);
    crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 2};
    CHECK(crux_config_service_try_update("value", &value) == expected);
    CHECK(crux_config_service_try_read("value", &value, NULL, 0) == expected);
}

static void expect_saves_give(crux_config_service_result_t expected)
{
    crux_config_service_save_report_t report = {7, "stale", CRUX_CONFIG_ERR_IO};
    CHECK(crux_config_service_save(session, &report) == expected);
    CHECK(report.saved == 0 && report.failed_name == NULL);
    CHECK(crux_config_service_save_local(&report) == expected);
}

int main(void)
{
    config_fault_reset();
    CHECK(crux_config_init(&parameter, &spec) == CRUX_CONFIG_OK);

    /* A lock that cannot be created leaves the service not ready, and start can be retried. */
    create_result = CRUX_ERR_NO_RESOURCES;
    CHECK(crux_config_service_start(table, 1) == CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    expect_polls_give(CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    create_result = CRUX_OK;
    CHECK(crux_config_service_start(table, 1) == CRUX_CONFIG_SERVICE_OK);

    expect_polls_give(CRUX_CONFIG_SERVICE_OK);

    /* Held or contended Sync metadata reads as busy on a poll, and a save's timeout does too. */
    lock_result = CRUX_ERR_BUSY;
    expect_polls_give(CRUX_CONFIG_SERVICE_ERR_BUSY);
    expect_saves_give(CRUX_CONFIG_SERVICE_ERR_BUSY);
    lock_result = CRUX_ERR_TIMEOUT;
    expect_polls_give(CRUX_CONFIG_SERVICE_ERR_BUSY);
    expect_saves_give(CRUX_CONFIG_SERVICE_ERR_BUSY);

    /* Anything else means the lock itself failed, which fails closed and changes nothing. */
    const crux_status_t others[] = {CRUX_ERR_IO, CRUX_ERR_INVALID_STATE, CRUX_ERR_NOT_SUPPORTED,
                                    CRUX_ERR_INVALID_ARGUMENT, CRUX_ERR_NO_RESOURCES};
    for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); ++i) {
        lock_result = others[i];
        expect_polls_give(CRUX_CONFIG_SERVICE_ERR_NOT_READY);
        expect_saves_give(CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    }
    lock_result = CRUX_OK;
    crux_config_value_t value = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_service_try_read("value", &value, NULL, 0) == CRUX_CONFIG_SERVICE_OK);
    CHECK(value.data.u32 == 2);

    /* The polling operations never ask for a wait: only a save does. */
    save_waits = 0;
    expect_polls_give(CRUX_CONFIG_SERVICE_OK);
    CHECK(save_waits == 0);
    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save(session, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(save_waits == 1);

    printf("ConfigService.lock_faults: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
