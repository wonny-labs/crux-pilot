/* Builds log lines without printf: the fields are few and fixed, a format string would be one more
 * thing to get wrong, and the drain's stack stays small. */
#include "security_event_internal.h"

#include <string.h>

typedef struct {
    char *text;
    size_t capacity;
    size_t length;
} line_t;

/* Stops short rather than overruns. The worst-case line fits, so this never cuts one. */
static void append_text(line_t *line, const char *text)
{
    size_t room = line->capacity - 1u - line->length;
    size_t count = strlen(text);
    count = count < room ? count : room;
    memcpy(&line->text[line->length], text, count);
    line->length += count;
    line->text[line->length] = '\0';
}

static void append_u64(line_t *line, uint64_t value)
{
    char digits[21];
    size_t position = sizeof(digits) - 1u;
    digits[position] = '\0';
    do {
        position -= 1u;
        digits[position] = (char)('0' + (char)(value % 10u));
        value /= 10u;
    } while (value != 0u);
    append_text(line, &digits[position]);
}

static void append_hex(line_t *line, unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    char text[5];
    size_t position = sizeof(text) - 1u;
    text[position] = '\0';
    do {
        position -= 1u;
        text[position] = digits[value & 0xFu];
        value >>= 4u;
    } while (value != 0u);
    append_text(line, &text[position]);
}

static void append_source(line_t *line, const security_event_stored_t *record)
{
    if (record->source_length == 4u) {
        for (size_t index = 0; index < 4u; ++index) {
            if (index != 0) {
                append_text(line, ".");
            }
            append_u64(line, record->source[index]);
        }
    } else if (record->source_length == 8u) {
        for (size_t index = 0; index < 8u; index += 2u) {
            if (index != 0) {
                append_text(line, ":");
            }
            append_hex(line, ((unsigned)record->source[index] << 8u) | record->source[index + 1u]);
        }
        append_text(line, "::/64");
    }
}

static const char *clock_text(crux_security_event_clock_t clock)
{
    switch (clock) {
    case CRUX_SECURITY_EVENT_CLOCK_UNKNOWN:
        return "unk";
    case CRUX_SECURITY_EVENT_CLOCK_RTC:
        return "rtc";
    case CRUX_SECURITY_EVENT_CLOCK_MANUAL:
        return "man";
    case CRUX_SECURITY_EVENT_CLOCK_EXTERNAL:
        return "ext";
    case CRUX_SECURITY_EVENT_CLOCK_NONE:
        break;
    }
    return "none";
}

static const char *channel_text(uint8_t channel)
{
    switch (channel) {
    case 1:
        return "serial";
    case 2:
        return "web";
    case 3:
        return "ssh";
    default:
        return NULL;
    }
}

static const char *result_text(uint8_t result)
{
    switch ((crux_security_event_result_t)result) {
    case CRUX_SECURITY_EVENT_OK:
        return "ok";
    case CRUX_SECURITY_EVENT_DENIED:
        return "denied";
    case CRUX_SECURITY_EVENT_DELAYED:
        return "delayed";
    case CRUX_SECURITY_EVENT_BUSY:
        return "busy";
    case CRUX_SECURITY_EVENT_LIMIT:
        return "limit";
    case CRUX_SECURITY_EVENT_INVALID:
        return "invalid";
    case CRUX_SECURITY_EVENT_FAILED:
        break;
    }
    return "failed";
}

/* What every line starts with, in this order. */
typedef struct {
    const char *name;
    uint32_t sequence;
    uint64_t uptime_ms;
    bool no_uptime;
    int64_t wall_s;
    crux_security_event_clock_t clock;
} line_start_t;

static void append_start(line_t *line, const line_start_t *start)
{
    append_text(line, "evt=");
    append_text(line, start->name);
    append_text(line, " seq=");
    append_u64(line, start->sequence);
    if (!start->no_uptime) {
        append_text(line, " up=");
        append_u64(line, start->uptime_ms);
    }
    if (start->clock != CRUX_SECURITY_EVENT_CLOCK_NONE) {
        append_text(line, " wall=");
        append_u64(line, (uint64_t)start->wall_s);
    }
    append_text(line, " clock=");
    append_text(line, clock_text(start->clock));
}

size_t crux_security_event_line_format(const security_event_stored_t *record, int64_t wall_s,
                                       crux_security_event_clock_t clock, char *text,
                                       size_t capacity)
{
    line_t line = {text, capacity, 0};
    text[0] = '\0';
    const line_start_t start = {crux_security_event_name(record->id),
                                record->sequence,
                                record->uptime_ms,
                                record->no_uptime,
                                wall_s,
                                clock};
    append_start(&line, &start);
    const char *channel = channel_text(record->channel);
    if (channel != NULL) {
        append_text(&line, " chan=");
        append_text(&line, channel);
    }
    if (record->source_length != 0) {
        append_text(&line, " src=");
        append_source(&line, record);
    }
    if (record->user[0] != '\0') {
        append_text(&line, " user=");
        append_text(&line, record->user);
    }
    if (record->subject[0] != '\0') {
        append_text(&line, " subj=");
        append_text(&line, record->subject);
    }
    append_text(&line, " res=");
    append_text(&line, result_text(record->result));
    return line.length;
}

size_t crux_security_event_lost_line_format(uint32_t sequence, uint64_t uptime_ms, bool no_uptime,
                                            int64_t wall_s, crux_security_event_clock_t clock,
                                            bool has_first, uint32_t first, uint32_t count,
                                            char *text, size_t capacity)
{
    line_t line = {text, capacity, 0};
    text[0] = '\0';
    const line_start_t start = {crux_security_event_name(CRUX_SECURITY_EVENT_ID_EVENT_LOST),
                                sequence,
                                uptime_ms,
                                no_uptime,
                                wall_s,
                                clock};
    append_start(&line, &start);
    if (has_first) {
        append_text(&line, " first=");
        append_u64(&line, first);
    }
    append_text(&line, " count=");
    append_u64(&line, count);
    append_text(&line, " res=failed");
    return line.length;
}
