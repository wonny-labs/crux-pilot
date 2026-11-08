#include "bench_report_format.h"

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool overflow;
} writer_t;

static void put_char(writer_t *w, char c)
{
    if (w->length + 1u >= w->capacity) {
        w->overflow = true;
        return;
    }
    w->buffer[w->length++] = c;
}

static void put_text(writer_t *w, const char *text)
{
    while (*text != '\0') {
        put_char(w, *text++);
    }
}

static void put_u32(writer_t *w, uint32_t value)
{
    char digits[10];
    size_t count = 0;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (count != 0u) {
        put_char(w, digits[--count]);
    }
}

static void put_field(writer_t *w, const char *key, uint32_t value)
{
    put_char(w, ' ');
    put_text(w, key);
    put_char(w, '=');
    put_u32(w, value);
}

static bool start(writer_t *w, char *out, size_t capacity, const char *prefix)
{
    *w = (writer_t){.buffer = out, .capacity = capacity};
    if (out == NULL || capacity == 0u) {
        return false;
    }
    out[0] = '\0';
    put_text(w, prefix);
    return true;
}

static size_t refuse(writer_t *w)
{
    w->buffer[0] = '\0';
    return 0u;
}

static size_t finish(writer_t *w)
{
    if (w->overflow) {
        return refuse(w);
    }
    w->buffer[w->length] = '\0';
    return w->length;
}

static bool name_valid(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '.' || c == '[' || c == ']' || c == '-' || c == '@';
}

size_t bench_report_sanitize_name(char *out, size_t capacity, const char *name,
                                  uintptr_t fallback_id)
{
    size_t length = 0;

    if (out == NULL || capacity < 2u) {
        return 0u;
    }
    if (capacity > BENCH_REPORT_NAME_MAX) {
        capacity = BENCH_REPORT_NAME_MAX;
    }
    if (name == NULL || name[0] == '\0') {
        static const char prefix[] = "thread@";
        char digits[sizeof(uintptr_t) * 2u];
        size_t count = 0;

        for (; prefix[length] != '\0' && length + 1u < capacity; length++) {
            out[length] = prefix[length];
        }
        do {
            unsigned nibble = (unsigned)(fallback_id & 0xfu);
            digits[count++] = (char)(nibble < 10u ? '0' + nibble : 'a' + (nibble - 10u));
            fallback_id >>= 4;
        } while (fallback_id != 0u);
        while (count != 0u && length + 1u < capacity) {
            out[length++] = digits[--count];
        }
        out[length] = '\0';
        return length;
    }
    for (; name[length] != '\0' && length + 1u < capacity; length++) {
        out[length] = name_valid(name[length]) ? name[length] : '_';
    }
    out[length] = '\0';
    return length;
}

static const char *reason_text(bench_report_reason_t reason)
{
    switch (reason) {
    case BENCH_REPORT_BOOT:
        return "boot";
    case BENCH_REPORT_QUIET:
        return "quiet";
    case BENCH_REPORT_IDLE:
        return "idle";
    default:
        return NULL;
    }
}

size_t bench_report_format_begin(char *out, size_t capacity, uint32_t seq,
                                 bench_report_reason_t reason, uint32_t uptime_ms,
                                 uint32_t bursts)
{
    writer_t w;
    const char *text = reason_text(reason);

    if (!start(&w, out, capacity, "BENCH-REPORT v=1 begin")) {
        return 0u;
    }
    if (text == NULL) {
        return refuse(&w);
    }
    put_field(&w, "seq", seq);
    put_text(&w, " reason=");
    put_text(&w, text);
    put_field(&w, "uptime_ms", uptime_ms);
    put_field(&w, "bursts", bursts);
    return finish(&w);
}

size_t bench_report_format_stack(char *out, size_t capacity, uint32_t seq, const char *name,
                                 uint32_t size, uint32_t unused, bool ended)
{
    writer_t w;
    char clean[BENCH_REPORT_NAME_MAX];

    if (!start(&w, out, capacity, "BENCH-STACK")) {
        return 0u;
    }
    if (name == NULL || name[0] == '\0' || unused > size) {
        return refuse(&w);
    }
    (void)bench_report_sanitize_name(clean, sizeof(clean), name, 0u);
    put_field(&w, "seq", seq);
    put_text(&w, " thread=");
    put_text(&w, clean);
    put_field(&w, "size", size);
    put_field(&w, "used", size - unused);
    put_field(&w, "unused", unused);
    put_text(&w, ended ? " state=ended" : " state=live");
    return finish(&w);
}

size_t bench_report_format_heap(char *out, size_t capacity, uint32_t seq, uint32_t arena,
                                uint32_t in_use, uint32_t peak, uint32_t free_bytes)
{
    writer_t w;

    if (!start(&w, out, capacity, "BENCH-HEAP")) {
        return 0u;
    }
    put_field(&w, "seq", seq);
    put_field(&w, "arena", arena);
    put_field(&w, "in_use", in_use);
    put_field(&w, "peak", peak);
    put_field(&w, "free", free_bytes);
    return finish(&w);
}

size_t bench_report_format_end(char *out, size_t capacity, uint32_t seq, uint32_t lines,
                               uint32_t missed)
{
    writer_t w;

    if (!start(&w, out, capacity, "BENCH-REPORT v=1 end")) {
        return 0u;
    }
    put_field(&w, "seq", seq);
    put_field(&w, "lines", lines);
    put_field(&w, "missed", missed);
    return finish(&w);
}

static bool fingerprint_valid(const char *text)
{
    if (text == NULL) {
        return false;
    }
    for (size_t i = 0; i < BENCH_REPORT_FINGERPRINT_CHARS; i++) {
        char c = text[i];
        bool separator = i % 3u == 2u;
        bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        if (c == '\0' || (separator ? c != ':' : !hex)) {
            return false;
        }
    }
    return text[BENCH_REPORT_FINGERPRINT_CHARS] == '\0';
}

size_t bench_report_format_cert(char *out, size_t capacity, const char *fingerprint,
                                bool generated)
{
    writer_t w;

    if (!start(&w, out, capacity, "BENCH-CERT v=1 sha256=")) {
        return 0u;
    }
    if (!fingerprint_valid(fingerprint)) {
        return refuse(&w);
    }
    put_text(&w, fingerprint);
    put_text(&w, generated ? " source=generated" : " source=stored");
    return finish(&w);
}
