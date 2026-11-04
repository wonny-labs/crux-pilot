/* What the public contract promises, whichever implementation is linked. It runs against the core
 * and against the fake, so that a consumer's tests on the fake hold on the core. It includes only
 * the public header and the driver. */
#include "public_driver.h"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static const crux_access_session_t session = {1};

/* No call may write to its output unless it reports a record. */
static void fill_record(crux_security_event_record_t *record)
{
    memset(record, 0x5A, sizeof(*record));
}

static bool record_untouched(const crux_security_event_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    for (size_t index = 0; index < sizeof(*record); ++index) {
        if (bytes[index] != 0x5Au) {
            return false;
        }
    }
    return true;
}

static crux_security_event_stats_t stats(void)
{
    crux_security_event_stats_t out;
    memset(&out, 0xA5, sizeof(out));
    CHECK(crux_security_event_get_stats(&out) == CRUX_OK);
    return out;
}

static void boot(void)
{
    public_driver_reset();
    CHECK(crux_security_event_init(NULL, 0) == CRUX_OK);
}

static crux_security_event_record_t emit_and_read(const crux_security_event_t *event)
{
    boot();
    crux_security_event_emit(event);
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    memset(&record, 0xA5, sizeof(record));
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    return record;
}

static void before_init(void)
{
    public_driver_reset();
    crux_security_event_t event = {.id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGOUT};
    crux_security_event_emit(&event);
    crux_security_event_stats_t counted = stats();
    CHECK(counted.dropped == 1);
    CHECK(counted.emitted == 0);
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_ERR_NOT_READY);
    CHECK(crux_security_event_start() == CRUX_ERR_NOT_READY);
    CHECK(crux_security_event_get_stats(NULL) == CRUX_ERR_INVALID_ARGUMENT);
}

static void init_rules(void)
{
    public_driver_reset();
    static const crux_security_event_project_id_t low[] = {
        {0x7FFF, "app.low", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t bad_name[] = {
        {0x8000, "App Name", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t duplicate[] = {
        {0x8000, "app.one", CRUX_SECURITY_EVENT_ROUTINE},
        {0x8000, "app.two", CRUX_SECURITY_EVENT_CHANGE}};
    static const crux_security_event_project_id_t forged_lost[] = {
        {0x8000, "event.lost", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t forged_login[] = {
        {0x8000, "access.login_ok", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t good[] = {
        {0x8001, "app.good", CRUX_SECURITY_EVENT_CHANGE}};
    CHECK(crux_security_event_init(NULL, 1) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_init(low, 1) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_init(bad_name, 1) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_init(duplicate, 2) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_init(forged_lost, 1) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_init(forged_login, 1) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_init(good, 1) == CRUX_OK);
    CHECK(crux_security_event_init(NULL, 0) == CRUX_ERR_INVALID_STATE);
    CHECK(strcmp(crux_security_event_name(0x8001), "app.good") == 0);
    CHECK(strcmp(crux_security_event_name(0x8002), "event.unknown") == 0);
    CHECK(strcmp(crux_security_event_name(CRUX_SECURITY_EVENT_ID_CONFIG_SAVED), "config.saved") ==
          0);
    CHECK(strcmp(crux_security_event_name(CRUX_SECURITY_EVENT_ID_CLI_LINE_REJECTED),
                 "cli.line_rejected") == 0);

    crux_security_event_t event = {.id = 0x8001, .user = "alice"};
    crux_security_event_emit(&event);
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == 0x8001);
}

static void start_rules(void)
{
    boot();
    CHECK(crux_security_event_start() == CRUX_OK);
    CHECK(crux_security_event_start() == CRUX_ERR_INVALID_STATE);
}

static void field_rules(void)
{
    crux_security_event_t event = {
        .id = CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED,
        .result = CRUX_SECURITY_EVENT_DENIED,
        .channel = 2,
        .source_length = 4,
        .source = {192, 0, 2, 7, 9, 9, 9, 9},
        .user = "alice",
        .subject = "config.save",
    };
    crux_security_event_record_t record = emit_and_read(&event);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED);
    CHECK(record.result == CRUX_SECURITY_EVENT_DENIED);
    CHECK(record.channel == 2);
    CHECK(record.source_length == 4);
    CHECK(memcmp(record.source, "\xC0\x00\x02\x07\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0);
    CHECK(strcmp(record.user, "alice") == 0);
    CHECK(strcmp(record.subject, "config.save") == 0);
    CHECK(record.sequence == 1);
    CHECK(!record.subject_truncated);
    CHECK(stats().invalid_fields == 0);

    /* A name outside the rule is omitted and counted. */
    event.user = "known secret text";
    record = emit_and_read(&event);
    CHECK(record.user[0] == '\0');
    CHECK(stats().invalid_fields == 1);
    event.user = "alice";

    event.subject = "not allowed";
    record = emit_and_read(&event);
    CHECK(record.subject[0] == '\0');
    CHECK(stats().invalid_fields == 1);

    event.subject = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
    record = emit_and_read(&event);
    CHECK(strlen(record.subject) == 48);
    CHECK(record.subject[47] == '~');
    CHECK(record.subject_truncated);
    CHECK(stats().invalid_fields == 0);
    event.subject = NULL;

    event.source_length = 16;
    record = emit_and_read(&event);
    CHECK(record.source_length == 0);
    CHECK(stats().invalid_fields == 1);
    event.source_length = 4;

    event.channel = 9;
    record = emit_and_read(&event);
    CHECK(record.channel == 0);
    CHECK(stats().invalid_fields == 1);
    event.channel = 2;

    event.id = 0x8123;
    record = emit_and_read(&event);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN);
    CHECK(stats().invalid_fields == 1);

    event.id = CRUX_SECURITY_EVENT_ID_EVENT_LOST;
    record = emit_and_read(&event);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN);
    CHECK(stats().invalid_fields == 1);
}

static void read_rules(void)
{
    boot();
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    fill_record(&record);

    CHECK(crux_security_event_read(session, NULL, &record) ==
          CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_read(session, &cursor, NULL) ==
          CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_END);
    CHECK(cursor == 0);
    CHECK(record_untouched(&record));

    crux_security_event_t first = {.id = CRUX_SECURITY_EVENT_ID_FILE_CREATED};
    crux_security_event_t second = {.id = CRUX_SECURITY_EVENT_ID_FILE_REMOVED};
    crux_security_event_emit(&first);
    crux_security_event_emit(&second);
    CHECK(stats().emitted == 2);

    public_driver_access_answer(CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_ERR_DENIED);
    public_driver_access_answer(CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_security_event_read(session, &cursor, &record) ==
          CRUX_SECURITY_READ_ERR_SESSION_INVALID);
    public_driver_access_answer(CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_ERR_NOT_READY);
    CHECK(cursor == 0);
    CHECK(record_untouched(&record));
    public_driver_access_answer(CRUX_ACCESS_OK);

    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_FILE_CREATED);
    CHECK(record.sequence == 1);
    CHECK(cursor == 2);
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_FILE_REMOVED);
    CHECK(record.sequence == 2);
    CHECK(cursor == 3);
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_END);
    CHECK(cursor == 3);

    /* A cursor ahead of the numbering restarts at the oldest. */
    cursor = 1000;
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 1);
}

int main(void)
{
    before_init();
    init_rules();
    start_rules();
    field_rules();
    read_rules();
    public_driver_reset();
    printf("Security event public contract: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
