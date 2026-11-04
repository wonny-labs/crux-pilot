/* Built-in items, project items, filtering and start. */
#include "status_test.h"

#include "text_vectors.h"

/* Past the policy's idle limit of 15 minutes. */
#define IDLE_LIMIT_PASSED_MS UINT64_C(960000)

static void expect_order(crux_access_session_t session, const char *const *names, size_t count)
{
    size_t cursor = 0;
    for (size_t i = 0; i < count; ++i) {
        crux_mgmt_status_entry_t entry;
        CHECK(crux_mgmt_status_next(session, &cursor, &entry) == CRUX_OK);
        if (strcmp(entry.name, names[i]) != 0) {
            printf("position %zu: %s, expected %s\n", i, entry.name, names[i]);
        }
        CHECK(strcmp(entry.name, names[i]) == 0);
    }
    crux_mgmt_status_entry_t entry = {.name = "untouched"};
    /* The end says nothing about how many items there are beyond what was shown. */
    size_t before_end = cursor;
    CHECK(crux_mgmt_status_next(session, &cursor, &entry) == CRUX_ERR_NO_DATA);
    CHECK(strcmp(entry.name, "untouched") == 0);
    CHECK(cursor == before_end);
    /* The end stays the end. */
    CHECK(crux_mgmt_status_next(session, &cursor, &entry) == CRUX_ERR_NO_DATA);
}

static const char *const all_builtins[] = {"system.uptime_ms",
                                           "system.clock",
                                           "system.watchdog_state",
                                           "reset.causes",
                                           "reset.supported",
                                           "net.link",
                                           "net.admin_up",
                                           "net.ipv4",
                                           "version.platform",
                                           "version.project",
                                           "security.credential_state",
                                           "security.credential_basis",
                                           "security.credential_evidence",
                                           "security.credential_confidential",
                                           "security.credential_forgery_detected",
                                           "security.credential_rollback_detected",
                                           "security.events_emitted",
                                           "security.events_dropped",
                                           "security.events_delivered",
                                           "security.events_log_errors"};
enum { ALL_BUILTINS = sizeof(all_builtins) / sizeof(all_builtins[0]) };

static void builtin_values(void)
{
    fixture_start();
    unsigned saves = access_fake_store_saves();
    expect_order(s_viewer, all_builtins, ALL_BUILTINS);

    crux_mgmt_status_entry_t entry = entry_named(s_viewer, "system.uptime_ms");
    CHECK(entry.status == CRUX_OK && entry.value.type == CRUX_MGMT_STATUS_U64);
    CHECK(entry.value.data.u64 == 1000u);

    expect_text_item(s_viewer, "system.clock", "rtc");
    expect_text_item(s_viewer, "system.watchdog_state", "active");

    entry = entry_named(s_viewer, "reset.causes");
    CHECK(entry.status == CRUX_OK && entry.value.type == CRUX_MGMT_STATUS_U32);
    CHECK(entry.value.data.u32 == (CRUX_RESET_CAUSE_POR | CRUX_RESET_CAUSE_WATCHDOG));
    entry = entry_named(s_viewer, "reset.supported");
    CHECK(entry.status == CRUX_OK && entry.value.type == CRUX_MGMT_STATUS_U32);
    CHECK(entry.value.data.u32 ==
          (CRUX_RESET_CAUSE_POR | CRUX_RESET_CAUSE_WATCHDOG | CRUX_RESET_CAUSE_PIN));

    entry = entry_named(s_viewer, "net.link");
    CHECK(entry.status == CRUX_OK && entry.value.type == CRUX_MGMT_STATUS_BOOL);
    CHECK(entry.value.data.boolean);
    entry = entry_named(s_viewer, "net.admin_up");
    CHECK(entry.status == CRUX_OK && entry.value.type == CRUX_MGMT_STATUS_BOOL);
    CHECK(entry.value.data.boolean);
    expect_text_item(s_viewer, "net.ipv4", "192.0.2.10");

    expect_text_item(s_viewer, "version.platform", "1.2.3");
    expect_text_item(s_viewer, "version.project", "pilot-7");

    /* Every field of the store's declaration, so that a state never appears without its basis. */
    expect_text_item(s_viewer, "security.credential_state", "valid");
    expect_text_item(s_viewer, "security.credential_basis", "device_id_key");
    expect_text_item(s_viewer, "security.credential_evidence", "software");
    entry = entry_named(s_viewer, "security.credential_confidential");
    CHECK(entry.status == CRUX_OK && entry.value.type == CRUX_MGMT_STATUS_BOOL);
    CHECK(!entry.value.data.boolean);
    entry = entry_named(s_viewer, "security.credential_forgery_detected");
    CHECK(entry.status == CRUX_OK && entry.value.data.boolean);
    entry = entry_named(s_viewer, "security.credential_rollback_detected");
    CHECK(entry.status == CRUX_OK && !entry.value.data.boolean);

    entry = entry_named(s_viewer, "security.events_emitted");
    CHECK(entry.value.type == CRUX_MGMT_STATUS_U32 && entry.value.data.u32 == 11);
    entry = entry_named(s_viewer, "security.events_dropped");
    CHECK(entry.value.type == CRUX_MGMT_STATUS_U32 && entry.value.data.u32 == 22);
    entry = entry_named(s_viewer, "security.events_delivered");
    CHECK(entry.value.type == CRUX_MGMT_STATUS_U32 && entry.value.data.u32 == 33);
    entry = entry_named(s_viewer, "security.events_log_errors");
    CHECK(entry.value.type == CRUX_MGMT_STATUS_U32 && entry.value.data.u32 == 44);

    /* Reading status left no event, and touched no storage. */
    CHECK(access_fake_event_count() == 0);
    CHECK(access_fake_store_saves() == saves);
}

static void clock_and_watchdog_words(void)
{
    /* Each runs on a fresh snapshot, so that the word comes from a new read. */
    static const struct {
        crux_mgmt_status_clock_t value;
        const char *word;
    } clocks[] = {{CRUX_MGMT_STATUS_CLOCK_UNSET, "unset"},
                  {CRUX_MGMT_STATUS_CLOCK_UNKNOWN, "unknown"},
                  {CRUX_MGMT_STATUS_CLOCK_RTC, "rtc"},
                  {CRUX_MGMT_STATUS_CLOCK_MANUAL, "manual"},
                  {CRUX_MGMT_STATUS_CLOCK_EXTERNAL, "external"}};
    fixture_start();
    for (size_t i = 0; i < sizeof(clocks) / sizeof(clocks[0]); ++i) {
        status_fake_clock(CRUX_OK, clocks[i].value);
        status_fake_advance_ms(1000);
        expect_text_item(s_viewer, "system.clock", clocks[i].word);
    }
    static const struct {
        crux_watchdog_state_t state;
        const char *word;
    } states[] = {{CRUX_WATCHDOG_UNAVAILABLE, "unavailable"},
                  {CRUX_WATCHDOG_IDLE, "idle"},
                  {CRUX_WATCHDOG_ACTIVE, "active"},
                  {CRUX_WATCHDOG_FAULTED, "faulted"},
                  {CRUX_WATCHDOG_EXPIRED, "expired"}};
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        status_fake_system(CRUX_OK, CRUX_OK, states[i].state);
        status_fake_advance_ms(1000);
        expect_text_item(s_viewer, "system.watchdog_state", states[i].word);
    }
}

static void per_entry_failures(void)
{
    fixture_start();
    /* One failed source hides nothing else. */
    status_fake_uptime(CRUX_ERR_IO, 0);
    expect_failed_item(s_viewer, "system.uptime_ms", CRUX_ERR_IO);
    expect_text_item(s_viewer, "version.project", "pilot-7");
    status_fake_uptime(CRUX_OK, 2000);

    status_fake_clock(CRUX_ERR_NOT_READY, CRUX_MGMT_STATUS_CLOCK_UNSET);
    expect_failed_item(s_viewer, "system.clock", CRUX_ERR_NOT_READY);
    expect_text_item(s_viewer, "system.watchdog_state", "active");

    /* The System contract's call failed, or its per-service status did. */
    status_fake_clock(CRUX_OK, CRUX_MGMT_STATUS_CLOCK_RTC);
    status_fake_system(CRUX_ERR_IO, CRUX_OK, CRUX_WATCHDOG_ACTIVE);
    status_fake_advance_ms(1000);
    expect_failed_item(s_viewer, "system.watchdog_state", CRUX_ERR_IO);
    expect_text_item(s_viewer, "system.clock", "rtc");
    status_fake_system(CRUX_OK, CRUX_ERR_TIMEOUT, CRUX_WATCHDOG_EXPIRED);
    status_fake_advance_ms(1000);
    expect_failed_item(s_viewer, "system.watchdog_state", CRUX_ERR_TIMEOUT);
    status_fake_system(CRUX_OK, CRUX_ERR_NOT_SUPPORTED, CRUX_WATCHDOG_UNAVAILABLE);
    status_fake_advance_ms(1000);
    expect_failed_item(s_viewer, "system.watchdog_state", CRUX_ERR_NOT_SUPPORTED);

    /* Each result in the snapshot's word comes back as itself. */
    const crux_status_t all[] = {
        CRUX_ERR_INVALID_ARGUMENT, CRUX_ERR_OUT_OF_RANGE, CRUX_ERR_NOT_READY,
        CRUX_ERR_NO_DATA,          CRUX_ERR_IO,           CRUX_ERR_BUSY,
        CRUX_ERR_TIMEOUT,          CRUX_ERR_NO_RESOURCES, CRUX_ERR_NOT_SUPPORTED,
        CRUX_ERR_INVALID_STATE};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); ++i) {
        status_fake_system(all[i], CRUX_OK, CRUX_WATCHDOG_ACTIVE);
        status_fake_clock(all[i], CRUX_MGMT_STATUS_CLOCK_RTC);
        status_fake_advance_ms(1000);
        expect_failed_item(s_viewer, "system.watchdog_state", all[i]);
        expect_failed_item(s_viewer, "system.clock", all[i]);
    }
    /* A value that is no Base result never reaches a client. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    status_fake_system((crux_status_t)-99, CRUX_OK, CRUX_WATCHDOG_ACTIVE);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    status_fake_clock((crux_status_t)7, CRUX_MGMT_STATUS_CLOCK_RTC);
    status_fake_advance_ms(1000);
    expect_failed_item(s_viewer, "system.watchdog_state", CRUX_ERR_IO);
    expect_failed_item(s_viewer, "system.clock", CRUX_ERR_IO);

    /* The network's three items share a source. */
    crux_mgmt_status_net_t net = {0};
    status_fake_net(CRUX_ERR_NOT_READY, net);
    expect_failed_item(s_viewer, "net.link", CRUX_ERR_NOT_READY);
    expect_failed_item(s_viewer, "net.admin_up", CRUX_ERR_NOT_READY);
    expect_failed_item(s_viewer, "net.ipv4", CRUX_ERR_NOT_READY);

    /* The store and the events, each with every item it feeds. */
    crux_mgmt_status_credential_t credential = {"valid", "none", "host_fake", false, false, false};
    status_fake_credential(true, CRUX_ERR_NOT_READY, credential);
    expect_failed_item(s_viewer, "security.credential_state", CRUX_ERR_NOT_READY);
    expect_failed_item(s_viewer, "security.credential_basis", CRUX_ERR_NOT_READY);
    expect_failed_item(s_viewer, "security.credential_rollback_detected", CRUX_ERR_NOT_READY);
    crux_mgmt_status_event_counts_t counts = {0};
    status_fake_events(true, CRUX_ERR_IO, counts);
    expect_failed_item(s_viewer, "security.events_emitted", CRUX_ERR_IO);
    expect_failed_item(s_viewer, "security.events_log_errors", CRUX_ERR_IO);
}

/* A provider is code in the image, but an answer out of range does not reach a client. It also
 * must not spill into the other item's part of the published word. */
static void out_of_range_answers(void)
{
    fixture_start();
    const int values[] = {5, 7, 8, 99, 1000};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        status_fake_clock(CRUX_OK, CRUX_MGMT_STATUS_CLOCK_MANUAL);
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): refused by contract
        status_fake_system(CRUX_OK, CRUX_OK, (crux_watchdog_state_t)values[i]);
        status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS);
        expect_failed_item(s_viewer, "system.watchdog_state", CRUX_ERR_IO);
        expect_text_item(s_viewer, "system.clock", "manual");

        status_fake_system(CRUX_OK, CRUX_OK, CRUX_WATCHDOG_IDLE);
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): refused by contract
        status_fake_clock(CRUX_OK, (crux_mgmt_status_clock_t)values[i]);
        status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS);
        expect_failed_item(s_viewer, "system.clock", CRUX_ERR_IO);
        expect_text_item(s_viewer, "system.watchdog_state", "idle");
    }
}

static void network_partial_answers(void)
{
    fixture_start();
    /* Up, with a link state the driver cannot tell and no address. */
    crux_mgmt_status_net_t net = {.admin_up = true};
    status_fake_net(CRUX_OK, net);
    crux_mgmt_status_entry_t entry = entry_named(s_viewer, "net.admin_up");
    CHECK(entry.status == CRUX_OK && entry.value.data.boolean);
    expect_failed_item(s_viewer, "net.link", CRUX_ERR_NO_DATA);
    expect_failed_item(s_viewer, "net.ipv4", CRUX_ERR_NO_DATA);

    net = (crux_mgmt_status_net_t){false, true, false, true, {0, 0, 0, 0}};
    status_fake_net(CRUX_OK, net);
    entry = entry_named(s_viewer, "net.link");
    CHECK(entry.status == CRUX_OK && !entry.value.data.boolean);
    entry = entry_named(s_viewer, "net.admin_up");
    CHECK(entry.status == CRUX_OK && !entry.value.data.boolean);
    expect_text_item(s_viewer, "net.ipv4", "0.0.0.0");
    net.ipv4[0] = 255;
    net.ipv4[1] = 255;
    net.ipv4[2] = 255;
    net.ipv4[3] = 255;
    status_fake_net(CRUX_OK, net);
    expect_text_item(s_viewer, "net.ipv4", "255.255.255.255");
    net.ipv4[0] = 10;
    net.ipv4[1] = 0;
    net.ipv4[2] = 20;
    net.ipv4[3] = 100;
    status_fake_net(CRUX_OK, net);
    expect_text_item(s_viewer, "net.ipv4", "10.0.20.100");
}

static void credential_words_are_checked(void)
{
    fixture_start();
    /* A provider is code in the image, but a bad word still does not reach a client. */
    crux_mgmt_status_credential_t credential = {"va\nlid", "none", NULL, false, false, false};
    status_fake_credential(true, CRUX_OK, credential);
    expect_failed_item(s_viewer, "security.credential_state", CRUX_ERR_IO);
    expect_text_item(s_viewer, "security.credential_basis", "none");
    expect_failed_item(s_viewer, "security.credential_evidence", CRUX_ERR_IO);
}

static void reset_cause_as_captured(void)
{
    fixture_boot();
    crux_mgmt_status_config_t config = fixture_config();
    config.boot_reset_cause_status = CRUX_ERR_NOT_SUPPORTED;
    /* A value alongside a failed status is meaningless, and is not shown. */
    config.boot_reset_cause.causes = 0xFFFFFFFFu;
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    expect_failed_item(s_viewer, "reset.causes", CRUX_ERR_NOT_SUPPORTED);
    expect_failed_item(s_viewer, "reset.supported", CRUX_ERR_NOT_SUPPORTED);
}

static void reset_cause_unknown_status(void)
{
    fixture_boot();
    crux_mgmt_status_config_t config = fixture_config();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    config.boot_reset_cause_status = (crux_status_t)-99;
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    expect_failed_item(s_viewer, "reset.causes", CRUX_ERR_IO);
}

static void omits_what_is_not_linked(void)
{
    fixture_boot();
    crux_mgmt_status_credential_t credential = {"valid", "none", "host_fake", false, false, false};
    crux_mgmt_status_event_counts_t counts = {0};
    status_fake_credential(false, CRUX_OK, credential);
    status_fake_events(false, CRUX_OK, counts);
    crux_mgmt_status_config_t config = fixture_config();
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    expect_order(s_viewer, all_builtins, 10);
    CHECK(status_fake_credential_reads() == 0);
    CHECK(status_fake_events_reads() == 0);
}

static void omits_only_the_store(void)
{
    fixture_boot();
    crux_mgmt_status_credential_t credential = {"valid", "none", "host_fake", false, false, false};
    status_fake_credential(false, CRUX_OK, credential);
    crux_mgmt_status_config_t config = fixture_config();
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    CHECK(!listed(s_viewer, "security.credential_state"));
    CHECK(listed(s_viewer, "security.events_emitted"));
}

typedef struct {
    crux_status_t result;
    crux_mgmt_status_value_t value;
    unsigned calls;
} project_source_t;

static crux_status_t project_read(void *context, crux_mgmt_status_value_t *out)
{
    project_source_t *source = context;
    ++source->calls;
    /* The service zeroed this, so a source that sets nothing has no type. */
    CHECK(value_is_zero(out));
    if (source->result == CRUX_OK) {
        *out = source->value;
    }
    return source->result;
}

static project_source_t source_a, source_b, source_c;
static const crux_mgmt_status_item_t project_table[] = {
    {"app.first", "app.read", project_read, &source_a},
    {"system-not-reserved", "app.read", project_read, &source_b},
    {"app.viewer", CRUX_MGMT_STATUS_PERMISSION_READ, project_read, &source_c},
};

static void project_start(void)
{
    fixture_boot();
    crux_mgmt_status_config_t config = fixture_config();
    config.project_items = project_table;
    config.project_item_count = 3;
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
}

static void project_items_by_type(void)
{
    source_a.result = source_b.result = source_c.result = CRUX_OK;
    source_a.value =
        (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_BOOL, .data.boolean = true};
    source_b.value = (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_U32, .data.u32 = 42};
    source_c.value = (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_U64,
                                                .data.u64 = UINT64_C(18446744073709551615)};
    project_start();

    /* After every built-in, in table order, each by its own permission. */
    static const char *const app_only[] = {"app.first", "system-not-reserved"};
    expect_order(s_app, app_only, 2);
    crux_mgmt_status_entry_t entry = entry_named(s_app, "app.first");
    CHECK(entry.status == CRUX_OK && entry.value.type == CRUX_MGMT_STATUS_BOOL);
    CHECK(entry.value.data.boolean);
    entry = entry_named(s_app, "system-not-reserved");
    CHECK(entry.value.type == CRUX_MGMT_STATUS_U32 && entry.value.data.u32 == 42);
    entry = entry_named(s_viewer, "app.viewer");
    CHECK(entry.value.type == CRUX_MGMT_STATUS_U64);
    CHECK(entry.value.data.u64 == UINT64_C(18446744073709551615));

    /* A session with both sees both, and a hidden item's source does not run. */
    CHECK(listed(s_admin, "app.first") && listed(s_admin, "system.clock"));
    CHECK(!listed(s_viewer, "app.first"));
    unsigned before = source_a.calls;
    CHECK(!listed(s_viewer, "app.first"));
    CHECK(source_a.calls == before);

    /* Text: the longest that fits, and the empty string. */
    source_a.value = (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_TEXT};
    memset(source_a.value.data.text, 'x', 63);
    expect_text_item(s_app, "app.first", source_a.value.data.text);
    source_a.value.data.text[0] = '\0';
    expect_text_item(s_app, "app.first", "");

    CHECK(access_fake_event_count() == 0);
}

static void project_item_failures(void)
{
    project_start();
    source_a.result = CRUX_ERR_NOT_READY;
    expect_failed_item(s_app, "app.first", CRUX_ERR_NOT_READY);

    /* One failure hides nothing else. */
    source_b.result = CRUX_OK;
    source_b.value = (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_U32, .data.u32 = 1};
    CHECK(entry_named(s_app, "system-not-reserved").status == CRUX_OK);

    /* What a source returned that no client should see. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    source_a.result = (crux_status_t)1;
    expect_failed_item(s_app, "app.first", CRUX_ERR_IO);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    source_a.result = (crux_status_t)-99;
    expect_failed_item(s_app, "app.first", CRUX_ERR_IO);

    /* A wrong type, including none at all. */
    source_a.result = CRUX_OK;
    memset(&source_a.value, 0, sizeof(source_a.value));
    expect_failed_item(s_app, "app.first", CRUX_ERR_IO);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    source_a.value = (crux_mgmt_status_value_t){.type = (crux_mgmt_status_type_t)99};
    expect_failed_item(s_app, "app.first", CRUX_ERR_IO);

    /* Text under the character rule, whatever the bytes. */
    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_invalid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_invalid_vectors[i];
        /* A NUL ends the C string, so what is checked is the text before it. */
        if (memchr(vector->bytes, '\0', vector->length) != NULL) {
            continue;
        }
        source_a.value = (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_TEXT};
        memcpy(source_a.value.data.text, vector->bytes, vector->length);
        crux_mgmt_status_entry_t entry = entry_named(s_app, "app.first");
        if (entry.status != CRUX_ERR_IO) {
            printf("text %s\n", vector->what);
        }
        CHECK(entry.status == CRUX_ERR_IO);
    }
    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_valid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_valid_vectors[i];
        source_a.value = (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_TEXT};
        memcpy(source_a.value.data.text, vector->bytes, vector->length);
        expect_text_item(s_app, "app.first", vector->bytes);
    }
    /* No terminator anywhere in its 64 bytes. */
    memset(source_a.value.data.text, 'x', sizeof(source_a.value.data.text));
    expect_failed_item(s_app, "app.first", CRUX_ERR_IO);
}

static void filtering(void)
{
    source_a.result = source_b.result = source_c.result = CRUX_OK;
    source_a.value = source_b.value = source_c.value =
        (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_U32, .data.u32 = 1};
    project_start();

    /* Nothing visible: an immediate end, with no source read and no event. */
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    CHECK(crux_mgmt_status_next(s_none, &cursor, &entry) == CRUX_ERR_NO_DATA);
    CHECK(cursor == 0);
    CHECK(status_fake_system_reads() == 0);
    CHECK(status_fake_clock_reads() == 0);
    CHECK(status_fake_net_reads() == 0);
    CHECK(source_a.calls == 0);
    CHECK(access_fake_event_count() == 0);

    /* A polled page that may see nothing leaves the event ring empty, however long it polls. */
    for (unsigned i = 0; i < 100; ++i) {
        cursor = 0;
        CHECK(crux_mgmt_status_next(s_none, &cursor, &entry) == CRUX_ERR_NO_DATA);
    }
    CHECK(access_fake_event_count() == 0);
    /* A session seeing only built-ins, then only project items, leaves none either. */
    CHECK(!listed(s_viewer, "app.first"));
    CHECK(!listed(s_app, "system.clock"));
    CHECK(access_fake_event_count() == 0);
}

static void forced_change_lists_nothing(void)
{
    source_a.result = CRUX_OK;
    source_a.value = (crux_mgmt_status_value_t){.type = CRUX_MGMT_STATUS_U32, .data.u32 = 1};
    project_start();
    CHECK(crux_access_account_reset_password(s_admin, "viewer", ACCESS_TEST_PASSWORD,
                                             strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    crux_access_session_t forced =
        access_test_session("viewer", ACCESS_TEST_PASSWORD, &access_test_web);
    access_fake_events_clear();
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    CHECK(crux_mgmt_status_next(forced, &cursor, &entry) == CRUX_ERR_NO_DATA);
    CHECK(access_fake_event_count() == 0);
}

static void session_and_argument_refusals(void)
{
    fixture_boot();
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    CHECK(crux_mgmt_status_next(s_viewer, &cursor, &entry) == CRUX_ERR_NOT_READY);
    crux_mgmt_status_config_t config = fixture_config();
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);

    CHECK(crux_mgmt_status_next(s_never_valid, &cursor, &entry) == CRUX_ERR_INVALID_STATE);
    CHECK(cursor == 0);
    CHECK(crux_mgmt_status_next(s_never_valid, NULL, NULL) == CRUX_ERR_INVALID_STATE);
    CHECK(crux_mgmt_status_next(s_viewer, NULL, &entry) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_mgmt_status_next(s_viewer, &cursor, NULL) == CRUX_ERR_INVALID_ARGUMENT);

    /* A cursor beyond the list just ends. */
    cursor = 1000;
    CHECK(crux_mgmt_status_next(s_viewer, &cursor, &entry) == CRUX_ERR_NO_DATA);

    /* An ended session, part of the way through a list, keeps its place and reports its state. */
    cursor = 0;
    CHECK(crux_mgmt_status_next(s_viewer, &cursor, &entry) == CRUX_OK);
    size_t place = cursor;
    crux_access_logout(&s_viewer);
    access_fake_events_clear();
    CHECK(crux_mgmt_status_next(s_viewer, &cursor, &entry) == CRUX_ERR_INVALID_STATE);
    CHECK(cursor == place);

    /* An expired one too, and polling it does not end it: the next operation that may do so will.
     */
    access_fake_clock_advance(IDLE_LIMIT_PASSED_MS);
    cursor = 0;
    CHECK(crux_mgmt_status_next(s_app, &cursor, &entry) == CRUX_ERR_INVALID_STATE);
    CHECK(crux_mgmt_status_next(s_app, &cursor, &entry) == CRUX_ERR_INVALID_STATE);
    CHECK(access_fake_event_count() == 0);
    CHECK(crux_access_check(s_app, "app.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_SESSION_EXPIRED) == 1);
}

static void idle_timer_is_not_touched(void)
{
    fixture_start();
    /* Polling through most of the idle limit does not extend it. */
    for (unsigned minute = 0; minute < 14; ++minute) {
        access_fake_clock_advance(UINT64_C(60000));
        size_t cursor = 0;
        crux_mgmt_status_entry_t entry;
        CHECK(crux_mgmt_status_next(s_viewer, &cursor, &entry) == CRUX_OK);
    }
    access_fake_clock_advance(UINT64_C(120000));
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    CHECK(crux_mgmt_status_next(s_viewer, &cursor, &entry) == CRUX_ERR_INVALID_STATE);
}

/* Access control that cannot answer fails closed, and is not mistaken for an empty list. */
static void access_not_ready(void)
{
    fixture_start();
    crux_access_test_reset();
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    CHECK(crux_mgmt_status_next(s_viewer, &cursor, &entry) == CRUX_ERR_NOT_READY);
    CHECK(cursor == 0);
    char out[64];
    size_t length = 0;
    CHECK(crux_mgmt_status_report_json(s_viewer, out, sizeof(out), &length) == CRUX_ERR_NOT_READY);
}

static crux_status_t refused_start(crux_mgmt_status_config_t config)
{
    return crux_mgmt_status_start(&config);
}

static void start_refusals(void)
{
    fixture_boot();
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    CHECK(crux_mgmt_status_start(NULL) == CRUX_ERR_INVALID_ARGUMENT);

    crux_mgmt_status_config_t good = fixture_config();
    crux_mgmt_status_config_t config = good;

    /* Version strings: present, at most 63 bytes, and under the character rule. */
    config.platform_version = NULL;
    CHECK(refused_start(config) == CRUX_ERR_INVALID_ARGUMENT);
    config = good;
    config.project_version = NULL;
    CHECK(refused_start(config) == CRUX_ERR_INVALID_ARGUMENT);
    char too_long[65];
    memset(too_long, 'v', sizeof(too_long));
    too_long[64] = '\0';
    config = good;
    config.platform_version = too_long;
    CHECK(refused_start(config) == CRUX_ERR_INVALID_ARGUMENT);
    config = good;
    config.project_version = too_long;
    CHECK(refused_start(config) == CRUX_ERR_INVALID_ARGUMENT);
    config = good;
    config.project_version = "line\nbreak";
    CHECK(refused_start(config) == CRUX_ERR_INVALID_ARGUMENT);
    config = good;
    config.platform_version = "\xc2\x9b";
    CHECK(refused_start(config) == CRUX_ERR_INVALID_ARGUMENT);

    /* Tables. */
    config = good;
    config.project_item_count = 1;
    CHECK(refused_start(config) == CRUX_ERR_INVALID_ARGUMENT);

    static crux_mgmt_status_item_t items[2];
    const crux_mgmt_status_item_t valid = {"app.a", "app.read", project_read, &source_a};
    config = good;
    config.project_items = items;
    config.project_item_count = 1;
#define REFUSES_ITEM(name_value, permission_value, read_value)                                     \
    do {                                                                                           \
        items[0] = valid;                                                                          \
        items[0].name = (name_value);                                                              \
        items[0].permission = (permission_value);                                                  \
        items[0].read = (read_value);                                                              \
        CHECK(refused_start(config) == CRUX_ERR_INVALID_ARGUMENT);                                 \
        CHECK(crux_mgmt_status_next(s_viewer, &cursor, &entry) == CRUX_ERR_NOT_READY);             \
    } while (0)

    REFUSES_ITEM(NULL, "app.read", project_read);
    REFUSES_ITEM("", "app.read", project_read);
    REFUSES_ITEM("Upper", "app.read", project_read);
    REFUSES_ITEM("with space", "app.read", project_read);
    REFUSES_ITEM("slash/name", "app.read", project_read);
    REFUSES_ITEM("\xc3\xa9", "app.read", project_read);
    REFUSES_ITEM("12345678901234567890123456789012", "app.read", project_read);
    REFUSES_ITEM("system.x", "app.read", project_read);
    REFUSES_ITEM("reset.x", "app.read", project_read);
    REFUSES_ITEM("net.x", "app.read", project_read);
    REFUSES_ITEM("version.x", "app.read", project_read);
    REFUSES_ITEM("security.x", "app.read", project_read);
    REFUSES_ITEM("app.a", NULL, project_read);
    REFUSES_ITEM("app.a", "", project_read);
    char long_permission[CRUX_ACCESS_PERMISSION_MAX + 2];
    memset(long_permission, 'p', sizeof(long_permission));
    long_permission[CRUX_ACCESS_PERMISSION_MAX + 1] = '\0';
    REFUSES_ITEM("app.a", long_permission, project_read);
    REFUSES_ITEM("app.a", "app.read", NULL);

    /* A duplicate. */
    items[0] = valid;
    items[1] = valid;
    config.project_item_count = 2;
    CHECK(refused_start(config) == CRUX_ERR_INVALID_ARGUMENT);

    /* The longest name and a 63-byte version are fine. A corrected table can still start. */
    too_long[63] = '\0';
    items[0] = valid;
    items[0].name = "1234567890123456789012345678901";
    config.project_item_count = 1;
    config.platform_version = too_long;
    config.project_version = "";
    CHECK(refused_start(config) == CRUX_OK);
    expect_text_item(s_viewer, "version.platform", too_long);
    expect_text_item(s_viewer, "version.project", "");

    /* The version was copied, and a second start changes nothing. */
    too_long[0] = 'w';
    CHECK(strcmp(entry_named(s_viewer, "version.platform").value.data.text, too_long) != 0);
    config = good;
    CHECK(refused_start(config) == CRUX_ERR_INVALID_STATE);
    CHECK(strcmp(entry_named(s_viewer, "version.project").value.data.text, "") == 0);
#undef REFUSES_ITEM
}

static crux_status_t bound_read(void *context, crux_mgmt_status_value_t *out)
{
    (void)context;
    out->type = CRUX_MGMT_STATUS_U32;
    return CRUX_OK;
}

/* One over the bound with every name valid and different, so that the bound is what refuses it. */
static void project_table_bound(void)
{
    fixture_boot();
    static crux_mgmt_status_item_t items[CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX + 1u];
    static char names[CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX + 1u][8];
    for (size_t i = 0; i < CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX + 1u; ++i) {
        snprintf(names[i], sizeof(names[i]), "app.%02zu", i);
        items[i] = (crux_mgmt_status_item_t){names[i], "app.read", bound_read, NULL};
    }
    crux_mgmt_status_config_t config = fixture_config();
    config.project_items = items;
    config.project_item_count = CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX + 1u;
    CHECK(crux_mgmt_status_start(&config) == CRUX_ERR_INVALID_ARGUMENT);

    /* Exactly the bound starts, and each is listed. */
    config.project_item_count = CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX;
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    size_t cursor = 0;
    size_t count = 0;
    crux_mgmt_status_entry_t entry;
    while (crux_mgmt_status_next(s_app, &cursor, &entry) == CRUX_OK) {
        ++count;
    }
    CHECK(count == CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX);
}

int main(void)
{
    run_case("builtin values", builtin_values);
    run_case("clock and watchdog words", clock_and_watchdog_words);
    run_case("per entry failures", per_entry_failures);
    run_case("out of range answers", out_of_range_answers);
    run_case("network partial answers", network_partial_answers);
    run_case("credential words are checked", credential_words_are_checked);
    run_case("reset cause as captured", reset_cause_as_captured);
    run_case("reset cause unknown status", reset_cause_unknown_status);
    run_case("omits what is not linked", omits_what_is_not_linked);
    run_case("omits only the store", omits_only_the_store);
    run_case("project items by type", project_items_by_type);
    run_case("project item failures", project_item_failures);
    run_case("filtering", filtering);
    run_case("forced change lists nothing", forced_change_lists_nothing);
    run_case("session and argument refusals", session_and_argument_refusals);
    run_case("idle timer is not touched", idle_timer_is_not_touched);
    run_case("access not ready", access_not_ready);
    run_case("start refusals", start_refusals);
    run_case("project table bound", project_table_bound);
    return suite_finish("MgmtStatus.items");
}
