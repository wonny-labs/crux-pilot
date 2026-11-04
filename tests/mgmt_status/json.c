/* The JSON forms, byte for byte. check_openapi_status.py holds the same forms to the merged schema,
 * and this suite holds them to their exact text and to what can go wrong in the buffers. */
#include "status_test.h"

static crux_mgmt_status_entry_t ok_entry(const char *name, crux_mgmt_status_value_t value)
{
    crux_mgmt_status_entry_t entry = {.name = name, .status = CRUX_OK, .value = value};
    return entry;
}

static crux_mgmt_status_entry_t failed_entry(const char *name, crux_status_t status)
{
    crux_mgmt_status_entry_t entry = {.name = name, .status = status};
    return entry;
}

static crux_mgmt_status_value_t text_value(const char *text)
{
    crux_mgmt_status_value_t value = {.type = CRUX_MGMT_STATUS_TEXT};
    strncpy(value.data.text, text, sizeof(value.data.text) - 1u);
    return value;
}

static void expect_entry(const crux_mgmt_status_entry_t *entry, const char *expected)
{
    char out[CRUX_MGMT_STATUS_JSON_ENTRY_SIZE];
    size_t length = 0;
    CHECK(crux_mgmt_status_entry_json(entry, out, sizeof(out), &length) == CRUX_OK);
    if (strcmp(out, expected) != 0) {
        printf("got %s\nexpected %s\n", out, expected);
    }
    CHECK(strcmp(out, expected) == 0);
    CHECK(length == strlen(expected));

    /* Exactly what fits, then one short of it. */
    char exact[CRUX_MGMT_STATUS_JSON_ENTRY_SIZE];
    size_t exact_length = 0;
    CHECK(crux_mgmt_status_entry_json(entry, exact, length + 1u, &exact_length) == CRUX_OK);
    CHECK(strcmp(exact, expected) == 0);
    size_t unchanged = 99;
    CHECK(crux_mgmt_status_entry_json(entry, exact, length, &unchanged) == CRUX_ERR_NO_RESOURCES);
    CHECK(unchanged == 99);
}

static void entry_forms(void)
{
    crux_mgmt_status_entry_t entry =
        ok_entry("system.uptime_ms", (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_U64,
                                                                .data.u64 = UINT64_C(86400000)});
    expect_entry(&entry, "{\"name\":\"system.uptime_ms\",\"status\":\"ok\",\"type\":\"u64\","
                         "\"value\":\"86400000\"}");

    /* Above 2^53, where a browser's number would lose it. */
    entry.value.data.u64 = UINT64_C(18446744073709551615);
    expect_entry(&entry, "{\"name\":\"system.uptime_ms\",\"status\":\"ok\",\"type\":\"u64\","
                         "\"value\":\"18446744073709551615\"}");
    entry.value.data.u64 = 0;
    expect_entry(
        &entry,
        "{\"name\":\"system.uptime_ms\",\"status\":\"ok\",\"type\":\"u64\",\"value\":\"0\"}");

    entry = ok_entry("net.link", (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_BOOL,
                                                            .data.boolean = true});
    expect_entry(&entry,
                 "{\"name\":\"net.link\",\"status\":\"ok\",\"type\":\"bool\",\"value\":true}");
    entry.value.data.boolean = false;
    expect_entry(&entry,
                 "{\"name\":\"net.link\",\"status\":\"ok\",\"type\":\"bool\",\"value\":false}");

    entry = ok_entry("app.cycle_count",
                     (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_U32, .data.u32 = 42});
    expect_entry(&entry,
                 "{\"name\":\"app.cycle_count\",\"status\":\"ok\",\"type\":\"u32\",\"value\":42}");
    entry.value.data.u32 = UINT32_MAX;
    expect_entry(
        &entry,
        "{\"name\":\"app.cycle_count\",\"status\":\"ok\",\"type\":\"u32\",\"value\":4294967295}");

    entry = ok_entry("net.ipv4", text_value("192.0.2.10"));
    expect_entry(
        &entry,
        "{\"name\":\"net.ipv4\",\"status\":\"ok\",\"type\":\"text\",\"value\":\"192.0.2.10\"}");
    entry.value = text_value("");
    expect_entry(&entry,
                 "{\"name\":\"net.ipv4\",\"status\":\"ok\",\"type\":\"text\",\"value\":\"\"}");

    /* Only the two characters JSON needs escaped can be there: the character rule keeps out
     * controls. */
    entry.value = text_value("a\"b\\c/d");
    expect_entry(
        &entry,
        "{\"name\":\"net.ipv4\",\"status\":\"ok\",\"type\":\"text\",\"value\":\"a\\\"b\\\\c/d\"}");
    /* UTF-8 passes through, including what the rule allows and a client must still escape to show.
     */
    entry.value = text_value("\xc3\xa9\xe2\x80\xa8");
    expect_entry(&entry, "{\"name\":\"net.ipv4\",\"status\":\"ok\",\"type\":\"text\",\"value\":\""
                         "\xc3\xa9\xe2\x80\xa8\"}");
}

static void failed_forms(void)
{
    /* A failed item has no type or value, and each Base result has its word. */
    static const struct {
        crux_status_t status;
        const char *word;
    } results[] = {{CRUX_ERR_INVALID_ARGUMENT, "invalid_argument"},
                   {CRUX_ERR_OUT_OF_RANGE, "out_of_range"},
                   {CRUX_ERR_NOT_READY, "not_ready"},
                   {CRUX_ERR_NO_DATA, "no_data"},
                   {CRUX_ERR_IO, "io"},
                   {CRUX_ERR_BUSY, "busy"},
                   {CRUX_ERR_TIMEOUT, "timeout"},
                   {CRUX_ERR_NO_RESOURCES, "no_resources"},
                   {CRUX_ERR_NOT_SUPPORTED, "not_supported"},
                   {CRUX_ERR_INVALID_STATE, "invalid_state"}};
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
        crux_mgmt_status_entry_t entry = failed_entry("system.watchdog_state", results[i].status);
        char expected[128];
        snprintf(expected, sizeof(expected),
                 "{\"name\":\"system.watchdog_state\",\"status\":\"%s\"}", results[i].word);
        expect_entry(&entry, expected);
        CHECK(strcmp(crux_mgmt_status_result_name(results[i].status), results[i].word) == 0);
    }
    CHECK(strcmp(crux_mgmt_status_result_name(CRUX_OK), "ok") == 0);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    CHECK(crux_mgmt_status_result_name((crux_status_t)-99) == NULL);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    CHECK(crux_mgmt_status_result_name((crux_status_t)1) == NULL);
}

static void refuses_what_the_service_cannot_produce(void)
{
    char out[CRUX_MGMT_STATUS_JSON_ENTRY_SIZE];
    size_t length = 99;
    crux_mgmt_status_entry_t good = failed_entry("name", CRUX_ERR_IO);
    CHECK(crux_mgmt_status_entry_json(NULL, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_mgmt_status_entry_json(&good, NULL, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_mgmt_status_entry_json(&good, out, sizeof(out), NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_mgmt_status_entry_json(&good, out, 0, &length) == CRUX_ERR_NO_RESOURCES);

    crux_mgmt_status_entry_t bad = good;
    bad.name = NULL;
    CHECK(crux_mgmt_status_entry_json(&bad, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    /* A name out of the pattern could break out of the string. */
    bad.name = "na\"me";
    CHECK(crux_mgmt_status_entry_json(&bad, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    bad.name = "UPPER";
    CHECK(crux_mgmt_status_entry_json(&bad, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    bad.name = "";
    CHECK(crux_mgmt_status_entry_json(&bad, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    /* A name longer than any item's. */
    char long_name[129];
    memset(long_name, 'n', sizeof(long_name));
    long_name[128] = '\0';
    bad.name = long_name;
    CHECK(crux_mgmt_status_entry_json(&bad, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);

    bad = good;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    bad.status = (crux_status_t)-99;
    CHECK(crux_mgmt_status_entry_json(&bad, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);

    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    bad = ok_entry("name", (crux_mgmt_status_value_t){.type = (crux_mgmt_status_type_t)99});
    CHECK(crux_mgmt_status_entry_json(&bad, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    bad = ok_entry("name", (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_TEXT});
    memset(bad.value.data.text, 'x', sizeof(bad.value.data.text));
    CHECK(crux_mgmt_status_entry_json(&bad, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    bad.value = text_value("line\nbreak");
    CHECK(crux_mgmt_status_entry_json(&bad, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    CHECK(length == 99);
}

static crux_status_t worst_read(void *context, crux_mgmt_status_value_t *out)
{
    (void)context;
    out->type = CRUX_MGMT_STATUS_TEXT;
    /* Every byte needs its escape, the longest a text item can be as JSON. */
    memset(out->data.text, '"', CRUX_MGMT_STATUS_TEXT_SIZE - 1u);
    return CRUX_OK;
}

static void the_promised_buffer_holds_the_worst_case(void)
{
    fixture_boot();
    static const crux_mgmt_status_item_t items[] = {
        {"1234567890123456789012345678901", "app.read", worst_read, NULL},
    };
    crux_mgmt_status_config_t config = fixture_config();
    config.project_items = items;
    config.project_item_count = 1;
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    char out[CRUX_MGMT_STATUS_JSON_ENTRY_SIZE];
    size_t length = 0;
    size_t longest = 0;
    while (crux_mgmt_status_next(s_admin, &cursor, &entry) == CRUX_OK) {
        CHECK(crux_mgmt_status_entry_json(&entry, out, sizeof(out), &length) == CRUX_OK);
        if (length > longest) {
            longest = length;
        }
    }
    CHECK(longest < CRUX_MGMT_STATUS_JSON_ENTRY_SIZE);
    /* 31 for the name, 126 for the escaped text, and the object's own characters. */
    CHECK(longest >= 31u + 126u);
    /* A built-in's longest name, with the longest answer it can have. */
    crux_mgmt_status_entry_t builtin =
        ok_entry("security.credential_rollback_detected",
                 (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_U64});
    builtin.value.data.u64 = UINT64_MAX;
    CHECK(crux_mgmt_status_entry_json(&builtin, out, sizeof(out), &length) == CRUX_OK);
}

static void report_forms(void)
{
    fixture_boot();
    /* A small set, so that the whole document can be compared. */
    crux_mgmt_status_credential_t credential = {"valid", "none", "host_fake", false, false, false};
    crux_mgmt_status_event_counts_t counts = {0};
    status_fake_credential(false, CRUX_OK, credential);
    status_fake_events(false, CRUX_OK, counts);
    crux_mgmt_status_config_t config = fixture_config();
    config.boot_reset_cause_status = CRUX_ERR_NOT_SUPPORTED;
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    status_fake_system(CRUX_ERR_IO, CRUX_OK, CRUX_WATCHDOG_ACTIVE);

    char out[2048];
    size_t length = 0;
    CHECK(crux_mgmt_status_report_json(s_viewer, out, sizeof(out), &length) == CRUX_OK);
    const char *expected =
        "{\"items\":["
        "{\"name\":\"system.uptime_ms\",\"status\":\"ok\",\"type\":\"u64\",\"value\":\"1000\"},"
        "{\"name\":\"system.clock\",\"status\":\"ok\",\"type\":\"text\",\"value\":\"rtc\"},"
        "{\"name\":\"system.watchdog_state\",\"status\":\"io\"},"
        "{\"name\":\"reset.causes\",\"status\":\"not_supported\"},"
        "{\"name\":\"reset.supported\",\"status\":\"not_supported\"},"
        "{\"name\":\"net.link\",\"status\":\"ok\",\"type\":\"bool\",\"value\":true},"
        "{\"name\":\"net.admin_up\",\"status\":\"ok\",\"type\":\"bool\",\"value\":true},"
        "{\"name\":\"net.ipv4\",\"status\":\"ok\",\"type\":\"text\",\"value\":\"192.0.2.10\"},"
        "{\"name\":\"version.platform\",\"status\":\"ok\",\"type\":\"text\",\"value\":\"1.2.3\"},"
        "{\"name\":\"version.project\",\"status\":\"ok\",\"type\":\"text\",\"value\":\"pilot-7\"}"
        "]}";
    if (strcmp(out, expected) != 0) {
        printf("got      %s\nexpected %s\n", out, expected);
    }
    CHECK(strcmp(out, expected) == 0);
    CHECK(length == strlen(expected));

    /* Exactly what fits, then one short. */
    char exact[2048];
    CHECK(crux_mgmt_status_report_json(s_viewer, exact, length + 1u, &length) == CRUX_OK);
    CHECK(strcmp(exact, expected) == 0);
    size_t unchanged = 99;
    for (size_t capacity = 0; capacity <= length; ++capacity) {
        CHECK(crux_mgmt_status_report_json(s_viewer, exact, capacity, &unchanged) ==
              CRUX_ERR_NO_RESOURCES);
    }
    CHECK(unchanged == 99);

    /* A session that may see nothing gets the empty list and never a refusal, and that is not an
     * event either. */
    access_fake_events_clear();
    CHECK(crux_mgmt_status_report_json(s_none, out, sizeof(out), &length) == CRUX_OK);
    CHECK(strcmp(out, "{\"items\":[]}") == 0);
    CHECK(length == strlen("{\"items\":[]}"));
    CHECK(access_fake_event_count() == 0);
    CHECK(crux_mgmt_status_report_json(s_none, out, strlen("{\"items\":[]}"), &length) ==
          CRUX_ERR_NO_RESOURCES);

    /* As during a forced password change. */
    CHECK(crux_access_account_reset_password(s_admin, "viewer", ACCESS_TEST_PASSWORD,
                                             strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    crux_access_session_t forced =
        access_test_session("viewer", ACCESS_TEST_PASSWORD, &access_test_web);
    access_fake_events_clear();
    CHECK(crux_mgmt_status_report_json(forced, out, sizeof(out), &length) == CRUX_OK);
    CHECK(strcmp(out, "{\"items\":[]}") == 0);
    CHECK(access_fake_event_count() == 0);

    /* An ended session has its own result, which an adapter turns into a 401. */
    crux_access_logout(&forced);
    CHECK(crux_mgmt_status_report_json(forced, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_STATE);
    CHECK(crux_mgmt_status_report_json(s_never_valid, out, sizeof(out), &length) ==
          CRUX_ERR_INVALID_STATE);
    CHECK(crux_mgmt_status_report_json(s_none, NULL, sizeof(out), &length) ==
          CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_mgmt_status_report_json(s_none, out, sizeof(out), NULL) ==
          CRUX_ERR_INVALID_ARGUMENT);
}

static void report_before_start(void)
{
    fixture_boot();
    char out[64];
    size_t length = 0;
    CHECK(crux_mgmt_status_report_json(s_viewer, out, sizeof(out), &length) == CRUX_ERR_NOT_READY);
}

int main(void)
{
    run_case("entry forms", entry_forms);
    run_case("failed forms", failed_forms);
    run_case("refuses what the service cannot produce", refuses_what_the_service_cannot_produce);
    run_case("the promised buffer holds the worst case", the_promised_buffer_holds_the_worst_case);
    run_case("report forms", report_forms);
    run_case("report before start", report_before_start);
    return suite_finish("MgmtStatus.json");
}
