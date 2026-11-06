/* On the no-provider port, which a composition selects until a Configuration provider exists for
 * it: the target today. Nothing can be saved, the boot uses the defaults, and a request says so. */
#include "net_test.h"

/* net_test.c prepares the fake store. With no provider there is none, so these do nothing. */
void config_fault_reset(void)
{
}
void config_fault_record(const char *key, const uint8_t *bytes, size_t length)
{
    (void)key;
    (void)bytes;
    (void)length;
}
void config_fault_write(config_fault_write_options_t options)
{
    (void)options;
}
void config_fault_write_hook(config_fault_hook_t hook, void *context)
{
    (void)hook;
    (void)context;
}

static void defaults_run_and_nothing_saves(void)
{
    crux_network_boot_report_t report;
    crux_network_state_t state;
    crux_network_findings_t findings;

    net_boot(&report);
    CHECK(report.reason == CRUX_NETWORK_BOOT_NOT_SUPPORTED);
    CHECK(report.source == CRUX_NETWORK_SOURCE_DEFAULT && report.applied && !report.persistence);
    CHECK(net_port_apply_count() == 1u && net_same(net_port_applied(0), &net_defaults));
    CHECK(crux_config_service_start(net_table(), NET_TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_network_settings_start() == CRUX_NETWORK_OK);

    CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_OK);
    CHECK(state.has_running && state.source == CRUX_NETWORK_SOURCE_DEFAULT && !state.has_saved);
    CHECK(!state.reboot_required && state.save == CRUX_NETWORK_SAVE_NONE);

    /* The rules still answer first, so that a client learns what is wrong with its values. */
    crux_network_settings_t bad = {0u, 24u, false, 0u};
    CHECK(crux_network_settings_request(s_both, &bad, &findings) == CRUX_NETWORK_ERR_REJECTED);
    CHECK(crux_network_settings_request(s_both, &net_set_a, &findings) ==
          CRUX_NETWORK_ERR_NOT_SUPPORTED);
    /* The permission comes before it. */
    CHECK(crux_network_settings_request(s_reader, &net_set_a, &findings) ==
          CRUX_NETWORK_ERR_DENIED);
    CHECK(crux_network_settings_recover(s_both) == CRUX_NETWORK_ERR_NOT_SUPPORTED);
    CHECK(crux_network_settings_recover(s_reader) == CRUX_NETWORK_ERR_DENIED);
    CHECK(!crux_network_settings_work_once());
    CHECK(net_events_count() == 0u);
    CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_OK);
    CHECK(state.save == CRUX_NETWORK_SAVE_NONE);
}

int main(void)
{
    net_run_case("defaults run and nothing saves", defaults_run_and_nothing_saves);
    return net_finish("network_settings_no_provider");
}
