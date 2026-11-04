/* Text forms, parser limits, validators, sensitive values and the owning application's calls. */
#include "service_test.h"
#include "text_vectors.h"

#include <stdlib.h>

static void bool_forms(void)
{
    fixture_start();
    CHECK(update_text(s_both, NAME_MODE, "true") == CRUX_CONFIG_SERVICE_OK);
    expect_text(s_both, NAME_MODE, "true");
    CHECK(update_text(s_both, NAME_MODE, "false") == CRUX_CONFIG_SERVICE_OK);
    expect_text(s_both, NAME_MODE, "false");

    static const char *const bad[] = {"",   "TRUE", "True",  "1",     "0",      "yes",
                                      "no", "t",    "tru",   "truex", "true ",  " true",
                                      "on", "off",  "FALSE", "fals",  "falsey", "null"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(update_text(s_both, NAME_MODE, bad[i]) == CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    }
    crux_config_service_item_t applied;
    CHECK(crux_config_service_update(s_both, NAME_MODE, "true\0", 5, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    /* The length decides, not a terminator. */
    CHECK(crux_config_service_update(s_both, NAME_MODE, "truest", 4, &applied) ==
          CRUX_CONFIG_SERVICE_OK);
    expect_text(s_both, NAME_MODE, "true");
    CHECK(crux_config_service_update(s_both, NAME_MODE, "true", 3, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    expect_text(s_both, NAME_MODE, "true");
}

static void u32_forms(void)
{
    fixture_start();
    static const struct {
        const char *text;
    } good[] = {{"0"}, {"1"}, {"9"}, {"10"}, {"100"}, {"4294967295"}};
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); ++i) {
        CHECK(update_text(s_both, NAME_WIDE, good[i].text) == CRUX_CONFIG_SERVICE_OK);
        expect_text(s_both, NAME_WIDE, good[i].text);
    }
    expect_u32(NAME_WIDE, UINT32_MAX);

    static const char *const bad[] = {"",
                                      "-1",
                                      "+1",
                                      "01",
                                      "00",
                                      "007",
                                      " 1",
                                      "1 ",
                                      "0x10",
                                      "1.5",
                                      "1e3",
                                      "a",
                                      "4294967296",
                                      "42949672950",
                                      "99999999999",
                                      "18446744073709551616",
                                      "99999999999999999999999999999999",
                                      "1\n",
                                      "\xc2\xb2"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(update_text(s_both, NAME_WIDE, bad[i]) == CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    }
    crux_config_service_item_t applied;
    CHECK(crux_config_service_update(s_both, NAME_WIDE, "12\0", 3, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_update(s_both, NAME_WIDE, "123", SIZE_MAX, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    expect_u32(NAME_WIDE, UINT32_MAX);
    /* Each refusal left one event with only the name. */
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_REJECTED) ==
          sizeof(bad) / sizeof(bad[0]) + 2u);
    CHECK(strcmp(config_events_get(config_events_count() - 1)->name, NAME_WIDE) == 0);
}

static void validator_rejects(void)
{
    fixture_start();
    /* It parses, so it is the validator that refuses, and the old value stays. */
    CHECK(update_text(s_both, NAME_LIMIT, "101") == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    CHECK(update_text(s_both, NAME_LIMIT, "0") == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    expect_u32(NAME_LIMIT, 5);
    CHECK(update_text(s_both, NAME_LIMIT, "100") == CRUX_CONFIG_SERVICE_OK);
    expect_u32(NAME_LIMIT, 100);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_REJECTED) == 2);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_UPDATED) == 1);

    CHECK(update_text(s_both, NAME_HOST, "bad name") == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    expect_string(NAME_HOST, "crux");

    /* A rejected update leaves no mark to save. */
    crux_config_service_save_report_t report;
    CHECK(update_text(s_both, NAME_HOST, "bad") == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    CHECK(crux_config_service_save_local(&report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 1);
}

static void string_forms(void)
{
    fixture_start();
    /* The bytes as given, with no parsing or quoting. */
    CHECK(update_text(s_both, NAME_HOST, "a \"quoted\" \\value") == CRUX_CONFIG_SERVICE_OK);
    expect_text(s_both, NAME_HOST, "a \"quoted\" \\value");
    CHECK(update_text(s_both, NAME_HOST, "") == CRUX_CONFIG_SERVICE_OK);
    expect_text(s_both, NAME_HOST, "");

    /* Text need not be terminated: the length decides. */
    const char *longer = "abcdef";
    crux_config_service_item_t applied;
    CHECK(crux_config_service_update(s_both, NAME_HOST, longer, 3, &applied) ==
          CRUX_CONFIG_SERVICE_OK);
    expect_text(s_both, NAME_HOST, "abc");
    CHECK(applied.type == CRUX_CONFIG_STRING);

    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_invalid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_invalid_vectors[i];
        crux_config_service_result_t result =
            crux_config_service_update(s_both, NAME_HOST, vector->bytes, vector->length, &applied);
        if (result != CRUX_CONFIG_SERVICE_ERR_REJECTED) {
            printf("string %s\n", vector->what);
        }
        CHECK(result == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    }
    expect_text(s_both, NAME_HOST, "abc");
    for (size_t i = 0; i < CONFIG_TEXT_VECTOR_COUNT(config_text_valid_vectors); ++i) {
        const config_text_vector_t *vector = &config_text_valid_vectors[i];
        CHECK(crux_config_service_update(s_both, NAME_HOST, vector->bytes, vector->length,
                                         &applied) == CRUX_CONFIG_SERVICE_OK);
        expect_text(s_both, NAME_HOST, vector->bytes);
    }

    /* The longest value, then one more, then a length that a copy would overrun. */
    static char longest[CRUX_CONFIG_STRING_MAX + 2];
    memset(longest, 'x', sizeof(longest));
    CHECK(crux_config_service_update(s_both, NAME_HOST, longest, CRUX_CONFIG_STRING_MAX,
                                     &applied) == CRUX_CONFIG_SERVICE_OK);
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    size_t length = 0;
    crux_config_type_t type = CRUX_CONFIG_BOOL;
    CHECK(crux_config_service_read(s_both, NAME_HOST, text, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_OK);
    CHECK(length == CRUX_CONFIG_STRING_MAX && text[CRUX_CONFIG_STRING_MAX] == '\0');
    CHECK(type == CRUX_CONFIG_STRING);
    CHECK(crux_config_service_update(s_both, NAME_HOST, longest, CRUX_CONFIG_STRING_MAX + 1u,
                                     &applied) == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    CHECK(crux_config_service_update(s_both, NAME_HOST, longest, SIZE_MAX, &applied) ==
          CRUX_CONFIG_SERVICE_ERR_REJECTED);

    /* A parameter's own capacity is the bound, whichever is lower. */
    CHECK(update_text(s_both, NAME_SMALL, "1234567") == CRUX_CONFIG_SERVICE_OK);
    CHECK(update_text(s_both, NAME_SMALL, "12345678") == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    expect_string(NAME_SMALL, "1234567");
}

/* Fails the case if a known secret appears anywhere an adapter or an operator can see. */
#define SECRET "correct-horse-battery-secret"

static void expect_no_secret_in_events(void)
{
    for (size_t i = 0; i < config_events_count(); ++i) {
        CHECK(strstr(config_events_get(i)->name, SECRET) == NULL);
    }
    for (size_t i = 0; i < access_fake_event_count(); ++i) {
        const crux_access_event_t *event = access_fake_event(i);
        CHECK(strstr(event->actor, SECRET) == NULL);
        CHECK(strstr(event->account, SECRET) == NULL);
        CHECK(strstr(event->permission, SECRET) == NULL);
    }
}

static void bytes_absent(const void *memory, size_t size)
{
    const char *bytes = memory;
    size_t secret = strlen(SECRET);
    for (size_t i = 0; i + secret <= size; ++i) {
        CHECK(memcmp(bytes + i, SECRET, secret) != 0);
    }
}

static void sensitive_is_write_only(void)
{
    fixture_start();
    crux_config_service_item_t applied;
    memset(&applied, 0, sizeof(applied));
    CHECK(crux_config_service_update(s_writer, NAME_SECRET, SECRET, strlen(SECRET), &applied) ==
          CRUX_CONFIG_SERVICE_OK);
    CHECK(applied.sensitive && applied.unsaved && !applied.readable);
    bytes_absent(&applied, sizeof(applied));

    /* Every session gets the same refusal, and the outputs stay as they were. */
    const crux_access_session_t sessions[] = {s_reader, s_writer, s_both, s_admin};
    for (size_t i = 0; i < sizeof(sessions) / sizeof(sessions[0]); ++i) {
        char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
        memset(text, 'z', sizeof(text));
        size_t length = 99;
        crux_config_type_t type = CRUX_CONFIG_BOOL;
        CHECK(crux_config_service_read(sessions[i], NAME_SECRET, text, sizeof(text), &length,
                                       &type) == CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY);
        CHECK(text[0] == 'z' && length == 99 && type == CRUX_CONFIG_BOOL);
    }
    /* A session that holds neither still sees nothing there. */
    char text[8];
    size_t length;
    crux_config_type_t type;
    CHECK(crux_config_service_read(s_stranger, NAME_SECRET, text, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);

    /* Listings carry no value. */
    size_t cursor = 0;
    crux_config_service_item_t item;
    while (crux_config_service_next(s_both, &cursor, &item) == CRUX_CONFIG_SERVICE_OK) {
        bytes_absent(&item, sizeof(item));
    }

    /* Rejected text holding the secret does not reach an event either. */
    CHECK(update_text(s_both, NAME_WIDE, SECRET) == CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(update_text(s_both, NAME_HOST, "bad " SECRET) == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    CHECK(update_text(s_reader, NAME_SECRET, SECRET) == CRUX_CONFIG_SERVICE_ERR_DENIED);
    CHECK(update_text(s_stranger, SECRET, SECRET) == CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    /* A name that is not in the table never reaches an event, because it is client text. */
    expect_no_secret_in_events();
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_REJECTED) == 2);

    /* A save produces events with names only. */
    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save(s_saver, &report) == CRUX_CONFIG_SERVICE_OK);
    bytes_absent(&report, sizeof(report));
    expect_no_secret_in_events();

    /* The application owns the value, so its own call still reads it. */
    expect_string(NAME_SECRET, SECRET);
}

static void application_calls(void)
{
    fixture_start();
    /* No session and no permission: even a parameter no role holds. */
    expect_u32(NAME_PRIVATE, 9);
    crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 10};
    CHECK(crux_config_service_try_update(NAME_PRIVATE, &value) == CRUX_CONFIG_SERVICE_OK);
    expect_u32(NAME_PRIVATE, 10);

    /* Rejected and missing. */
    value.data.u32 = 101;
    CHECK(crux_config_service_try_update(NAME_LIMIT, &value) == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    expect_u32(NAME_LIMIT, 5);
    value.type = CRUX_CONFIG_BOOL;
    CHECK(crux_config_service_try_update(NAME_LIMIT, &value) == CRUX_CONFIG_SERVICE_ERR_REJECTED);
    CHECK(crux_config_service_try_update("no_such_name", &value) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(crux_config_service_try_update(NULL, &value) == CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_try_update(NAME_LIMIT, NULL) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);

    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    crux_config_value_t out = {.type = CRUX_CONFIG_BOOL, .data.boolean = true};
    CHECK(crux_config_service_try_read("no_such_name", &out, text, sizeof(text)) ==
          CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    CHECK(crux_config_service_try_read(NULL, &out, text, sizeof(text)) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_try_read(NAME_LIMIT, NULL, text, sizeof(text)) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    /* A string needs a buffer that holds it, and a scalar needs none. */
    CHECK(crux_config_service_try_read(NAME_HOST, &out, NULL, 0) ==
          CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_service_try_read(NAME_HOST, &out, text, 4) ==
          CRUX_CONFIG_SERVICE_ERR_TOO_SMALL);
    CHECK(out.type == CRUX_CONFIG_BOOL && out.data.boolean);
    CHECK(crux_config_service_try_read(NAME_HOST, &out, text, 5) == CRUX_CONFIG_SERVICE_OK);
    CHECK(out.data.string.text == text && out.data.string.length == 4);
    CHECK(strcmp(text, "crux") == 0);

    /* A string update, with text that is terminated and may alias the buffer it came from. */
    crux_config_value_t candidate = out;
    CHECK(crux_config_service_try_update(NAME_HOST, &candidate) == CRUX_CONFIG_SERVICE_OK);
    expect_string(NAME_HOST, "crux");

    /* None of this reached the event port, so a control thread never meets its lock. */
    CHECK(config_events_count() == 0);
    CHECK(access_fake_event_count() == 0);

    /* Each accepted update is marked, and the application's save reaches it silently. */
    crux_config_service_save_report_t report;
    CHECK(crux_config_service_save_local(&report) == CRUX_CONFIG_SERVICE_OK);
    CHECK(report.saved == 2 && report.failed_name == NULL);
    CHECK(config_events_count() == 0);
    CHECK(crux_config_service_save_local(NULL) == CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
}

int main(void)
{
    run_case("bool forms", bool_forms);
    run_case("u32 forms", u32_forms);
    run_case("validator rejects", validator_rejects);
    run_case("string forms", string_forms);
    run_case("sensitive is write-only", sensitive_is_write_only);
    run_case("application calls", application_calls);
    return suite_finish("ConfigService.values");
}
