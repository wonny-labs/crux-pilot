/* Saving in table order, marks, partial failures and their events. */
#include "service_test.h"

static void saves_only_what_is_marked(void)
{
    fixture_start();
    crux_config_service_save_report_t report = {99, "stale", CRUX_CONFIG_ERR_IO};
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 0 && report.failed_name == NULL &&
          report.failed_result == CRUX_CONFIG_OK);
    CHECK(config_fault_writes() == 0);
    CHECK(config_events_count() == 0);

    CHECK(update_text(s_both, NAME_HOST, "b") == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_both, NAME_MODE, "true") == CRUX_CONFIG_SERVICE_OK);
    config_events_clear();
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 2);
    CHECK(config_fault_writes() == 2);

    /* In table order, not in the order of the updates. */
    CHECK(config_events_count() == 2);
    CHECK(config_events_get(0)->event == CRUX_CONFIG_SERVICE_EVENT_SAVED);
    CHECK(strcmp(config_events_get(0)->name, NAME_MODE) == 0);
    CHECK(strcmp(config_events_get(1)->name, NAME_HOST) == 0);
    CHECK(config_events_get(0)->session.value == s_admin.value);

    /* What was stored is what a restart would load. */
    uint8_t bytes[8] = {0};
    const uint8_t host_wire[] = {1, 3, 1, 0, 'b'};
    CHECK(config_fault_bytes(NAME_HOST, bytes, sizeof(bytes)) == sizeof(host_wire));
    CHECK(memcmp(bytes, host_wire, sizeof(host_wire)) == 0);

    /* The marks cleared, so a second save does nothing. */
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 0);
    CHECK(config_fault_writes() == 2);

    /* Every accepted update marks, with no comparison of the values. */
    CHECK(update_text(s_both, NAME_HOST, "b") == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 1);
}

static void stops_at_the_first_failure(void)
{
    fixture_start();
    CHECK(update_text(s_both, NAME_MODE, "true") == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_both, NAME_LIMIT, "6") == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_both, NAME_HOST, "abc") == CRUX_CONFIG_SERVICE_OK);
    config_events_clear();
    config_fault_write_fail_key(NAME_LIMIT, CRUX_CONFIG_ERR_NO_SPACE);

    crux_config_service_save_report_t report = {0};
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(report.saved == 1);
    CHECK(strcmp(report.failed_name, NAME_LIMIT) == 0);
    CHECK(report.failed_result == CRUX_CONFIG_ERR_NO_SPACE);
    /* The third was never tried. */
    CHECK(config_fault_writes() == 2);
    uint8_t bytes[8];
    CHECK(config_fault_bytes(NAME_HOST, bytes, sizeof(bytes)) == 0);

    /* One event for what saved, one for what failed, and none for what was not tried. */
    CHECK(config_events_count() == 2);
    CHECK(config_events_get(0)->event == CRUX_CONFIG_SERVICE_EVENT_SAVED);
    CHECK(strcmp(config_events_get(0)->name, NAME_MODE) == 0);
    CHECK(config_events_get(1)->event == CRUX_CONFIG_SERVICE_EVENT_SAVE_FAILED);
    CHECK(strcmp(config_events_get(1)->name, NAME_LIMIT) == 0);

    /* The value stays accepted, and the marks stay on what did not save. */
    expect_u32(NAME_LIMIT, 6);
    config_fault_write_fail_key(NULL, CRUX_CONFIG_OK);
    config_events_clear();
    size_t writes = config_fault_writes();
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 2);
    CHECK(config_fault_writes() == writes + 2);
    CHECK(strcmp(config_events_get(0)->name, NAME_LIMIT) == 0);
}

static void failed_save_may_have_changed_bytes(void)
{
    fixture_start();
    CHECK(update_text(s_both, NAME_LIMIT, "6") == CRUX_CONFIG_SERVICE_OK);
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_ERR_IO, true});
    crux_config_service_save_report_t report = {0};
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(report.failed_result == CRUX_CONFIG_ERR_IO);
    uint8_t bytes[8];
    CHECK(config_fault_bytes(NAME_LIMIT, bytes, sizeof(bytes)) == 8);
    /* The record may be there, yet the mark stays on, because nothing was acknowledged. */
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_OK, false});
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 1);
}

static bool allow_validator(const crux_config_value_t *value, void *context)
{
    (void)value;
    return *(bool *)context;
}

static void validator_refuses_at_save(void)
{
    fixture_boot();
    /* A validator whose context the application changed after an accepted update. */
    static bool allow = true;
    static const crux_config_spec_t spec = {
        "guarded", 1, {.type = CRUX_CONFIG_U32, .data.u32 = 1}, allow_validator, &allow};
    static crux_config_param_t parameter;
    CHECK(crux_config_init(&parameter, &spec) == CRUX_CONFIG_OK);
    static const crux_config_service_entry_t table[] = {
        {&parameter, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION}};
    CHECK(crux_config_service_start(table, 1) == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_both, "guarded", "2") == CRUX_CONFIG_SERVICE_OK);
    allow = false;
    crux_config_service_save_report_t report = {0};
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(strcmp(report.failed_name, "guarded") == 0);
    CHECK(report.failed_result == CRUX_CONFIG_ERR_REJECTED);
    CHECK(config_fault_writes() == 0);
}

static void no_provider_result_passes_through(void)
{
    fixture_start();
    CHECK(update_text(s_both, NAME_LIMIT, "6") == CRUX_CONFIG_SERVICE_OK);
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_ERR_NOT_SUPPORTED, false});
    crux_config_service_save_report_t report = {0};
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(strcmp(report.failed_name, NAME_LIMIT) == 0);
    CHECK(report.failed_result == CRUX_CONFIG_ERR_NOT_SUPPORTED);
    CHECK(crux_config_service_save_local(&report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(report.failed_result == CRUX_CONFIG_ERR_NOT_SUPPORTED);
}

static void application_save(void)
{
    fixture_start();
    /* A parameter kept private, as a composition keeps its own: table order decides the last. */
    crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 11};
    CHECK(crux_config_service_try_update(NAME_PRIVATE, &value) == CRUX_CONFIG_SERVICE_OK);
    value.data.boolean = true;
    value.type = CRUX_CONFIG_BOOL;
    CHECK(crux_config_service_try_update(NAME_MODE, &value) == CRUX_CONFIG_SERVICE_OK);
    config_fault_write_fail_key(NAME_PRIVATE, CRUX_CONFIG_ERR_IO);

    crux_config_service_save_report_t report = {0};
    CHECK(crux_config_service_save_local(&report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(report.saved == 1);
    CHECK(strcmp(report.failed_name, NAME_PRIVATE) == 0);
    uint8_t bytes[8];
    CHECK(config_fault_bytes(NAME_MODE, bytes, sizeof(bytes)) == 5);
    CHECK(config_events_count() == 0);
}

/* A session holding only config.save has an empty listing, so its report names nothing. */
static void report_hides_what_the_session_cannot_see(void)
{
    fixture_start();
    size_t cursor = 0;
    crux_config_service_item_t item;
    CHECK(crux_config_service_next(s_saver, &cursor, &item) == CRUX_CONFIG_SERVICE_END);
    CHECK(update_text(s_both, NAME_MODE, "true") == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_both, NAME_LIMIT, "6") == CRUX_CONFIG_SERVICE_OK);
    config_events_clear();
    config_fault_write_fail_key(NAME_LIMIT, CRUX_CONFIG_ERR_NO_SPACE);

    crux_config_service_save_report_t report = {99, "stale", CRUX_CONFIG_OK};
    CHECK(crux_config_service_save(s_saver, &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(report.failed_name == NULL);
    CHECK(report.saved == 0);
    /* The failure itself stays known, so that a caller can tell it from a success. */
    CHECK(report.failed_result == CRUX_CONFIG_ERR_NO_SPACE);

    /* The save did happen, and the operator's record names both. */
    uint8_t bytes[8];
    CHECK(config_fault_bytes(NAME_MODE, bytes, sizeof(bytes)) == 5);
    CHECK(config_events_count() == 2);
    CHECK(config_events_get(0)->event == CRUX_CONFIG_SERVICE_EVENT_SAVED);
    CHECK(strcmp(config_events_get(0)->name, NAME_MODE) == 0);
    CHECK(config_events_get(1)->event == CRUX_CONFIG_SERVICE_EVENT_SAVE_FAILED);
    CHECK(strcmp(config_events_get(1)->name, NAME_LIMIT) == 0);

    /* A later clean save counts nothing either, while it writes what was left. */
    config_fault_write_fail_key(NULL, CRUX_CONFIG_OK);
    size_t writes = config_fault_writes();
    CHECK(crux_config_service_save(s_saver, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 0 && report.failed_name == NULL);
    CHECK(config_fault_writes() == writes + 1);
}

/* Read permission alone, or write permission alone, makes a parameter visible, as in a listing. */
static void report_names_what_the_session_can_see(void)
{
    fixture_start();
    /* Room for the two logins, in a session table the fixture has nearly filled. */
    crux_access_logout(&s_stranger);
    crux_access_logout(&s_reader);
    CHECK(crux_access_account_create(s_admin, "read_saver", "read_saver", ACCESS_TEST_PASSWORD,
                                     strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(s_admin, "write_saver", "write_saver", ACCESS_TEST_PASSWORD,
                                     strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    const crux_access_session_t sessions[] = {fixture_login("read_saver"),
                                              fixture_login("write_saver"), s_admin};
    for (size_t i = 0; i < sizeof(sessions) / sizeof(sessions[0]); ++i) {
        /* One that saves and one that fails, both visible. */
        CHECK(update_text(s_both, NAME_MODE, "true") == CRUX_CONFIG_SERVICE_OK);
        CHECK(update_text(s_both, NAME_LIMIT, "6") == CRUX_CONFIG_SERVICE_OK);
        config_fault_write_fail_key(NAME_LIMIT, CRUX_CONFIG_ERR_IO);
        crux_config_service_save_report_t report = {0};
        CHECK(crux_config_service_save(sessions[i], &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
        CHECK(report.saved == 1);
        CHECK(report.failed_name != NULL && strcmp(report.failed_name, NAME_LIMIT) == 0);
        CHECK(report.failed_result == CRUX_CONFIG_ERR_IO);
        config_fault_write_fail_key(NULL, CRUX_CONFIG_OK);
        CHECK(crux_config_service_save(sessions[i], &report) == CRUX_CONFIG_SERVICE_OK);
        CHECK(report.saved == 1);
    }
}

/* A session that sees some of what a save covers is told about those only. */
static void report_counts_only_what_the_session_can_see(void)
{
    fixture_start();
    crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 11};
    CHECK(crux_config_service_try_update(NAME_MIDDLE, &value) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_try_update(NAME_PRIVATE, &value) == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_both, NAME_MODE, "true") == CRUX_CONFIG_SERVICE_OK);

    /* Three written, and one that this session may know of. */
    size_t writes = config_fault_writes();
    crux_config_service_save_report_t report = {0};
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(config_fault_writes() == writes + 3);
    CHECK(report.saved == 1);

    /* A failure of a hidden parameter, after a visible one was saved. */
    CHECK(crux_config_service_try_update(NAME_MIDDLE, &value) == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_both, NAME_MODE, "false") == CRUX_CONFIG_SERVICE_OK);
    config_fault_write_fail_key(NAME_MIDDLE, CRUX_CONFIG_ERR_IO);
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(report.saved == 1);
    CHECK(report.failed_name == NULL);
    CHECK(report.failed_result == CRUX_CONFIG_ERR_IO);

    /* The application's own save names it: it owns the table. */
    crux_config_service_save_report_t local = {0};
    CHECK(crux_config_service_save_local(&local) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(local.failed_name != NULL && strcmp(local.failed_name, NAME_MIDDLE) == 0);
}

/* A probe that fails denies: a session ended during its own save learns no name. */
static void end_a_session(void *context)
{
    crux_access_logout(context);
    config_fault_write_hook(NULL, NULL);
}

static void report_hides_on_a_failed_probe(void)
{
    fixture_start();
    CHECK(update_text(s_both, NAME_LIMIT, "6") == CRUX_CONFIG_SERVICE_OK);
    crux_access_session_t ending = s_admin;
    config_fault_write_hook(end_a_session, &ending);
    config_fault_write_fail_key(NAME_LIMIT, CRUX_CONFIG_ERR_IO);
    crux_config_service_save_report_t report = {0};
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(report.failed_name == NULL);
    CHECK(report.saved == 0);
    CHECK(report.failed_result == CRUX_CONFIG_ERR_IO);
}

/* The port runs with no service lock held, so the service answers from inside it. */
static void reenter_from_event(void *context)
{
    crux_config_value_t value;
    crux_config_service_result_t *result = context;
    *result = crux_config_service_try_read(NAME_LIMIT, &value, NULL, 0);
}

static void events_run_with_no_lock_held(void)
{
    fixture_start();
    crux_config_service_result_t seen = CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    config_events_hook(reenter_from_event, &seen);

    CHECK(update_text(s_both, NAME_LIMIT, "6") == CRUX_CONFIG_SERVICE_OK);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_UPDATED) == 1);
    CHECK(seen == CRUX_CONFIG_SERVICE_OK);

    seen = CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    CHECK(update_text(s_both, NAME_LIMIT, "1000") == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    CHECK(seen == CRUX_CONFIG_SERVICE_OK);

    seen = CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(seen == CRUX_CONFIG_SERVICE_OK);

    seen = CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    CHECK(update_text(s_both, NAME_LIMIT, "7") == CRUX_CONFIG_SERVICE_OK);
    config_fault_write_fail_key(NAME_LIMIT, CRUX_CONFIG_ERR_IO);
    seen = CRUX_CONFIG_SERVICE_ERR_NOT_READY;
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_ERR_STORE);
    CHECK(seen == CRUX_CONFIG_SERVICE_OK);
    config_events_hook(NULL, NULL);
}

int main(void)
{
    run_case("saves only what is marked", saves_only_what_is_marked);
    run_case("stops at the first failure", stops_at_the_first_failure);
    run_case("failed save may have changed bytes", failed_save_may_have_changed_bytes);
    run_case("validator refuses at save", validator_refuses_at_save);
    run_case("no provider result passes through", no_provider_result_passes_through);
    run_case("application save", application_save);
    run_case("report hides what the session cannot see", report_hides_what_the_session_cannot_see);
    run_case("report names what the session can see", report_names_what_the_session_can_see);
    run_case("report counts only what the session can see",
             report_counts_only_what_the_session_can_see);
    run_case("report hides on a failed probe", report_hides_on_a_failed_probe);
    run_case("events run with no lock held", events_run_with_no_lock_held);
    return suite_finish("ConfigService.save");
}
