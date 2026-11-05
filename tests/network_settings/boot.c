/* What the boot decides and applies: every row of the decision on the loaded records, and the boot
 * itself on the fake Configuration provider and the fake apply port. */
#include "net_test.h"

static network_stored_t good_stored(const crux_network_settings_t *set)
{
    crux_network_settings_t stored = net_stored_form(set);
    network_stored_t row = {CRUX_CONFIG_OK,
                            CRUX_CONFIG_OK,
                            CRUX_CONFIG_OK,
                            CRUX_CONFIG_OK,
                            CRUX_CONFIG_OK,
                            stored.address,
                            stored.prefix_length,
                            stored.gateway,
                            crux_network_settings_check_key(&stored),
                            true};

    return row;
}

static void decision_rows(void)
{
    crux_network_settings_t values = {0};
    network_stored_t row = good_stored(&net_set_a);

    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_SAVED);
    CHECK(net_same(&values, &net_set_a));

    /* Without a gateway the stored gateway is zero, and the values come back without one. */
    crux_network_settings_t plain = {net_set_a.address, 24u, false, 0u};
    row = good_stored(&plain);
    values = (crux_network_settings_t){0};
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_SAVED);
    CHECK(!values.has_gateway && values.gateway == 0u);

    row = good_stored(&net_set_a);
    row.address_load = CRUX_CONFIG_ERR_NOT_SUPPORTED;
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_NOT_SUPPORTED);
    row = good_stored(&net_set_a);
    row.flag_load = CRUX_CONFIG_ERR_IO;
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_STORE_ERROR);
    row.flag_load = CRUX_CONFIG_ERR_NOT_READY;
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_STORE_ERROR);
    row.flag_load = CRUX_CONFIG_ERR_NO_SPACE;
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_STORE_ERROR);
    row.flag_load = CRUX_CONFIG_ERR_REJECTED;
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_STORE_ERROR);

    /* Nothing saved is a first boot, not a fault. */
    network_stored_t none = {CRUX_CONFIG_DEFAULT_MISSING,
                             CRUX_CONFIG_DEFAULT_MISSING,
                             CRUX_CONFIG_DEFAULT_MISSING,
                             CRUX_CONFIG_DEFAULT_MISSING,
                             CRUX_CONFIG_DEFAULT_MISSING,
                             0,
                             0,
                             0,
                             0,
                             false};
    CHECK(network_decide(&none, &values) == CRUX_NETWORK_BOOT_NOT_STORED);

    /* A record that failed its own checks. */
    for (unsigned which = 0; which < 5; ++which) {
        row = good_stored(&net_set_a);
        crux_config_result_t *loads[] = {&row.address_load, &row.prefix_load, &row.gateway_load,
                                         &row.check_load, &row.flag_load};
        *loads[which] = CRUX_CONFIG_DEFAULT_REJECTED;
        CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_RECORD_INVALID);
    }

    /* Some of the five missing is a mixed set, whichever are missing. */
    for (unsigned mask = 1; mask < 31; ++mask) {
        row = good_stored(&net_set_a);
        crux_config_result_t *loads[] = {&row.address_load, &row.prefix_load, &row.gateway_load,
                                         &row.check_load, &row.flag_load};
        for (unsigned which = 0; which < 5; ++which) {
            if (mask & (1u << which)) {
                *loads[which] = CRUX_CONFIG_DEFAULT_MISSING;
            }
        }
        /* A missing flag reads as its default, false, and says disabled. Anything else missing
         * with the flag present is incomplete. */
        crux_network_boot_reason_t reason = network_decide(&row, &values);
        CHECK(reason == CRUX_NETWORK_BOOT_INCOMPLETE);
    }

    /* The recovery command stored the flag as false. */
    row = good_stored(&net_set_a);
    row.use_saved = false;
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_DISABLED);
    /* Even when it is the only record, as on a device that never saved. */
    none.flag_load = CRUX_CONFIG_OK;
    CHECK(network_decide(&none, &values) == CRUX_NETWORK_BOOT_DISABLED);

    /* Values that fail the checks, with a check key that matches them. */
    crux_network_settings_t bad = {0u, 24u, false, 0u};
    row = good_stored(&bad);
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_VALUES_INVALID);
    row = good_stored(&net_set_a);
    row.prefix_length = 31;
    row.check = crux_network_settings_check_key(
        &(crux_network_settings_t){row.address, row.prefix_length, true, row.gateway});
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_VALUES_INVALID);
    row = good_stored(&net_set_a);
    row.gateway = row.address;
    row.check = crux_network_settings_check_key(
        &(crux_network_settings_t){row.address, row.prefix_length, true, row.gateway});
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_VALUES_INVALID);

    /* The check key of the other set. */
    for (unsigned which = 0; which < 4; ++which) {
        row = good_stored(&net_set_a);
        network_stored_t other = good_stored(&net_set_b);
        uint32_t *members[] = {&row.address, &row.prefix_length, &row.gateway, &row.check};
        uint32_t *others[] = {&other.address, &other.prefix_length, &other.gateway, &other.check};
        *members[which] = *others[which];
        crux_network_boot_reason_t reason = network_decide(&row, &values);
        /* A mix is often not even a usable set. Either way it does not boot. */
        CHECK(reason == CRUX_NETWORK_BOOT_CHECK_MISMATCH ||
              reason == CRUX_NETWORK_BOOT_VALUES_INVALID);
        if (which == 1u || which == 3u) {
            CHECK(reason == CRUX_NETWORK_BOOT_CHECK_MISMATCH);
        }
    }
    /* The default check key never matches anything. */
    row = good_stored(&net_set_a);
    row.check = 0;
    CHECK(network_decide(&row, &values) == CRUX_NETWORK_BOOT_CHECK_MISMATCH);
}

static void first_boot_uses_defaults(void)
{
    crux_network_boot_report_t report = {0};

    net_boot(&report);
    CHECK(report.source == CRUX_NETWORK_SOURCE_DEFAULT);
    CHECK(report.reason == CRUX_NETWORK_BOOT_NOT_STORED);
    CHECK(report.applied && report.persistence);
    CHECK(net_port_apply_count() == 1u);
    CHECK(net_same(net_port_applied(0), &net_defaults));
    CHECK(net_same(&report.values, &net_defaults));
    /* One boot loaded each of the five records, and wrote nothing and recorded no event. */
    CHECK(config_fault_reads() == 5u);
    CHECK(config_fault_writes() == 0u);
    CHECK(net_events_count() == 0u);
}

static void saved_values_apply(void)
{
    crux_network_boot_report_t report = {0};
    const net_options_t keep = {true, 0};

    config_fault_reset();
    net_store_set(&net_set_a, true);
    net_boot_with(&keep, &report);
    CHECK(report.source == CRUX_NETWORK_SOURCE_SAVED);
    CHECK(report.reason == CRUX_NETWORK_BOOT_SAVED);
    CHECK(net_port_apply_count() == 1u);
    CHECK(net_same(net_port_applied(0), &net_set_a));

    /* A set with no gateway, which is stored as zero. */
    crux_network_settings_t plain = {net_set_b.address, net_set_b.prefix_length, false, 0};
    config_fault_reset();
    net_store_set(&plain, true);
    net_boot_with(&keep, &report);
    CHECK(report.reason == CRUX_NETWORK_BOOT_SAVED);
    CHECK(!net_port_applied(0)->has_gateway);
}

static crux_network_boot_reason_t boot_reason_with(const crux_network_settings_t *set, bool flag,
                                                   crux_network_boot_report_t *report)
{
    const net_options_t keep = {true, 0};

    config_fault_reset();
    if (set != NULL) {
        net_store_set(set, flag);
    }
    net_boot_with(&keep, report);
    return report->reason;
}

static void every_fault_falls_back(void)
{
    crux_network_boot_report_t report = {0};
    uint8_t torn[3] = {1, CRUX_CONFIG_U32, 1};

    /* The flag is false. */
    CHECK(boot_reason_with(&net_set_a, false, &report) == CRUX_NETWORK_BOOT_DISABLED);
    CHECK(report.source == CRUX_NETWORK_SOURCE_DEFAULT &&
          net_same(net_port_applied(0), &net_defaults));

    /* Each record torn in turn. */
    static const char *const keys[] = {
        CRUX_NETWORK_SETTINGS_KEY_ADDRESS, CRUX_NETWORK_SETTINGS_KEY_PREFIX,
        CRUX_NETWORK_SETTINGS_KEY_GATEWAY, CRUX_NETWORK_SETTINGS_KEY_CHECK,
        CRUX_NETWORK_SETTINGS_KEY_USE_SAVED};
    for (size_t index = 0; index < 5; ++index) {
        const net_options_t keep = {true, 0};

        config_fault_reset();
        net_store_set(&net_set_a, true);
        config_fault_record(keys[index], torn, sizeof(torn));
        net_boot_with(&keep, &report);
        CHECK(report.reason == CRUX_NETWORK_BOOT_RECORD_INVALID);
        CHECK(report.source == CRUX_NETWORK_SOURCE_DEFAULT);
        CHECK(net_port_apply_count() == 1u && net_same(net_port_applied(0), &net_defaults));
    }

    /* A check key from another set. */
    config_fault_reset();
    net_store_set(&net_set_a, true);
    net_store_u32(CRUX_NETWORK_SETTINGS_KEY_CHECK, crux_network_settings_check_key(&net_set_b));
    {
        const net_options_t keep = {true, 0};

        net_boot_with(&keep, &report);
    }
    CHECK(report.reason == CRUX_NETWORK_BOOT_CHECK_MISMATCH);

    /* A record of the wrong type. */
    config_fault_reset();
    net_store_set(&net_set_a, true);
    net_store_bool(CRUX_NETWORK_SETTINGS_KEY_ADDRESS, true);
    {
        const net_options_t keep = {true, 0};

        net_boot_with(&keep, &report);
    }
    CHECK(report.reason == CRUX_NETWORK_BOOT_RECORD_INVALID);

    /* Values that fail the checks, with a matching check key: a store from other firmware. */
    crux_network_settings_t bad = {0xE0000001u, 24u, false, 0u};
    CHECK(boot_reason_with(&bad, true, &report) == CRUX_NETWORK_BOOT_VALUES_INVALID);

    /* Only the address, as an interrupted first save leaves it. */
    config_fault_reset();
    net_store_u32(CRUX_NETWORK_SETTINGS_KEY_ADDRESS, net_set_a.address);
    {
        const net_options_t keep = {true, 0};

        net_boot_with(&keep, &report);
    }
    CHECK(report.reason == CRUX_NETWORK_BOOT_INCOMPLETE);
    CHECK(report.source == CRUX_NETWORK_SOURCE_DEFAULT);

    /* A read that fails, which is not a missing record. */
    config_fault_reset();
    net_store_set(&net_set_a, true);
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_ERR_IO, SIZE_MAX});
    {
        const net_options_t keep = {true, 0};

        net_boot_with(&keep, &report);
    }
    CHECK(report.reason == CRUX_NETWORK_BOOT_STORE_ERROR && report.persistence);
    CHECK(report.source == CRUX_NETWORK_SOURCE_DEFAULT);
}

static void a_refused_apply_falls_back(void)
{
    crux_network_boot_report_t report = {0};
    net_options_t options = {true, 1};

    /* The port refuses the saved values: the defaults are tried, and the report says why. */
    config_fault_reset();
    net_store_set(&net_set_a, true);
    net_boot_with(&options, &report);
    CHECK(report.source == CRUX_NETWORK_SOURCE_DEFAULT);
    CHECK(report.reason == CRUX_NETWORK_BOOT_APPLY_FAILED);
    CHECK(report.applied);
    CHECK(net_port_apply_count() == 2u);
    CHECK(net_same(net_port_applied(0), &net_set_a));
    CHECK(net_same(net_port_applied(1), &net_defaults));
    CHECK(net_same(&report.values, &net_defaults));

    /* It refuses the defaults too: nothing is applied, and the boot still ends. */
    options.apply_failures = 2;
    net_boot_with(&options, &report);
    CHECK(!report.applied && report.reason == CRUX_NETWORK_BOOT_APPLY_FAILED);
    CHECK(net_port_apply_count() == 2u);

    /* Defaults that the port refuses are not tried twice. */
    options.keep_store = false;
    options.apply_failures = 1;
    net_boot_with(&options, &report);
    CHECK(!report.applied && report.reason == CRUX_NETWORK_BOOT_NOT_STORED);
    CHECK(net_port_apply_count() == 1u);
}

static void boot_refuses_bad_arguments(void)
{
    crux_network_settings_config_t bad = {{0xE0000001u, 24u, false, 0u}};
    crux_network_boot_report_t report = {0};

    CHECK(crux_network_settings_boot(NULL, &report) == CRUX_NETWORK_ERR_INVALID_ARGUMENT);
    CHECK(crux_network_settings_boot(&bad, &report) == CRUX_NETWORK_ERR_INVALID_ARGUMENT);
    CHECK(net_port_apply_count() == 0u);
    bad.defaults = net_defaults;
    CHECK(crux_network_settings_boot(&bad, NULL) == CRUX_NETWORK_OK);
    crux_network_settings_boot_report(&report);
    CHECK(report.reason == CRUX_NETWORK_BOOT_NOT_STORED);
}

int main(void)
{
    net_run_case("every row of the decision", decision_rows);
    net_run_case("a first boot uses the defaults", first_boot_uses_defaults);
    net_run_case("saved values apply", saved_values_apply);
    net_run_case("every fault falls back to the defaults", every_fault_falls_back);
    net_run_case("a refused apply", a_refused_apply_falls_back);
    net_run_case("bad arguments", boot_refuses_bad_arguments);
    return net_finish("network_settings_boot");
}
