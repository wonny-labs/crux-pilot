/* The service: permission, the state it shows, validation, the save and what it records, each
 * failure of a save, and the recovery. On the real Access core and the real Configuration service,
 * over the fake provider and the fake apply port. */
#include "net_test.h"

static crux_network_state_t state_of(crux_access_session_t session)
{
    crux_network_state_t state;

    memset(&state, 0xA5, sizeof(state));
    CHECK(crux_network_settings_get(session, &state) == CRUX_NETWORK_OK);
    return state;
}

static crux_network_result_t request(crux_access_session_t session,
                                     const crux_network_settings_t *set)
{
    crux_network_findings_t findings;

    return crux_network_settings_request(session, set, &findings);
}

static void not_ready_before_start(void)
{
    crux_network_boot_report_t report;
    crux_network_state_t state;

    CHECK(crux_network_settings_start() == CRUX_NETWORK_ERR_NOT_READY);
    net_boot(&report);
    /* Booted, but the service has not started. */
    CHECK(crux_network_settings_get(s_admin, &state) == CRUX_NETWORK_ERR_NOT_READY);
    CHECK(request(s_admin, &net_set_a) == CRUX_NETWORK_ERR_NOT_READY);
    CHECK(crux_network_settings_recover(s_admin) == CRUX_NETWORK_ERR_NOT_READY);
    CHECK(!crux_network_settings_work_once());
    CHECK(crux_network_settings_start_worker() == CRUX_NETWORK_ERR_NOT_READY);
    /* Without the parameters in the Configuration service it cannot start either. */
    CHECK(crux_network_settings_start() == CRUX_NETWORK_ERR_NOT_READY);
    CHECK(crux_config_service_start(net_table(), 1u) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_network_settings_start() == CRUX_NETWORK_ERR_NOT_READY);
}

static void start_with_the_table(void)
{
    net_start();
    /* A second start changes nothing. */
    CHECK(crux_network_settings_start() == CRUX_NETWORK_OK);
    crux_network_state_t state = state_of(s_admin);
    CHECK(state.save == CRUX_NETWORK_SAVE_NONE);
}

static void read_permission(void)
{
    crux_network_state_t state = {0};
    size_t denials_before;

    net_start();
    CHECK(crux_network_settings_get(s_admin, &state) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_get(s_reader, &state) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_OK);
    denials_before = net_denials();
    /* Write alone is not read: a session may be able to save and not to see. */
    CHECK(crux_network_settings_get(s_writer, &state) == CRUX_NETWORK_ERR_DENIED);
    CHECK(crux_network_settings_get(s_stranger, &state) == CRUX_NETWORK_ERR_DENIED);
    CHECK(crux_network_settings_get(s_saver, &state) == CRUX_NETWORK_ERR_DENIED);
    CHECK(net_denials() == denials_before + 3u);
    /* A forced password change holds everything back. */
    CHECK(crux_network_settings_get(s_forced, &state) == CRUX_NETWORK_ERR_DENIED);
    CHECK(crux_network_settings_get(s_never_valid, &state) == CRUX_NETWORK_ERR_SESSION_INVALID);
    /* A refusal leaves the output alone. */
    memset(&state, 0x5A, sizeof(state));
    CHECK(crux_network_settings_get(s_stranger, &state) == CRUX_NETWORK_ERR_DENIED);
    CHECK(((const uint8_t *)&state)[0] == 0x5A);
    CHECK(crux_network_settings_get(s_admin, NULL) == CRUX_NETWORK_ERR_INVALID_ARGUMENT);
    /* No network event for any of it. */
    CHECK(net_events_count() == 0u);
}

static void write_permission(void)
{
    net_start();
    size_t denials_before = net_denials();

    CHECK(request(s_reader, &net_set_a) == CRUX_NETWORK_ERR_DENIED);
    CHECK(request(s_stranger, &net_set_a) == CRUX_NETWORK_ERR_DENIED);
    CHECK(request(s_saver, &net_set_a) == CRUX_NETWORK_ERR_DENIED);
    CHECK(request(s_forced, &net_set_a) == CRUX_NETWORK_ERR_DENIED);
    CHECK(request(s_never_valid, &net_set_a) == CRUX_NETWORK_ERR_SESSION_INVALID);
    CHECK(net_denials() == denials_before + 4u);
    /* Nothing was queued: no save runs, and nothing is written or recorded. */
    CHECK(!crux_network_settings_work_once());
    CHECK(config_fault_writes() == 0u);
    CHECK(net_events_count() == 0u);
    CHECK(state_of(s_admin).save == CRUX_NETWORK_SAVE_NONE);
    /* Permission comes before validation, so that an unpermitted session learns nothing of the
     * rules. */
    crux_network_settings_t bad = {0u, 99u, false, 0u};
    crux_network_findings_t findings = {7, {{0, 0}}};
    CHECK(crux_network_settings_request(s_stranger, &bad, &findings) == CRUX_NETWORK_ERR_DENIED);
    CHECK(findings.count == 0u);
    /* Write alone is enough to save. */
    CHECK(request(s_writer, &net_set_a) == CRUX_NETWORK_OK);
    CHECK(request(s_admin, NULL) == CRUX_NETWORK_ERR_INVALID_ARGUMENT);
}

static void defaults_show_as_running(void)
{
    crux_network_state_t state;

    net_start();
    state = state_of(s_reader);
    CHECK(state.has_running && net_same(&state.running, &net_defaults));
    CHECK(state.source == CRUX_NETWORK_SOURCE_DEFAULT);
    CHECK(!state.has_saved);
    CHECK(!state.reboot_required);
    CHECK(state.save == CRUX_NETWORK_SAVE_NONE && state.failure == CRUX_NETWORK_FAILURE_NONE);

    /* An interface with no address has no running values, and no reboot is promised. */
    net_port_set_running(false, NULL);
    state = state_of(s_reader);
    CHECK(!state.has_running && !state.reboot_required);
}

static void saved_values_show_as_the_source(void)
{
    crux_network_boot_report_t report;
    crux_network_state_t state;
    const net_options_t keep = {true, 0};

    config_fault_reset();
    net_store_set(&net_set_a, true);
    net_boot_with(&keep, &report);
    CHECK(crux_config_service_start(net_table(), NET_TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_network_settings_start() == CRUX_NETWORK_OK);
    state = state_of(s_both);
    CHECK(state.source == CRUX_NETWORK_SOURCE_SAVED);
    CHECK(state.has_running && net_same(&state.running, &net_set_a));
    CHECK(state.has_saved && net_same(&state.saved, &net_set_a));
    CHECK(!state.reboot_required);
}

static void invalid_settings_are_refused_whole(void)
{
    crux_network_findings_t findings;
    crux_network_settings_t bad = {0xE0000001u, 99u, true, 0xC6336401u};

    net_start();
    CHECK(crux_network_settings_request(s_both, &bad, &findings) == CRUX_NETWORK_ERR_REJECTED);
    CHECK(findings.count == 2u);
    CHECK(findings.items[0].field == CRUX_NETWORK_FIELD_ADDRESS);
    CHECK(findings.items[1].field == CRUX_NETWORK_FIELD_PREFIX_LENGTH);
    crux_network_settings_t zero_gateway = {0xC6336414u, 24u, true, 0u};
    CHECK(crux_network_settings_request(s_both, &zero_gateway, &findings) ==
          CRUX_NETWORK_ERR_REJECTED);
    CHECK(findings.count == 1u && findings.items[0].field == CRUX_NETWORK_FIELD_GATEWAY);
    /* No findings pointer is fine. */
    CHECK(crux_network_settings_request(s_both, &bad, NULL) == CRUX_NETWORK_ERR_REJECTED);
    /* A refusal is not a change: nothing queued, written or recorded, and the state is as before.
     */
    CHECK(!crux_network_settings_work_once());
    CHECK(config_fault_writes() == 0u && net_events_count() == 0u);
    CHECK(state_of(s_both).save == CRUX_NETWORK_SAVE_NONE);
}

static void expect_write_order(void)
{
    static const char *const order[] = {
        CRUX_NETWORK_SETTINGS_KEY_ADDRESS, CRUX_NETWORK_SETTINGS_KEY_PREFIX,
        CRUX_NETWORK_SETTINGS_KEY_GATEWAY, CRUX_NETWORK_SETTINGS_KEY_CHECK,
        CRUX_NETWORK_SETTINGS_KEY_USE_SAVED};

    CHECK(config_fault_write_log_count() == 5u);
    for (size_t index = 0; index < 5u && index < config_fault_write_log_count(); ++index) {
        CHECK(strcmp(config_fault_write_log_key(index), order[index]) == 0);
    }
}

static void a_save_runs_on_the_worker_step(void)
{
    crux_network_state_t state;
    crux_network_settings_t reboot_values;

    net_start();
    CHECK(request(s_both, &net_set_a) == CRUX_NETWORK_OK);
    /* Accepted is not saved. Nothing is written until the worker runs. */
    CHECK(config_fault_writes() == 0u);
    state = state_of(s_reader);
    CHECK(state.save == CRUX_NETWORK_SAVE_PENDING);
    CHECK(!state.has_saved);
    /* A second request while one is queued is told to wait, and changes nothing. */
    CHECK(request(s_both, &net_set_b) == CRUX_NETWORK_ERR_BUSY);
    CHECK(crux_network_settings_recover(s_both) == CRUX_NETWORK_ERR_BUSY);
    CHECK(net_events_count() == 0u);

    CHECK(crux_network_settings_work_once());
    CHECK(!crux_network_settings_work_once());
    expect_write_order();
    state = state_of(s_reader);
    CHECK(state.save == CRUX_NETWORK_SAVE_NONE && state.failure == CRUX_NETWORK_FAILURE_NONE);
    CHECK(state.has_saved && net_same(&state.saved, &net_set_a));
    /* Running is what was applied at boot, so the new values wait for a reboot. */
    CHECK(state.has_running && net_same(&state.running, &net_defaults));
    CHECK(state.source == CRUX_NETWORK_SOURCE_DEFAULT);
    CHECK(state.reboot_required);
    CHECK(net_port_apply_count() == 1u);
    /* One event, for the whole change, with the session that asked. */
    CHECK(net_events_count() == 1u);
    CHECK(net_events_get(0)->event == CRUX_NETWORK_SETTINGS_EVENT_CHANGED);
    CHECK(net_events_get(0)->session.value == s_both.value);
    /* The store now decides, at the next boot, on the whole set. */
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_SAVED);
    CHECK(net_same(&reboot_values, &net_set_a));

    /* The next request is accepted, and a set with no gateway stores zero. */
    crux_network_settings_t plain = {net_set_b.address, net_set_b.prefix_length, false, 0};
    CHECK(request(s_writer, &plain) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_SAVED);
    CHECK(net_same(&reboot_values, &plain) && !reboot_values.has_gateway);
    state = state_of(s_both);
    CHECK(state.has_saved && !state.saved.has_gateway);
    CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_CHANGED) == 2u);
    CHECK(net_events_count() == 2u);
}

static void saving_the_running_values_needs_no_reboot(void)
{
    crux_network_state_t state;

    net_start();
    CHECK(request(s_both, &net_defaults) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    state = state_of(s_both);
    CHECK(state.has_saved && net_same(&state.saved, &net_defaults));
    CHECK(!state.reboot_required);
    CHECK(state.source == CRUX_NETWORK_SOURCE_DEFAULT);
}

static void a_write_only_session_sees_nothing_of_it(void)
{
    net_start();
    CHECK(request(s_writer, &net_set_a) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    crux_network_state_t state;
    CHECK(crux_network_settings_get(s_writer, &state) == CRUX_NETWORK_ERR_DENIED);
}

static const char *const write_order[5] = {
    CRUX_NETWORK_SETTINGS_KEY_ADDRESS, CRUX_NETWORK_SETTINGS_KEY_PREFIX,
    CRUX_NETWORK_SETTINGS_KEY_GATEWAY, CRUX_NETWORK_SETTINGS_KEY_CHECK,
    CRUX_NETWORK_SETTINGS_KEY_USE_SAVED};

/* Each case starts the service anew, so each write that fails is a case. */
static void a_failed_write_is_a_failed_save(size_t failing)
{
    crux_network_state_t state;
    crux_network_settings_t reboot_values;

    net_start();
    config_fault_write_fail_key(write_order[failing], CRUX_CONFIG_ERR_NO_SPACE);
    CHECK(request(s_both, &net_set_a) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    state = state_of(s_both);
    CHECK(state.save == CRUX_NETWORK_SAVE_FAILED);
    CHECK(state.failure == CRUX_NETWORK_FAILURE_STORAGE);
    /* The stored set is not known to be whole, so none is shown as saved. */
    CHECK(!state.has_saved);
    /* Writes stop at the first failure: later records are never tried. */
    CHECK(config_fault_write_log_count() == failing + 1u);
    CHECK(net_events_count() == 1u);
    CHECK(net_events_get(0)->event == CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED);
    CHECK(net_events_get(0)->session.value == s_both.value);
    CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_CHANGED) == 0u);
    /* With nothing stored before, a failure never leaves a set that boots. */
    CHECK(net_reboot_decision(&reboot_values) != CRUX_NETWORK_BOOT_SAVED);

    /* The next accepted request clears the failure, and saves. */
    config_fault_write_fail_key(NULL, CRUX_CONFIG_OK);
    CHECK(request(s_both, &net_set_a) == CRUX_NETWORK_OK);
    CHECK(state_of(s_both).save == CRUX_NETWORK_SAVE_PENDING);
    CHECK(crux_network_settings_work_once());
    state = state_of(s_both);
    CHECK(state.save == CRUX_NETWORK_SAVE_NONE && state.has_saved);
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_SAVED);
    CHECK(net_same(&reboot_values, &net_set_a));
    CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_CHANGED) == 1u);
}

#define FAILING_CASE(index)                                                                        \
    static void failing_##index(void)                                                              \
    {                                                                                              \
        a_failed_write_is_a_failed_save(index);                                                    \
    }
FAILING_CASE(0)
FAILING_CASE(1)
FAILING_CASE(2)
FAILING_CASE(3)
FAILING_CASE(4)

static void a_refused_write_reports_its_kind(void)
{
    net_start();
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_ERR_NOT_SUPPORTED, false});
    CHECK(request(s_both, &net_set_a) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    crux_network_state_t state = state_of(s_both);
    CHECK(state.save == CRUX_NETWORK_SAVE_FAILED &&
          state.failure == CRUX_NETWORK_FAILURE_NOT_SUPPORTED);
}

static void an_ended_session_still_leaves_its_event(void)
{
    net_start();
    CHECK(request(s_both, &net_set_a) == CRUX_NETWORK_OK);
    crux_access_logout(&s_both);
    CHECK(crux_network_settings_work_once());
    CHECK(net_events_count() == 1u);
    CHECK(net_events_get(0)->event == CRUX_NETWORK_SETTINGS_EVENT_CHANGED);
    crux_network_state_t state = state_of(s_admin);
    CHECK(state.has_saved);
}

/* A save with other things marked saves them too: the Configuration service saves marks. */
static void a_save_also_writes_other_marks(void)
{
    crux_config_service_item_t item;

    net_start();
    CHECK(crux_config_service_update(s_saver, NET_CFG_LIMIT, "6", 1, &item) ==
          CRUX_CONFIG_SERVICE_OK);
    CHECK(request(s_both, &net_set_a) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    CHECK(config_fault_write_log_count() == 6u);
    CHECK(strcmp(config_fault_write_log_key(0), NET_CFG_LIMIT) == 0);
    CHECK(net_events_count() == 1u);
}

/* The recovery: the flag is saved false, the values stay, the next save sets it true last. */
static void recovery_saves_the_flag_false(void)
{
    crux_network_boot_report_t report;
    crux_network_state_t state;
    crux_network_settings_t reboot_values;
    const net_options_t keep = {true, 0};

    config_fault_reset();
    net_store_set(&net_set_a, true);
    net_boot_with(&keep, &report);
    CHECK(crux_config_service_start(net_table(), NET_TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_network_settings_start() == CRUX_NETWORK_OK);
    CHECK(state_of(s_both).has_saved);

    size_t denials_before = net_denials();
    CHECK(crux_network_settings_recover(s_reader) == CRUX_NETWORK_ERR_DENIED);
    CHECK(crux_network_settings_recover(s_stranger) == CRUX_NETWORK_ERR_DENIED);
    CHECK(crux_network_settings_recover(s_forced) == CRUX_NETWORK_ERR_DENIED);
    CHECK(crux_network_settings_recover(s_never_valid) == CRUX_NETWORK_ERR_SESSION_INVALID);
    CHECK(net_denials() == denials_before + 3u);
    CHECK(config_fault_writes() == 0u && net_events_count() == 0u);
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_SAVED);

    CHECK(crux_network_settings_recover(s_writer) == CRUX_NETWORK_OK);
    /* Only the flag was written, and it reads false at the next boot. */
    CHECK(config_fault_write_log_count() == 1u);
    CHECK(strcmp(config_fault_write_log_key(0), CRUX_NETWORK_SETTINGS_KEY_USE_SAVED) == 0);
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_DISABLED);
    CHECK(net_events_count() == 1u);
    CHECK(net_events_get(0)->event == CRUX_NETWORK_SETTINGS_EVENT_SAVED_IGNORED);
    CHECK(net_events_get(0)->session.value == s_writer.value);
    state = state_of(s_both);
    CHECK(!state.has_saved);
    CHECK(state.source == CRUX_NETWORK_SOURCE_SAVED);
    /* The interface runs the saved set and the next boot would use the defaults. */
    CHECK(state.reboot_required);

    /* The next successful save sets the flag back, last of the set. */
    config_fault_write_log_clear();
    CHECK(request(s_both, &net_set_b) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_work_once());
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_SAVED);
    CHECK(net_same(&reboot_values, &net_set_b));
    CHECK(strcmp(config_fault_write_log_key(config_fault_write_log_count() - 1u),
                 CRUX_NETWORK_SETTINGS_KEY_USE_SAVED) == 0);
    CHECK(state_of(s_both).has_saved);
    /* Running it twice is harmless. */
    CHECK(crux_network_settings_recover(s_both) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_recover(s_both) == CRUX_NETWORK_OK);
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_DISABLED);
}

static void recovery_failure_is_reported(void)
{
    crux_network_settings_t reboot_values;

    net_start();
    net_store_set(&net_set_a, true);
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_ERR_IO, false});
    CHECK(crux_network_settings_recover(s_both) == CRUX_NETWORK_ERR_STORE);
    CHECK(net_events_count() == 1u);
    CHECK(net_events_get(0)->event == CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED);
    CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_SAVED_IGNORED) == 0u);
    /* The flag was not stored, and the saved set is still what a reset would use. */
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_SAVED);
    /* Run again once the store works. */
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_OK, false});
    CHECK(crux_network_settings_recover(s_both) == CRUX_NETWORK_OK);
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_DISABLED);
}

static void recovery_on_a_device_that_never_saved(void)
{
    crux_network_settings_t reboot_values;

    net_start();
    CHECK(crux_network_settings_recover(s_both) == CRUX_NETWORK_OK);
    CHECK(net_reboot_decision(&reboot_values) == CRUX_NETWORK_BOOT_DISABLED);
}

int main(void)
{
    net_run_case("not ready before start", not_ready_before_start);
    net_run_case("starts on the table", start_with_the_table);
    net_run_case("read permission", read_permission);
    net_run_case("write permission", write_permission);
    net_run_case("defaults show as running", defaults_show_as_running);
    net_run_case("saved values show as the source", saved_values_show_as_the_source);
    net_run_case("invalid settings are refused whole", invalid_settings_are_refused_whole);
    net_run_case("a save runs on the worker step", a_save_runs_on_the_worker_step);
    net_run_case("saving the running values needs no reboot",
                 saving_the_running_values_needs_no_reboot);
    net_run_case("a write-only session sees nothing", a_write_only_session_sees_nothing_of_it);
    net_run_case("a failed address write is a failed save", failing_0);
    net_run_case("a failed prefix write is a failed save", failing_1);
    net_run_case("a failed gateway write is a failed save", failing_2);
    net_run_case("a failed check key write is a failed save", failing_3);
    net_run_case("a failed flag write is a failed save", failing_4);
    net_run_case("a refused write reports its kind", a_refused_write_reports_its_kind);
    net_run_case("an ended session still leaves its event",
                 an_ended_session_still_leaves_its_event);
    net_run_case("a save also writes other marks", a_save_also_writes_other_marks);
    net_run_case("recovery saves the flag false", recovery_saves_the_flag_false);
    net_run_case("a failed recovery is reported", recovery_failure_is_reported);
    net_run_case("recovery on a device that never saved", recovery_on_a_device_that_never_saved);
    return net_finish("network_settings_service");
}
