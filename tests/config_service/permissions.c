/* Every row of the permission table for listing, read, update and save, with success,
 * unauthenticated, unpermitted and invalid input on each operation, on the real Access core. */
#include "service_test.h"

/* Past the policy's idle limit of 15 minutes. */
#define IDLE_LIMIT_PASSED_MS UINT64_C(960000)

/* Walks a listing, and expects exactly these names in table order. */
static void expect_listing(crux_access_session_t session, const char *const *names, size_t count)
{
    size_t cursor = 0;
    for (size_t i = 0; i < count; ++i) {
        crux_config_service_item_t item;
        CHECK(crux_config_service_next(session, &cursor, &item) == CRUX_CONFIG_SERVICE_OK);
        CHECK(strcmp(item.name, names[i]) == 0);
    }
    crux_config_service_item_t item = {.name = "untouched"};
    /* The end says nothing about how many entries the table has beyond what was shown. */
    size_t before_end = cursor;
    CHECK(crux_config_service_next(session, &cursor, &item) == CRUX_CONFIG_SERVICE_END);
    CHECK(strcmp(item.name, "untouched") == 0);
    CHECK(cursor == before_end);
    /* The end stays the end. */
    CHECK(crux_config_service_next(session, &cursor, &item) == CRUX_CONFIG_SERVICE_END);
}

static crux_config_service_item_t item_of(crux_access_session_t session, const char *name)
{
    size_t cursor = 0;
    crux_config_service_item_t item;
    while (crux_config_service_next(session, &cursor, &item) == CRUX_CONFIG_SERVICE_OK) {
        if (strcmp(item.name, name) == 0) {
            return item;
        }
    }
    CHECK(!"the parameter is listed");
    memset(&item, 0, sizeof(item));
    return item;
}

static void listing_by_role(void)
{
    fixture_start();
    static const char *const everything[] = {NAME_MODE,  NAME_LIMIT,  NAME_WIDE, NAME_HOST,
                                             NAME_SMALL, NAME_SECRET, NAME_FIXED};
    static const char *const writable[] = {NAME_MODE, NAME_LIMIT, NAME_WIDE,
                                           NAME_HOST, NAME_SMALL, NAME_SECRET};
    expect_listing(s_stranger, NULL, 0);
    expect_listing(s_saver, NULL, 0);
    expect_listing(s_reader, everything, 7);
    expect_listing(s_writer, writable, 6);
    expect_listing(s_both, everything, 7);
    expect_listing(s_admin, everything, 7);
    /* Hiding left no event, however many listings ran. */
    CHECK(denials() == 0);
    CHECK(access_fake_event_count() == 0);
    CHECK(config_events_count() == 0);
}

static void listing_fields(void)
{
    fixture_start();
    crux_config_service_item_t item = item_of(s_reader, NAME_HOST);
    CHECK(item.type == CRUX_CONFIG_STRING);
    CHECK(item.activation == CRUX_CONFIG_ACTIVATION_APPLICATION);
    CHECK(!item.sensitive);
    CHECK(item.readable && !item.writable && !item.unsaved);

    item = item_of(s_writer, NAME_HOST);
    CHECK(!item.readable && item.writable && !item.unsaved);
    CHECK(update_text(s_writer, NAME_HOST, "other") == CRUX_CONFIG_SERVICE_OK);
    item = item_of(s_writer, NAME_HOST);
    CHECK(item.unsaved);
    /* A session that cannot write is not told. */
    item = item_of(s_reader, NAME_HOST);
    CHECK(!item.unsaved);

    item = item_of(s_both, NAME_SECRET);
    CHECK(item.sensitive && item.writable);
    CHECK(!item.readable);
    CHECK(item.activation == CRUX_CONFIG_ACTIVATION_RESTART);
    CHECK(item.type == CRUX_CONFIG_STRING);

    item = item_of(s_reader, NAME_FIXED);
    CHECK(item.type == CRUX_CONFIG_U32);
    CHECK(item.activation == CRUX_CONFIG_ACTIVATION_SAFE_STATE);
    CHECK(item.readable && !item.writable);

    item = item_of(s_reader, NAME_MODE);
    CHECK(item.type == CRUX_CONFIG_BOOL);
}

static void listing_refusals(void)
{
    fixture_boot();
    size_t cursor = 0;
    crux_config_service_item_t item;
    CHECK(crux_config_service_next(s_both, &cursor, &item) == CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    CHECK(crux_config_service_start(test_table, TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_next(s_never_valid, &cursor, &item) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(cursor == 0);
    CHECK(crux_config_service_next(s_both, NULL, &item) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_next(s_both, &cursor, NULL) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    /* The session comes first, as on every operation. */
    CHECK(crux_config_service_next(s_never_valid, NULL, NULL) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);

    /* A cursor beyond the table just ends. */
    cursor = 1000;
    CHECK(crux_config_service_next(s_both, &cursor, &item) == CRUX_CONFIG_SERVICE_END);

    /* An ended session, and an expired one, which the listing that finds it reports once. */
    crux_access_logout(&s_reader);
    cursor = 0;
    CHECK(crux_config_service_next(s_reader, &cursor, &item) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    access_fake_events_clear();
    access_fake_clock_advance(IDLE_LIMIT_PASSED_MS);
    CHECK(crux_config_service_next(s_both, &cursor, &item) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_SESSION_EXPIRED) == 1);
}

static void forced_change_sees_nothing(void)
{
    fixture_start();
    CHECK(crux_access_account_reset_password(s_admin, "both", ACCESS_TEST_PASSWORD,
                                             strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    crux_access_session_t forced = fixture_login("both");
    access_fake_events_clear();
    expect_listing(forced, NULL, 0);
    CHECK(access_fake_event_count() == 0);
    char text[8];
    size_t length;
    crux_config_type_t type;
    CHECK(crux_config_service_read(forced, NAME_MODE, text, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(update_text(forced, NAME_MODE, "true") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save(forced, &report) == CRUX_CONFIG_SERVICE_ERR_DENIED);
    expect_u32(NAME_LIMIT, 5);
}

static void read_rows(void)
{
    fixture_start();
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    size_t length = 0;
    crux_config_type_t type = CRUX_CONFIG_BOOL;
#define READ(session, name)                                                                        \
    crux_config_service_read(session, name, text, sizeof(text), &length, &type)

    /* Neither: looks missing, and a direct attempt leaves a denial naming the permission. */
    CHECK(READ(s_stranger, NAME_LIMIT) == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(denials() == 1);
    const crux_access_event_t *event = access_fake_event(0);
    CHECK(event->type == CRUX_ACCESS_EVENT_PERMISSION_DENIED);
    CHECK(strcmp(event->permission, "cfg.read") == 0);
    /* Truly missing looks the same, and leaves nothing to report. */
    access_fake_events_clear();
    CHECK(READ(s_stranger, "no_such_name") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(READ(s_both, "no_such_name") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    /* A name matches whole: not a prefix of a key, or a key with more after it. */
    CHECK(READ(s_both, "host") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(READ(s_both, "host_name_more") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(READ(s_both, "") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(update_text(s_both, "host", "x") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    crux_config_value_t unread = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_service_try_read("host", &unread, text, sizeof(text)) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(access_fake_event_count() == 0);

    /* Read only: the value. */
    CHECK(READ(s_reader, NAME_LIMIT) == CRUX_CONFIG_SERVICE_OK);
    CHECK(strcmp(text, "5") == 0 && length == 1 && type == CRUX_CONFIG_U32);
    CHECK(access_fake_event_count() == 0);

    /* Write only: denied, with a denial. */
    CHECK(READ(s_writer, NAME_LIMIT) == CRUX_CONFIG_SERVICE_ERR_DENIED);
    CHECK(denials() == 1);
    /* Both: the value. */
    access_fake_events_clear();
    CHECK(READ(s_both, NAME_LIMIT) == CRUX_CONFIG_SERVICE_OK);
    CHECK(strcmp(text, "5") == 0);
    CHECK(access_fake_event_count() == 0);

    /* A read-only parameter, and one no role holds. */
    CHECK(READ(s_reader, NAME_FIXED) == CRUX_CONFIG_SERVICE_OK);
    CHECK(READ(s_writer, NAME_FIXED) == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(READ(s_admin, NAME_PRIVATE) == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);

    /* An unauthenticated session gets that answer, whatever it names. */
    CHECK(READ(s_never_valid, NAME_LIMIT) == CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(READ(s_never_valid, "no_such_name") == CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(crux_config_service_read(s_never_valid, NULL, text, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(crux_config_service_read(s_never_valid, NAME_LIMIT, NULL, 0, NULL, NULL) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);

    /* An expired session is ended and reported by the operation that finds it. */
    access_fake_clock_advance(IDLE_LIMIT_PASSED_MS);
    access_fake_events_clear();
    CHECK(READ(s_reader, NAME_LIMIT) == CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_SESSION_EXPIRED) == 1);
#undef READ
}

static void read_invalid_input(void)
{
    fixture_boot();
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    size_t length = 99;
    crux_config_type_t type = CRUX_CONFIG_BOOL;
    CHECK(crux_config_service_read(s_both, NAME_LIMIT, text, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    CHECK(crux_config_service_start(test_table, TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_read(s_both, NULL, text, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_read(s_both, NAME_LIMIT, NULL, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_read(s_both, NAME_LIMIT, text, sizeof(text), NULL, &type) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_read(s_both, NAME_LIMIT, text, sizeof(text), &length, NULL) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);

    /* Exactly what fits, then one short, which leaves the outputs alone. */
    CHECK(update_text(s_both, NAME_LIMIT, "42") == CRUX_CONFIG_SERVICE_OK);
    memset(text, 'z', sizeof(text));
    CHECK(crux_config_service_read(s_both, NAME_LIMIT, text, 3, &length, &type) ==
          CRUX_CONFIG_SERVICE_OK);
    CHECK(strcmp(text, "42") == 0 && length == 2);
    memset(text, 'z', sizeof(text));
    length = 99;
    CHECK(crux_config_service_read(s_both, NAME_LIMIT, text, 2, &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_TOO_SMALL);
    CHECK(text[0] == 'z' && length == 99);
    CHECK(crux_config_service_read(s_both, NAME_LIMIT, text, 0, &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_TOO_SMALL);
    CHECK(update_text(s_both, NAME_HOST, "abc") == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_read(s_both, NAME_HOST, text, 3, &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_TOO_SMALL);
    CHECK(text[0] == 'z' && length == 99);
}

static void update_rows(void)
{
    fixture_start();
    crux_config_service_item_t applied = {.name = "untouched"};

    /* Neither: looks missing, with a denial, and nothing changes. */
    CHECK(crux_config_service_update(s_stranger, NAME_LIMIT, "6", 1, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(denials() == 1);
    CHECK(strcmp(access_fake_event(0)->permission, "cfg.write") == 0);
    access_fake_events_clear();
    CHECK(update_text(s_stranger, "no_such_name", "6") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(access_fake_event_count() == 0);

    /* Read only: denied, with a denial. */
    CHECK(crux_config_service_update(s_reader, NAME_LIMIT, "6", 1, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_DENIED);
    CHECK(denials() == 1);
    CHECK(strcmp(applied.name, "untouched") == 0);
    expect_u32(NAME_LIMIT, 5);
    CHECK(config_events_count() == 0);

    /* Write only and both are allowed, and each reports what it is told about the parameter. */
    CHECK(crux_config_service_update(s_writer, NAME_LIMIT, "6", 1, &applied) ==
          CRUX_CONFIG_SERVICE_OK);
    expect_u32(NAME_LIMIT, 6);
    CHECK(strcmp(applied.name, NAME_LIMIT) == 0);
    CHECK(applied.type == CRUX_CONFIG_U32);
    CHECK(applied.activation == CRUX_CONFIG_ACTIVATION_APPLICATION);
    CHECK(!applied.sensitive && !applied.readable && applied.writable && applied.unsaved);
    CHECK(crux_config_service_update(s_both, NAME_LIMIT, "7", 1, &applied) ==
          CRUX_CONFIG_SERVICE_OK);
    expect_u32(NAME_LIMIT, 7);
    CHECK(applied.readable && applied.writable && applied.unsaved);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_UPDATED) == 2);

    /* Each write names the session, and only the parameter's name. */
    const config_event_record_t *record = config_events_get(1);
    CHECK(record->session.value == s_both.value);
    CHECK(strcmp(record->name, NAME_LIMIT) == 0);

    /* A read-only parameter: a session that sees it is denied with no denial, because it holds what
     * was checked, and a session that does not looks at nothing. */
    access_fake_events_clear();
    CHECK(update_text(s_reader, NAME_FIXED, "8") == CRUX_CONFIG_SERVICE_ERR_DENIED);
    CHECK(denials() == 0);
    CHECK(update_text(s_writer, NAME_FIXED, "8") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(denials() == 1);
    expect_u32(NAME_FIXED, 7);

    /* A parameter under a permission no role holds is out of reach, and looks missing. */
    CHECK(update_text(s_admin, NAME_PRIVATE, "8") == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    expect_u32(NAME_PRIVATE, 9);

    /* The activation carries through: applied never says a change took effect. */
    CHECK(crux_config_service_update(s_writer, NAME_SECRET, "x", 1, &applied) ==
          CRUX_CONFIG_SERVICE_OK);
    CHECK(applied.activation == CRUX_CONFIG_ACTIVATION_RESTART);
}

static void update_invalid_input(void)
{
    fixture_boot();
    crux_config_service_item_t applied;
    CHECK(crux_config_service_update(s_both, NAME_LIMIT, "6", 1, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    CHECK(crux_config_service_start(test_table, TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_update(s_never_valid, NAME_LIMIT, "6", 1, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(crux_config_service_update(s_never_valid, "no_such_name", "6", 1, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(crux_config_service_update(s_never_valid, NULL, NULL, 0, NULL) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(crux_config_service_update(s_both, NULL, "6", 1, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_update(s_both, NAME_LIMIT, NULL, 0, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_update(s_both, NAME_LIMIT, "6", 1, NULL) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    expect_u32(NAME_LIMIT, 5);
    /* A bad session is reported and ended by the operation that finds it. */
    crux_access_logout(&s_writer);
    access_fake_events_clear();
    CHECK(crux_config_service_update(s_writer, NAME_LIMIT, "6", 1, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_SESSION_INVALID) == 1);
    /* A missing pointer is the caller's error, and no client's refused value. */
    CHECK(config_events_count() == 0);
}

static void save_permission(void)
{
    fixture_boot();
    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save(s_saver, &report) == CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    CHECK(crux_config_service_start(test_table, TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_writer, NAME_LIMIT, "6") == CRUX_CONFIG_SERVICE_OK);

    size_t writes = config_fault_writes();
    CHECK(crux_config_service_save(s_never_valid, &report) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    CHECK(crux_config_service_save(s_never_valid, NULL) == CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    access_fake_events_clear();
    /* Holding cfg.write does not grant a save: it is the fixed permission only. */
    CHECK(crux_config_service_save(s_writer, &report) == CRUX_CONFIG_SERVICE_ERR_DENIED);
    CHECK(denials() == 1);
    CHECK(strcmp(access_fake_event(0)->permission, CRUX_CONFIG_SERVICE_PERMISSION_SAVE) == 0);
    CHECK(crux_config_service_save(s_stranger, &report) == CRUX_CONFIG_SERVICE_ERR_DENIED);
    CHECK(crux_config_service_save(s_saver, NULL) == CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(config_fault_writes() == writes);

    /* It saved a parameter this session cannot see, so its report counts none. */
    CHECK(crux_config_service_save(s_saver, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 0 && report.failed_name == NULL);
    CHECK(config_fault_writes() == writes + 1);
}

static void start_refusals(void)
{
    fixture_boot();
    /* Until a table starts, everything is not ready. */
    crux_config_value_t unread = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_service_try_read(NAME_LIMIT, &unread, NULL, 0) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    crux_config_value_t candidate = {.type = CRUX_CONFIG_U32, .data.u32 = 6};
    CHECK(crux_config_service_try_update(NAME_LIMIT, &candidate) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save_local(&report) == CRUX_CONFIG_SERVICE_ERR_NOT_READY);

    crux_config_service_entry_t table[2];
#define RESET_TABLE()                                                                              \
    do {                                                                                           \
        table[0] = test_table[0];                                                                  \
        table[1] = test_table[1];                                                                  \
    } while (0)
#define REFUSED()                                                                                  \
    do {                                                                                           \
        CHECK(crux_config_service_start(table, 2) == CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);    \
        size_t cursor = 0;                                                                         \
        crux_config_service_item_t item;                                                           \
        CHECK(crux_config_service_next(s_both, &cursor, &item) ==                                  \
              CRUX_CONFIG_SERVICE_ERR_NOT_READY);                                                  \
    } while (0)

    CHECK(crux_config_service_start(NULL, 1) == CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_start(test_table, CRUX_CONFIG_SERVICE_MAX + 1u) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);

    RESET_TABLE();
    table[1].parameter = NULL;
    REFUSED();

    /* A parameter that is not initialised. */
    static crux_config_param_t not_initialised;
    RESET_TABLE();
    table[1].parameter = &not_initialised;
    REFUSED();

    /* A duplicate, by object or by key. */
    RESET_TABLE();
    table[1].parameter = table[0].parameter;
    REFUSED();
    static const crux_config_spec_t same_name = {
        NAME_MODE, 1, {.type = CRUX_CONFIG_U32, .data.u32 = 0}, NULL, NULL};
    static crux_config_param_t same_name_param;
    CHECK(crux_config_init(&same_name_param, &same_name) == CRUX_CONFIG_OK);
    RESET_TABLE();
    table[1].parameter = &same_name_param;
    REFUSED();

    /* Permissions: required, not empty and not over-long, with no default. */
    RESET_TABLE();
    table[1].read_permission = NULL;
    REFUSED();
    RESET_TABLE();
    table[1].read_permission = "";
    REFUSED();
    RESET_TABLE();
    table[1].write_permission = "";
    REFUSED();
    char too_long[CRUX_ACCESS_PERMISSION_MAX + 2];
    memset(too_long, 'p', sizeof(too_long));
    too_long[CRUX_ACCESS_PERMISSION_MAX + 1] = '\0';
    RESET_TABLE();
    table[1].read_permission = too_long;
    REFUSED();
    RESET_TABLE();
    table[1].write_permission = too_long;
    REFUSED();

    /* Sensitive needs a write permission, or it could never be set. */
    RESET_TABLE();
    table[1].sensitive = true;
    table[1].write_permission = NULL;
    REFUSED();

    RESET_TABLE();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): a value the contract refuses
    table[1].activation = (crux_config_activation_t)3;
    REFUSED();

    /* A corrected table can still start, and a second start changes nothing. */
    RESET_TABLE();
    CHECK(crux_config_service_start(table, 2) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_start(test_table, TABLE_ENTRIES) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    size_t cursor = 0;
    crux_config_service_item_t item;
    CHECK(crux_config_service_next(s_both, &cursor, &item) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_next(s_both, &cursor, &item) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_next(s_both, &cursor, &item) == CRUX_CONFIG_SERVICE_END);
#undef RESET_TABLE
#undef REFUSED
}

static void table_bound(void)
{
    fixture_boot();
    static crux_config_param_t parameters[CRUX_CONFIG_SERVICE_MAX];
    static crux_config_spec_t specs[CRUX_CONFIG_SERVICE_MAX];
    static crux_config_service_entry_t table[CRUX_CONFIG_SERVICE_MAX];
    static char names[CRUX_CONFIG_SERVICE_MAX][8];
    for (size_t i = 0; i < CRUX_CONFIG_SERVICE_MAX; ++i) {
        snprintf(names[i], sizeof(names[i]), "p%02zu", i);
        crux_config_spec_t spec = {
            names[i], 1, {.type = CRUX_CONFIG_U32, .data.u32 = (uint32_t)i}, NULL, NULL};
        specs[i] = spec;
        CHECK(crux_config_init(&parameters[i], &specs[i]) == CRUX_CONFIG_OK);
        table[i] = (crux_config_service_entry_t){&parameters[i], "cfg.read", "cfg.write", false,
                                                 CRUX_CONFIG_ACTIVATION_APPLICATION};
    }
    CHECK(crux_config_service_start(table, CRUX_CONFIG_SERVICE_MAX) == CRUX_CONFIG_SERVICE_OK);

    /* The last entry's unsaved mark is the top bit of the record, and has to survive. */
    char name[8];
    snprintf(name, sizeof(name), "p%02d", CRUX_CONFIG_SERVICE_MAX - 1);
    CHECK(update_text(s_both, name, "1") == CRUX_CONFIG_SERVICE_OK);
    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 1);
    CHECK(crux_config_service_save(s_admin, &report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 0);
}

/* Access control that cannot answer fails closed, for every operation that needs it. */
static void access_not_ready(void)
{
    fixture_boot();
    CHECK(crux_config_service_start(test_table, TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    crux_access_test_reset();
    size_t cursor = 0;
    crux_config_service_item_t item;
    CHECK(crux_config_service_next(s_both, &cursor, &item) == CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    char text[8];
    size_t length;
    crux_config_type_t type;
    CHECK(crux_config_service_read(s_both, NAME_MODE, text, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    CHECK(crux_config_service_read(s_both, "no_such_name", text, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    CHECK(update_text(s_both, NAME_MODE, "true") == CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save(s_both, &report) == CRUX_CONFIG_SERVICE_ERR_NOT_READY);
    /* The application's own calls need no session, so they still answer. */
    expect_u32(NAME_LIMIT, 5);
}

static void empty_table(void)
{
    fixture_boot();
    CHECK(crux_config_service_start(NULL, 0) == CRUX_CONFIG_SERVICE_OK);
    size_t cursor = 0;
    crux_config_service_item_t item;
    CHECK(crux_config_service_next(s_both, &cursor, &item) == CRUX_CONFIG_SERVICE_END);
    CHECK(crux_config_service_next(s_never_valid, &cursor, &item) ==
          CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
}

int main(void)
{
    run_case("listing by role", listing_by_role);
    run_case("listing fields", listing_fields);
    run_case("listing refusals", listing_refusals);
    run_case("forced change", forced_change_sees_nothing);
    run_case("read rows", read_rows);
    run_case("read invalid input", read_invalid_input);
    run_case("update rows", update_rows);
    run_case("update invalid input", update_invalid_input);
    run_case("save permission", save_permission);
    run_case("start refusals", start_refusals);
    run_case("table bound", table_bound);
    run_case("access not ready", access_not_ready);
    run_case("empty table", empty_table);
    return suite_finish("ConfigService.permissions");
}
