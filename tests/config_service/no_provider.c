/* The service over the selected no-provider port, as an image with no Configuration provider: a
 * parameter starts from its default, an update still works, and a save says that it is not
 * available.
 */
#include "fake_events.h"

#include <crux/config_service.h>

#include <stdio.h>
#include <string.h>

static unsigned failures;
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #expression);                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    static const crux_config_spec_t spec = {
        "value", 1, {.type = CRUX_CONFIG_U32, .data.u32 = 1}, NULL, NULL};
    static crux_config_param_t parameter;
    CHECK(crux_config_init(&parameter, &spec) == CRUX_CONFIG_OK);
    /* What a composition does at boot: a load that is not supported leaves the default. */
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_NOT_SUPPORTED);
    static const crux_config_service_entry_t table[] = {
        {&parameter, "p.read", "p.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION}};
    CHECK(crux_config_service_start(table, 1) == CRUX_CONFIG_SERVICE_OK);

    const crux_access_session_t session = {1};
    crux_config_service_item_t applied;
    CHECK(crux_config_service_update(session, "value", "2", 1, &applied) == CRUX_CONFIG_SERVICE_OK);
    crux_config_service_save_report_t report = {0};
    CHECK(crux_config_service_save(session, &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(report.saved == 0);
    CHECK(strcmp(report.failed_name, "value") == 0);
    CHECK(report.failed_result == CRUX_CONFIG_ERR_NOT_SUPPORTED);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_SAVE_FAILED) == 1);

    /* Its value stays accepted, and it stays marked. */
    crux_config_value_t value = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_service_try_read("value", &value, NULL, 0) == CRUX_CONFIG_SERVICE_OK);
    CHECK(value.data.u32 == 2);
    CHECK(crux_config_service_save_local(&report) == CRUX_CONFIG_SERVICE_ERR_STORE);

    printf("ConfigService.no_provider: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
