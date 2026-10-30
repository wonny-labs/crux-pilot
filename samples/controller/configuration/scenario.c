/* Application-owned software scenario; existing native controller loops are unchanged. */
#include "controller.h"

#include <crux/configuration.h>

#include <stddef.h>

#define REQUIRE(expression)                                                                        \
    do {                                                                                           \
        if (!(expression))                                                                         \
            return __LINE__;                                                                       \
    } while (0)

typedef struct {
    uint32_t report_every;
    bool diagnostics;
} runtime_snapshot_t;

static bool report_range(const crux_config_value_t *value, void *context)
{
    (void)context;
    return value->type == CRUX_CONFIG_U32 && value->data.u32 >= 1 && value->data.u32 <= 100;
}

static crux_config_result_t snapshot(const crux_config_param_t *report,
                                     const crux_config_param_t *diagnostics,
                                     runtime_snapshot_t *out)
{
    crux_config_value_t r = {.type = CRUX_CONFIG_U32}, d = {.type = CRUX_CONFIG_BOOL};
    crux_config_result_t result = crux_config_read(report, &r);
    if (result != CRUX_CONFIG_OK)
        return result;
    result = crux_config_read(diagnostics, &d);
    if (result != CRUX_CONFIG_OK)
        return result;
    *out = (runtime_snapshot_t){r.data.u32, d.data.boolean};
    return CRUX_CONFIG_OK;
}

int controller_configuration_scenario(void)
{
    const crux_config_spec_t report_spec = {
        "report_every_cycles", 1, {.type = CRUX_CONFIG_U32, .data.u32 = 5}, report_range, NULL};
    const crux_config_spec_t diagnostics_spec = {
        "diagnostics_enabled", 1, {.type = CRUX_CONFIG_BOOL, .data.boolean = false}, NULL, NULL};
    crux_config_param_t report = {0}, diagnostics = {0};
    REQUIRE(crux_config_init(&report, &report_spec) == CRUX_CONFIG_OK);
    REQUIRE(crux_config_init(&diagnostics, &diagnostics_spec) == CRUX_CONFIG_OK);

    REQUIRE(crux_config_load(&report) == CRUX_CONFIG_DEFAULT_MISSING);
    REQUIRE(crux_config_load(&diagnostics) == CRUX_CONFIG_DEFAULT_MISSING);

    runtime_snapshot_t active = {0};
    REQUIRE(snapshot(&report, &diagnostics, &active) == CRUX_CONFIG_OK);
    REQUIRE(active.report_every == 5 && !active.diagnostics);

    crux_config_value_t report_value = {.type = CRUX_CONFIG_U32, .data.u32 = 3};
    crux_config_value_t diagnostics_value = {.type = CRUX_CONFIG_BOOL, .data.boolean = true};
    REQUIRE(crux_config_update(&report, &report_value) == CRUX_CONFIG_OK);
    REQUIRE(crux_config_update(&diagnostics, &diagnostics_value) == CRUX_CONFIG_OK);
    REQUIRE(active.report_every == 5 && !active.diagnostics); /* no implicit activation */
    REQUIRE(crux_config_save(&report) == CRUX_CONFIG_OK);
    REQUIRE(crux_config_save(&diagnostics) == CRUX_CONFIG_OK); /* separate saves, no transaction */
    report_value.data.u32 = 0;
    REQUIRE(crux_config_update(&report, &report_value) == CRUX_CONFIG_ERR_REJECTED);

    unsigned reports = 0, diagnostic_observations = 0;
    controller_schedule_t schedule = {.deadline_ms = 0, .period_ms = 100};
    for (uint32_t cycle = 1; cycle <= 20; ++cycle) {
        if (cycle == 4) { /* application chooses the activation boundary */
            REQUIRE(snapshot(&report, &diagnostics, &active) == CRUX_CONFIG_OK);
        }
        controller_reading_t reading = controller_observe(cycle);
        REQUIRE(reading.cycle == cycle);
        if (cycle % active.report_every == 0)
            ++reports;
        if (active.diagnostics)
            ++diagnostic_observations;
        REQUIRE(controller_schedule_advance(&schedule, (int64_t)(cycle - 1) * 100) == CRUX_OK);
    }
    REQUIRE(reports == 5 && diagnostic_observations == 17);
    REQUIRE(schedule.period_ms == 100 && schedule.deadline_ms == 2000 && schedule.skipped == 0);

    report_value.data.u32 = 2;
    diagnostics_value.data.boolean = false;
    REQUIRE(crux_config_update(&report, &report_value) == CRUX_CONFIG_OK);
    REQUIRE(crux_config_update(&diagnostics, &diagnostics_value) == CRUX_CONFIG_OK);
    crux_config_param_t restarted_report = {0}, restarted_diagnostics = {0};
    REQUIRE(crux_config_init(&restarted_report, &report_spec) == CRUX_CONFIG_OK);
    REQUIRE(crux_config_init(&restarted_diagnostics, &diagnostics_spec) == CRUX_CONFIG_OK);
    REQUIRE(crux_config_load(&restarted_report) == CRUX_CONFIG_OK);
    REQUIRE(crux_config_load(&restarted_diagnostics) == CRUX_CONFIG_OK);
    REQUIRE(snapshot(&restarted_report, &restarted_diagnostics, &active) == CRUX_CONFIG_OK);
    REQUIRE(active.report_every == 3 && active.diagnostics); /* unsaved updates not restored */
    return 0;
}
