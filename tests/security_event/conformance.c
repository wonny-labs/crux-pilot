/* Every identifier, the field rules and the log line, on the core with a controlled clock. The
 * table here is written out on purpose: a change to a value, name or class must change a test. */
#include "access_check_stub.h"
#include "event_test.h"

// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding): reads as the contract's table does.
typedef struct {
    crux_security_event_id_t id;
    const char *name;
    crux_security_event_class_t event_class;
} expected_id_t;

#define R CRUX_SECURITY_EVENT_ROUTINE
#define C CRUX_SECURITY_EVENT_CHANGE

static const expected_id_t expected_ids[] = {
    {0x0100, "access.login_ok", C},
    {0x0101, "access.login_failed", R},
    {0x0102, "access.key_failed", R},
    {0x0103, "access.delayed", R},
    {0x0104, "access.login_busy", R},
    {0x0105, "access.login_no_session", R},
    {0x0106, "access.login_refused", R},
    {0x0107, "access.not_ready", R},
    {0x0108, "access.permission_denied", R},
    {0x0109, "access.session_invalid", R},
    {0x010A, "access.logout", R},
    {0x010B, "access.session_expired", R},
    {0x010C, "access.provisioned", C},
    {0x010D, "access.provision_refused", R},
    {0x0110, "access.account_created", C},
    {0x0111, "access.account_removed", C},
    {0x0112, "access.account_disabled", C},
    {0x0113, "access.account_enabled", C},
    {0x0114, "access.role_changed", C},
    {0x0115, "access.password_reset", C},
    {0x0116, "access.password_changed", C},
    {0x0117, "access.key_added", C},
    {0x0118, "access.key_removed", C},
    {0x0119, "access.change_failed", C},
    {0x011A, "access.change_rejected", R},
    {0x0200, "credential.state", C},
    {0x0201, "credential.slot_invalid", C},
    {0x0202, "credential.save_failed", C},
    {0x0203, "credential.key_generated", C},
    {0x0204, "credential.key_replaced", C},
    {0x0205, "credential.erased", C},
    {0x0300, "file.created", C},
    {0x0301, "file.removed", C},
    {0x0302, "file.renamed", C},
    {0x0303, "file.partial_removed", C},
    {0x0304, "file.recovery_needed", C},
    {0x0305, "file.name_rejected", R},
    {0x0306, "file.commit_failed", C},
    {0x0307, "file.storage_failed", R},
    {0x0400, "config.updated", C},
    {0x0401, "config.rejected", R},
    {0x0402, "config.saved", C},
    {0x0403, "config.save_failed", C},
    {0x0500, "cli.line_rejected", R},
    {0x0501, "cli.console_error", R},
    {0x0600, "web.tls_failed", R},
    {0x0601, "web.csrf_rejected", R},
    {0x0602, "web.certificate_replaced", C},
    {0x0700, "ssh.auth_limit", R},
    {0x0701, "ssh.host_key_missing", C},
    {0x0F00, "event.unknown", R},
    {0x0F01, "event.lost", R},
};

/* The enumerators name the same values. */
_Static_assert(CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_OK == 0x0100, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_ACCESS_PROVISION_REFUSED == 0x010D, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_CREATED == 0x0110, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_ACCESS_CHANGE_REJECTED == 0x011A, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_CREDENTIAL_STATE == 0x0200, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_CREDENTIAL_ERASED == 0x0205, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_FILE_CREATED == 0x0300, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_FILE_STORAGE_FAILED == 0x0307, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED == 0x0400, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_CONFIG_SAVE_FAILED == 0x0403, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_CLI_LINE_REJECTED == 0x0500, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_CLI_CONSOLE_ERROR == 0x0501, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_WEB_TLS_FAILED == 0x0600, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REPLACED == 0x0602, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_SSH_AUTH_LIMIT == 0x0700, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_SSH_HOST_KEY_MISSING == 0x0701, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN == 0x0F00, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_EVENT_LOST == 0x0F01, "stable value");
_Static_assert(CRUX_SECURITY_EVENT_ID_PROJECT_FIRST == 0x8000, "stable value");

static bool name_characters_valid(const char *name)
{
    for (const char *c = name; *c != '\0'; ++c) {
        bool allowed = (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' ||
                       *c == '.' || *c == '-';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

static void identifiers(void)
{
    size_t rows = sizeof(expected_ids) / sizeof(expected_ids[0]);
    for (size_t index = 0; index < rows; ++index) {
        const expected_id_t *row = &expected_ids[index];
        bool known = false;
        const char *name = crux_security_event_id_name(row->id, &known);
        CHECK(known);
        CHECK(strcmp(name, row->name) == 0);
        CHECK(strcmp(crux_security_event_name(row->id), row->name) == 0);
        CHECK(crux_security_event_id_class(row->id) == row->event_class);
        CHECK(strlen(row->name) <= CRUX_SECURITY_EVENT_NAME_MAX);
        CHECK(name_characters_valid(row->name));
    }
    /* No identifier exists beyond the table above. */
    size_t known_count = 0;
    for (uint32_t id = 0; id <= UINT16_MAX; ++id) {
        bool known = false;
        const char *name = crux_security_event_id_name((crux_security_event_id_t)id, &known);
        known_count += known ? 1u : 0u;
        if (!known) {
            CHECK(strcmp(name, "event.unknown") == 0);
        }
    }
    CHECK(known_count == rows);
}

static crux_security_event_record_t read_only_record(void)
{
    crux_access_session_t session = {1};
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    memset(&record, 0xA5, sizeof(record));
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    return record;
}

/* One record with these fields, read back on a fresh ring. */
static crux_security_event_record_t emit_and_read(const crux_security_event_t *event)
{
    event_test_boot();
    crux_security_event_emit(event);
    return read_only_record();
}

static uint32_t invalid_fields(void)
{
    return event_test_stats().invalid_fields;
}

static void user_rule(void)
{
    crux_security_event_t event = {.id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGOUT};
    const char *const accepted[] = {"a", "alice", "user_1.name-2",
                                    "abcdefghijklmnopqrstuvwxyz01234"}; /* 31 characters */
    const char *const refused[] = {
        "",
        "Alice",
        "has space",
        "semi;colon",
        "new\nline",
        "tab\t",
        "per%cent",
        "abcdefghijklmnopqrstuvwxyz012345", /* 32 characters */
        "known-secret-typed-as-a-long-name-field",
    };
    for (size_t index = 0; index < sizeof(accepted) / sizeof(accepted[0]); ++index) {
        event.user = accepted[index];
        crux_security_event_record_t record = emit_and_read(&event);
        CHECK(strcmp(record.user, accepted[index]) == 0);
        CHECK(invalid_fields() == 0);
    }
    for (size_t index = 0; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        event.user = refused[index];
        crux_security_event_record_t record = emit_and_read(&event);
        CHECK(record.user[0] == '\0');
        CHECK(invalid_fields() == 1);
        CHECK(record.id == CRUX_SECURITY_EVENT_ID_ACCESS_LOGOUT);
    }
    event.user = NULL;
    crux_security_event_record_t record = emit_and_read(&event);
    CHECK(record.user[0] == '\0');
    CHECK(invalid_fields() == 0);
}

static void subject_rule(void)
{
    crux_security_event_t event = {.id = CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED};
    /* 48 bytes, the longest that stays whole. */
    const char *const whole = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN0123/:_-";
    CHECK(strlen(whole) == 48);
    event.subject = whole;
    crux_security_event_record_t record = emit_and_read(&event);
    CHECK(strcmp(record.subject, whole) == 0);
    CHECK(!record.subject_truncated);
    CHECK(invalid_fields() == 0);

    /* 49 bytes keeps 47 and marks it. */
    const char *const long_subject = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN0123/:_-Z";
    event.subject = long_subject;
    record = emit_and_read(&event);
    CHECK(strlen(record.subject) == 48);
    CHECK(strncmp(record.subject, long_subject, 47) == 0);
    CHECK(record.subject[47] == '~');
    CHECK(record.subject_truncated);
    CHECK(invalid_fields() == 0);

    /* A file's area/name can reach 79 bytes. */
    const char *const file_name = "logs/"
                                  "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl"
                                  "mnopqrstuv.txt";
    event.subject = file_name;
    record = emit_and_read(&event);
    CHECK(strlen(record.subject) == 48);
    CHECK(record.subject[47] == '~');

    /* Characters outside the rule omit the field, wherever they are in the text. */
    const char *const refused[] = {
        "has space",
        "quote\"",
        "new\nline",
        "carriage\rreturn",
        "tilde~",
        "per%cent",
        "login admin hunter2",
        "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz bad",
        "\xC3\xA9t\xC3\xA9",
    };
    for (size_t index = 0; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        event.subject = refused[index];
        record = emit_and_read(&event);
        CHECK(record.subject[0] == '\0');
        CHECK(!record.subject_truncated);
        CHECK(invalid_fields() == 1);
        CHECK(record.id == CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED);
    }

    /* Empty means no subject, and NULL too. */
    event.subject = "";
    record = emit_and_read(&event);
    CHECK(record.subject[0] == '\0');
    CHECK(invalid_fields() == 0);
    event.subject = NULL;
    record = emit_and_read(&event);
    CHECK(record.subject[0] == '\0');
    CHECK(invalid_fields() == 0);

    /* A subject has more characters than a user name, and a user keeps its own rule. */
    event.subject = "Config.Name/With:Upper-Case_1";
    record = emit_and_read(&event);
    CHECK(strcmp(record.subject, "Config.Name/With:Upper-Case_1") == 0);

    /* Each failed field counts, and the other fields stay. */
    event.subject = "bad subject";
    event.user = "Bad User";
    event.channel = 2;
    record = emit_and_read(&event);
    CHECK(record.user[0] == '\0' && record.subject[0] == '\0');
    CHECK(record.channel == 2);
    CHECK(invalid_fields() == 2);
}

static void source_and_channel_rule(void)
{
    crux_security_event_t event = {.id = CRUX_SECURITY_EVENT_ID_ACCESS_DELAYED,
                                   .channel = 2,
                                   .source = {192, 0, 2, 7, 1, 1, 1, 1, 9, 9, 9, 9, 9, 9, 9, 9}};
    event.source_length = 4;
    crux_security_event_record_t record = emit_and_read(&event);
    CHECK(record.source_length == 4);
    CHECK(memcmp(record.source, "\xC0\x00\x02\x07\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0);
    CHECK(invalid_fields() == 0);

    event.source_length = 8;
    record = emit_and_read(&event);
    CHECK(record.source_length == 8);
    CHECK(memcmp(record.source, "\xC0\x00\x02\x07\x01\x01\x01\x01\0\0\0\0\0\0\0\0", 16) == 0);
    CHECK(invalid_fields() == 0);

    event.source_length = 0;
    record = emit_and_read(&event);
    CHECK(record.source_length == 0);
    CHECK(memcmp(record.source, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0);
    CHECK(invalid_fields() == 0);

    /* A full address is not what the contract accepts, nor is any other length. */
    const uint8_t refused[] = {1, 3, 5, 16, 17, 255};
    for (size_t index = 0; index < sizeof(refused); ++index) {
        event.source_length = refused[index];
        record = emit_and_read(&event);
        CHECK(record.source_length == 0);
        CHECK(memcmp(record.source, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0);
        CHECK(invalid_fields() == 1);
    }

    event.source_length = 4;
    const uint8_t channels[] = {0, 1, 2, 3};
    for (size_t index = 0; index < sizeof(channels); ++index) {
        event.channel = channels[index];
        record = emit_and_read(&event);
        CHECK(record.channel == channels[index]);
        CHECK(invalid_fields() == 0);
    }
    event.channel = 4;
    record = emit_and_read(&event);
    CHECK(record.channel == 0);
    CHECK(invalid_fields() == 1);
}

static void identifier_and_result_rule(void)
{
    crux_security_event_t event = {.id = 0x0123};
    crux_security_event_record_t record = emit_and_read(&event);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN);
    CHECK(invalid_fields() == 1);

    /* An undeclared project identifier is unknown too. */
    event.id = 0x8000;
    record = emit_and_read(&event);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN);
    CHECK(invalid_fields() == 1);

    /* The lost line belongs to the drain, so no caller can forge one. */
    event.id = CRUX_SECURITY_EVENT_ID_EVENT_LOST;
    record = emit_and_read(&event);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN);
    CHECK(invalid_fields() == 1);

    event.id = CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN;
    record = emit_and_read(&event);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN);
    CHECK(invalid_fields() == 0);

    event.id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGOUT;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): the case under test.
    event.result = (crux_security_event_result_t)99;
    record = emit_and_read(&event);
    CHECK(record.result == CRUX_SECURITY_EVENT_FAILED);
    CHECK(invalid_fields() == 1);
}

static const char *only_line(void)
{
    CHECK(crux_security_event_test_drain_step() == 1);
    CHECK(event_test_line_count() == 1);
    return event_test_last_line();
}

static void line_format(void)
{
    /* The contract's examples, with a clock at the emit's own time. */
    event_test_boot();
    event_test_clock_set_ms(93211);
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    crux_security_event_t login = {
        .id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED,
        .result = CRUX_SECURITY_EVENT_DENIED,
        .channel = 3,
        .source_length = 4,
        .source = {192, 0, 2, 7},
    };
    crux_security_event_test_set_next_sequence(184);
    crux_security_event_emit(&login);
    CHECK(strcmp(only_line(), "evt=access.login_failed seq=184 up=93211 wall=1793750400 clock=rtc "
                              "chan=ssh src=192.0.2.7 res=denied") == 0);
    CHECK(event_test_line(0)->level == CRUX_LOG_WARN);
    event_test_lines_clear();

    event_test_clock_set_ms(94002);
    event_test_wall_set(1793750401, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    crux_security_event_t denied = {
        .id = CRUX_SECURITY_EVENT_ID_ACCESS_PERMISSION_DENIED,
        .result = CRUX_SECURITY_EVENT_DENIED,
        .channel = 2,
        .source_length = 4,
        .source = {192, 0, 2, 7},
        .user = "alice",
        .subject = "config.save",
    };
    crux_security_event_emit(&denied);
    CHECK(strcmp(only_line(), "evt=access.permission_denied seq=185 up=94002 wall=1793750401 "
                              "clock=rtc chan=web src=192.0.2.7 user=alice subj=config.save "
                              "res=denied") == 0);
    event_test_lines_clear();

    /* A result of OK is INFO, and every other result is WARN. */
    const struct {
        const char *text;
        crux_security_event_result_t result;
        crux_log_level_t level;
    } results[] = {
        {"ok", CRUX_SECURITY_EVENT_OK, CRUX_LOG_INFO},
        {"denied", CRUX_SECURITY_EVENT_DENIED, CRUX_LOG_WARN},
        {"delayed", CRUX_SECURITY_EVENT_DELAYED, CRUX_LOG_WARN},
        {"busy", CRUX_SECURITY_EVENT_BUSY, CRUX_LOG_WARN},
        {"limit", CRUX_SECURITY_EVENT_LIMIT, CRUX_LOG_WARN},
        {"invalid", CRUX_SECURITY_EVENT_INVALID, CRUX_LOG_WARN},
        {"failed", CRUX_SECURITY_EVENT_FAILED, CRUX_LOG_WARN},
    };
    for (size_t index = 0; index < sizeof(results) / sizeof(results[0]); ++index) {
        event_test_boot();
        crux_security_event_t event = {.id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGOUT,
                                       .result = results[index].result};
        crux_security_event_emit(&event);
        char expected[64];
        snprintf(expected, sizeof(expected), "evt=access.logout seq=1 up=1000 clock=none res=%s",
                 results[index].text);
        CHECK(strcmp(only_line(), expected) == 0);
        CHECK(event_test_line(0)->level == results[index].level);
    }

    /* Channels. Serial has no source, and no field is empty. */
    const struct {
        uint8_t channel;
        const char *text;
    } channels[] = {{1, "serial"}, {2, "web"}, {3, "ssh"}};
    for (size_t index = 0; index < sizeof(channels) / sizeof(channels[0]); ++index) {
        event_test_boot();
        crux_security_event_t event = {.id = CRUX_SECURITY_EVENT_ID_ACCESS_LOGOUT,
                                       .channel = channels[index].channel};
        crux_security_event_emit(&event);
        char expected[64];
        snprintf(expected, sizeof(expected),
                 "evt=access.logout seq=1 up=1000 clock=none chan=%s res=ok", channels[index].text);
        CHECK(strcmp(only_line(), expected) == 0);
    }

    /* An IPv6 source is its /64 prefix, and says so. */
    event_test_boot();
    crux_security_event_t ipv6 = {.id = CRUX_SECURITY_EVENT_ID_ACCESS_DELAYED,
                                  .result = CRUX_SECURITY_EVENT_DELAYED,
                                  .channel = 2,
                                  .source_length = 8,
                                  .source = {0x20, 0x01, 0x0D, 0xB8, 0, 0, 0xAB, 0x01}};
    crux_security_event_emit(&ipv6);
    CHECK(strcmp(only_line(), "evt=access.delayed seq=1 up=1000 clock=none chan=web "
                              "src=2001:db8:0:ab01::/64 res=delayed") == 0);

    /* A truncated subject keeps its mark in the line. */
    event_test_boot();
    crux_security_event_t longer = {.id = CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED,
                                    .subject = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvw"};
    crux_security_event_emit(&longer);
    CHECK(strcmp(only_line(), "evt=config.updated seq=1 up=1000 clock=none "
                              "subj=abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstu~ res=ok") == 0);
}

static void worst_case_line(void)
{
    /* Every field at its longest, formatted directly, because no real clock reaches these. */
    security_event_stored_t record = {
        .uptime_ms = 9223372036854ull, /* int64_t nanoseconds at their limit, 13 digits. */
        .sequence = UINT32_MAX,
        .id = CRUX_SECURITY_EVENT_ID_ACCESS_PERMISSION_DENIED,
        .result = CRUX_SECURITY_EVENT_DELAYED,
        .channel = 1,
        .source_length = 8,
        .source = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    };
    memset(record.user, 'u', sizeof(record.user) - 1u);
    memset(record.subject, 's', sizeof(record.subject) - 1u);
    char line[CRUX_LOG_TEXT_MAX + 1];
    size_t length = crux_security_event_line_format(
        &record, 4102444799, CRUX_SECURITY_EVENT_CLOCK_RTC, line, sizeof(line));
    CHECK(length == strlen(line));
    CHECK(length <= CRUX_LOG_TEXT_MAX);
    CHECK(length <= SECURITY_EVENT_LINE_WORST_CASE);
    /* The contract's 245 allows a source of 39, and a /64 needs 24. */
    CHECK(length == 245u - (39u - 24u));
    CHECK(strstr(line, " src=ffff:ffff:ffff:ffff::/64 ") != NULL);

    /* The lost line stays within the same bound. */
    length = crux_security_event_lost_line_format(UINT32_MAX, 9223372036854ull, false, 4102444799,
                                                  CRUX_SECURITY_EVENT_CLOCK_RTC, true, UINT32_MAX,
                                                  UINT32_MAX, line, sizeof(line));
    CHECK(length == strlen(line));
    CHECK(length <= SECURITY_EVENT_LINE_WORST_CASE);

    /* A longest record passes through the real path too, with a clock at its limit. */
    event_test_boot();
    event_test_clock_set_ns(INT64_MAX);
    event_test_wall_set(4102444799, 0, CRUX_REALTIME_SOURCE_EXTERNAL, true, true);
    crux_security_event_test_set_next_sequence(UINT32_MAX);
    crux_security_event_t event = {
        .id = CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REPLACED,
        .result = CRUX_SECURITY_EVENT_DELAYED,
        .channel = 1,
        .source_length = 8,
        .source = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        .user = "abcdefghijklmnopqrstuvwxyz01234",
        .subject = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwx",
    };
    crux_security_event_emit(&event);
    const char *written = only_line();
    CHECK(strlen(written) == 245u - (39u - 24u));
    CHECK(strstr(written, "seq=4294967295 up=9223372036854 wall=4102444799 clock=ext") != NULL);
}

int main(void)
{
    identifiers();
    user_rule();
    subject_rule();
    source_and_channel_rule();
    identifier_and_result_rule();
    line_format();
    worst_case_line();
    return event_test_finish("Security event conformance");
}
