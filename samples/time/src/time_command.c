#include "crux_time_command.h"

#include <crux/time.h>
#include <crux/time_calendar.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct token {
    const char *data;
    size_t size;
};

static bool is_space(char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\v' ||
           value == '\f';
}

static bool token_equals(struct token value, const char *expected) {
    const size_t expected_size = strlen(expected);
    return value.size == expected_size && memcmp(value.data, expected, expected_size) == 0;
}

static bool tokenize(const char *input, struct token *tokens, size_t capacity, size_t *count) {
    const char *cursor = input;
    size_t used = 0;

    while (*cursor != '\0') {
        while (is_space(*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (used == capacity) {
            return false;
        }
        tokens[used].data = cursor;
        while (*cursor != '\0' && !is_space(*cursor)) {
            ++cursor;
        }
        tokens[used].size = (size_t)(cursor - tokens[used].data);
        ++used;
    }

    *count = used;
    return true;
}

static bool parse_digits(const char *input, size_t count, int32_t *out) {
    int32_t value = 0;

    for (size_t index = 0; index < count; ++index) {
        const char digit = input[index];
        if (digit < '0' || digit > '9') {
            return false;
        }
        value = (value * 10) + (digit - '0');
    }
    *out = value;
    return true;
}

static bool parse_timestamp(struct token value, crux_time_utc_t *out) {
    if (value.size != 19 || value.data[4] != '-' || value.data[7] != '-' || value.data[10] != 'T' ||
        value.data[13] != ':' || value.data[16] != ':') {
        return false;
    }

    return parse_digits(value.data, 4, &out->year) &&
           parse_digits(value.data + 5, 2, &out->month) &&
           parse_digits(value.data + 8, 2, &out->day) &&
           parse_digits(value.data + 11, 2, &out->hour) &&
           parse_digits(value.data + 14, 2, &out->minute) &&
           parse_digits(value.data + 17, 2, &out->second);
}

static crux_time_command_result_t make_result(crux_status_t status, crux_time_command_kind_t kind,
                                              const char *text) {
    crux_time_command_result_t value = {0};
    value.status = status;
    value.kind = kind;
    if (text != NULL) {
        const size_t text_size = strlen(text);
        if (text_size >= sizeof(value.text)) {
            value.status = CRUX_ERR_IO;
        } else {
            memcpy(value.text, text, text_size + 1);
        }
    }
    return value;
}

static void write_two_digits(char *output, int32_t value) {
    output[0] = (char)('0' + (value / 10));
    output[1] = (char)('0' + (value % 10));
}

crux_time_command_result_t crux_time_command_execute(const char *input) {
    struct token tokens[4] = {0};
    size_t count = 0;

    if (input == NULL) {
        return make_result(CRUX_ERR_INVALID_ARGUMENT, CRUX_TIME_COMMAND_LINE, NULL);
    }
    if (!tokenize(input, tokens, sizeof(tokens) / sizeof(tokens[0]), &count)) {
        return make_result(CRUX_ERR_INVALID_ARGUMENT, CRUX_TIME_COMMAND_LINE, NULL);
    }
    if (count == 1 && token_equals(tokens[0], "help")) {
        return make_result(CRUX_OK, CRUX_TIME_COMMAND_HELP, NULL);
    }
    if (count == 2 && token_equals(tokens[0], "time") && token_equals(tokens[1], "help")) {
        return make_result(CRUX_OK, CRUX_TIME_COMMAND_HELP, NULL);
    }
    if (count < 2 || !token_equals(tokens[0], "time")) {
        return make_result(CRUX_ERR_INVALID_ARGUMENT, CRUX_TIME_COMMAND_LINE, NULL);
    }

    if (count == 2 && token_equals(tokens[1], "get")) {
        crux_time_instant_t instant;
        crux_time_utc_t calendar;
        crux_status_t status = crux_time_get(&instant);
        if (status != CRUX_OK) {
            return make_result(status, CRUX_TIME_COMMAND_LINE, NULL);
        }
        status = crux_time_to_utc(instant, &calendar);
        if (status != CRUX_OK) {
            return make_result(status, CRUX_TIME_COMMAND_LINE, NULL);
        }

        crux_time_command_result_t value = {0};
        value.status = CRUX_OK;
        value.kind = CRUX_TIME_COMMAND_LINE;
        value.text[0] = (char)('0' + (calendar.year / 1000));
        value.text[1] = (char)('0' + ((calendar.year / 100) % 10));
        value.text[2] = (char)('0' + ((calendar.year / 10) % 10));
        value.text[3] = (char)('0' + (calendar.year % 10));
        value.text[4] = '-';
        write_two_digits(value.text + 5, calendar.month);
        value.text[7] = '-';
        write_two_digits(value.text + 8, calendar.day);
        value.text[10] = 'T';
        write_two_digits(value.text + 11, calendar.hour);
        value.text[13] = ':';
        write_two_digits(value.text + 14, calendar.minute);
        value.text[16] = ':';
        write_two_digits(value.text + 17, calendar.second);
        memcpy(value.text + 19, " UTC", sizeof(" UTC"));
        return value;
    }

    if (count == 3 && token_equals(tokens[1], "set")) {
        crux_time_utc_t calendar = {0};
        crux_time_instant_t instant;
        if (!parse_timestamp(tokens[2], &calendar)) {
            return make_result(CRUX_ERR_INVALID_ARGUMENT, CRUX_TIME_COMMAND_LINE, NULL);
        }
        crux_status_t status = crux_time_from_utc(&calendar, &instant);
        if (status != CRUX_OK) {
            return make_result(status, CRUX_TIME_COMMAND_LINE, NULL);
        }
        status = crux_time_set(instant);
        return status == CRUX_OK ? make_result(CRUX_OK, CRUX_TIME_COMMAND_LINE, "OK")
                                 : make_result(status, CRUX_TIME_COMMAND_LINE, NULL);
    }

    return make_result(CRUX_ERR_INVALID_ARGUMENT, CRUX_TIME_COMMAND_LINE, NULL);
}

const char *crux_time_command_help_text(void) {
    return "Commands:\n"
           "  time set YYYY-MM-DDTHH:MM:SS\n"
           "  time get\n"
           "  help\n"
           "Input is UTC.\n";
}

const char *crux_time_command_startup_banner(void) {
    return "Crux Pilot Time host simulation (controlled clock; no wall-clock progression).\n";
}

const char *crux_time_command_cli_error_text(crux_status_t status) {
    switch (status) {
    case CRUX_OK:
        return "OK";
    case CRUX_ERR_INVALID_ARGUMENT:
        return "ERR INVALID_ARGUMENT";
    case CRUX_ERR_OUT_OF_RANGE:
        return "ERR OUT_OF_RANGE";
    case CRUX_ERR_NOT_READY:
        return "ERR NOT_READY";
    case CRUX_ERR_NO_DATA:
        return "ERR NOT_SET";
    default:
        return "ERR IO_ERROR";
    }
}
