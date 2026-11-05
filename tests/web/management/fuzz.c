/* The reader of the update body, and the routes that use it, against malformed and oversized input.
 *
 * Three levels. A table holds the grammar one case at a time. A deterministic generator then feeds
 * the reader random and mutated bodies, with a fixed seed so that a failure reproduces, and checks
 * what must hold for every input whether it is accepted or not. Last, the same kind of input goes
 * through the whole route, to show that no input changes a value without a 200, reaches a 5xx or
 * leaves a value in a buffer. Under host-sanitize, memory errors in any of it stop the run. */
#include "management_test.h"

#include <inttypes.h>

#define LIST "/api/v1/config"

/* xorshift64*, with a seed per use so that each stream is independent and repeatable. */
static uint64_t random_state;

static uint64_t next_random(void)
{
    random_state ^= random_state >> 12;
    random_state ^= random_state << 25;
    random_state ^= random_state >> 27;
    return random_state * UINT64_C(0x2545f4914f6cdd1d);
}

static size_t below(size_t bound)
{
    return (size_t)(next_random() % bound);
}

/* The reader. */

typedef struct {
    const char *body;
    web_value_kind_t kind;
    bool boolean;
    bool number_ok;
    uint32_t number;
    const char *text; /* A string's decoded bytes. */
    size_t length;
} accepted_t;

static const accepted_t accepted[] = {
    {"{\"value\":true}", WEB_VALUE_BOOL, true, false, 0u, NULL, 0u},
    {"{\"value\":false}", WEB_VALUE_BOOL, false, false, 0u, NULL, 0u},
    {"{\"value\":0}", WEB_VALUE_NUMBER, false, true, 0u, NULL, 0u},
    {"{\"value\":7}", WEB_VALUE_NUMBER, false, true, 7u, NULL, 0u},
    {"{\"value\":4294967295}", WEB_VALUE_NUMBER, false, true, 4294967295u, NULL, 0u},
    {"{\"value\":4294967296}", WEB_VALUE_NUMBER, false, false, 0u, NULL, 0u},
    {"{\"value\":18446744073709551616}", WEB_VALUE_NUMBER, false, false, 0u, NULL, 0u},
    {"{\"value\":-1}", WEB_VALUE_NUMBER, false, false, 0u, NULL, 0u},
    {"{\"value\":-0}", WEB_VALUE_NUMBER, false, false, 0u, NULL, 0u},
    {"{\"value\":1.0}", WEB_VALUE_NUMBER, false, false, 0u, NULL, 0u},
    {"{\"value\":1e3}", WEB_VALUE_NUMBER, false, false, 0u, NULL, 0u},
    {"{\"value\":1E+3}", WEB_VALUE_NUMBER, false, false, 0u, NULL, 0u},
    {"{\"value\":1e-3}", WEB_VALUE_NUMBER, false, false, 0u, NULL, 0u},
    {"{\"value\":\"\"}", WEB_VALUE_STRING, false, false, 0u, "", 0u},
    {"{\"value\":\"abc\"}", WEB_VALUE_STRING, false, false, 0u, "abc", 3u},
    {"{\"value\":\"a\\\"b\\\\c\\/d\"}", WEB_VALUE_STRING, false, false, 0u, "a\"b\\c/d", 7u},
    {"{\"value\":\"\\b\\f\\n\\r\\t\"}", WEB_VALUE_STRING, false, false, 0u, "\b\f\n\r\t", 5u},
    {"{\"value\":\"\\u0041\\u00e9\\u20ac\"}", WEB_VALUE_STRING, false, false, 0u,
     "A\xc3\xa9\xe2\x82\xac", 6u},
    {"{\"value\":\"\\ud83d\\ude00\"}", WEB_VALUE_STRING, false, false, 0u, "\xf0\x9f\x98\x80", 4u},
    {"{\"value\":\"\\uD83D\\uDE00\"}", WEB_VALUE_STRING, false, false, 0u, "\xf0\x9f\x98\x80", 4u},
    {"{\"value\":\"raw \xc3\xa9\"}", WEB_VALUE_STRING, false, false, 0u, "raw \xc3\xa9", 6u},
    {"  {  \"value\"  :  true  }  ", WEB_VALUE_BOOL, true, false, 0u, NULL, 0u},
    {"{\"v\\u0061lue\":1}", WEB_VALUE_NUMBER, false, true, 1u, NULL, 0u},
};

static void test_reader_accepts(void)
{
    for (size_t index = 0; index < sizeof(accepted) / sizeof(accepted[0]); ++index) {
        const accepted_t *want = &accepted[index];
        char text[64];
        web_value_t value = {.text = text, .size = sizeof(text)};

        if (!web_mgmt_parse_update((const uint8_t *)want->body, strlen(want->body), &value)) {
            printf("  not accepted: %s\n", want->body);
            CHECK(!"accepted");
            continue;
        }
        CHECK(value.kind == want->kind);
        if (want->kind == WEB_VALUE_BOOL) {
            CHECK(value.boolean == want->boolean);
        } else if (want->kind == WEB_VALUE_NUMBER) {
            CHECK(value.number_ok == want->number_ok);
            CHECK(value.number == want->number);
        } else {
            CHECK(value.length == want->length);
            CHECK(memcmp(text, want->text, want->length) == 0 && text[want->length] == '\0');
        }
    }
}

static const char *const rejected[] = {
    "",
    " ",
    "{",
    "}",
    "{}",
    "[]",
    "[{\"value\":1}]",
    "null",
    "true",
    "1",
    "\"value\"",
    "{\"value\"",
    "{\"value\":",
    "{\"value\":}",
    "{\"value\" 1}",
    "{\"value\":1",
    "{\"value\":1,",
    "{\"value\":1,}",
    "{\"value\":1}}",
    "{\"value\":1}x",
    "{\"value\":1} {}",
    "{,\"value\":1}",
    "{\"value\":1,\"value\":1}",
    "{\"value\":1,\"x\":1}",
    "{\"x\":1,\"value\":1}",
    "{\"x\":1}",
    "{\"Value\":1}",
    "{\"value \":1}",
    "{\" value\":1}",
    "{\"valu\":1}",
    "{\"values\":1}",
    "{\"value\":null}",
    "{\"value\":[]}",
    "{\"value\":[1]}",
    "{\"value\":{}}",
    "{\"value\":{\"a\":1}}",
    "{\"value\":01}",
    "{\"value\":00}",
    "{\"value\":-}",
    "{\"value\":+1}",
    "{\"value\":.1}",
    "{\"value\":1.}",
    "{\"value\":1.e1}",
    "{\"value\":1e}",
    "{\"value\":1e+}",
    "{\"value\":0x1}",
    "{\"value\":NaN}",
    "{\"value\":Infinity}",
    "{\"value\":-Infinity}",
    "{\"value\":tru}",
    "{\"value\":True}",
    "{\"value\":TRUE}",
    "{\"value\":truee}",
    "{\"value\":falsey}",
    "{\"value\":\"x}",
    "{\"value\":\"x\\\"}",
    "{\"value\":'x'}",
    "{\"value\":\"\\x\"}",
    "{\"value\":\"\\u\"}",
    "{\"value\":\"\\u12\"}",
    "{\"value\":\"\\u12g4\"}",
    "{\"value\":\"\\ud800\"}",
    "{\"value\":\"\\ud800x\"}",
    "{\"value\":\"\\ud800\\u0041\"}",
    "{\"value\":\"\\ud800\\ud800\"}",
    "{\"value\":\"\\udc00\"}",
    "{\"value\":\"\\udc00\\ud800\"}",
    "{\"value\":\"a\x01\"}",
    "{\"value\":\"a\nb\"}",
    "{\"value\":\"a\tb\"}",
    "{\"value\":\"a\x1f\"}",
    "\xef\xbb\xbf{\"value\":1}",
    "{\"value\":1}\x01",
    "{\xe2\x80\x9cvalue\xe2\x80\x9d:1}",
};

static void test_reader_rejects(void)
{
    for (size_t index = 0; index < sizeof(rejected) / sizeof(rejected[0]); ++index) {
        char text[64];
        web_value_t value = {.text = text, .size = sizeof(text)};
        if (web_mgmt_parse_update((const uint8_t *)rejected[index], strlen(rejected[index]),
                                  &value)) {
            printf("  accepted: %s\n", rejected[index]);
            CHECK(!"rejected");
        }
    }
    /* A NUL after the object is text after it, and a NUL in a key is another key. */
    {
        static const uint8_t after[] = {'{', '"', 'v', 'a', 'l', 'u', 'e', '"', ':', '1', '}', 0};
        static const uint8_t key[] = {'{', '"', 'v', 'a', 'l', 'u', 'e', 0, '"', ':', '1', '}'};
        web_value_t value = {0};

        CHECK(!web_mgmt_parse_update(after, sizeof(after), &value));
        CHECK(!web_mgmt_parse_update(key, sizeof(key), &value));
        CHECK(!web_mgmt_parse_update(NULL, 0u, &value));
    }
}

static void test_reader_bounds_a_long_string_and_counts_it(void)
{
    static uint8_t body[8192];
    char text[8];
    web_value_t value = {.text = text, .size = sizeof(text)};
    size_t used = (size_t)snprintf((char *)body, sizeof(body), "{\"value\":\"");

    memset(body + used, 'x', 5000u);
    used += 5000u;
    body[used++] = '"';
    body[used++] = '}';
    CHECK(web_mgmt_parse_update(body, used, &value));
    CHECK(value.length == 5000u);
    CHECK(strlen(text) == sizeof(text) - 1u);
    /* No text buffer at all: the length is still counted, and nothing is written. */
    value = (web_value_t){0};
    CHECK(web_mgmt_parse_update(body, used, &value));
    CHECK(value.length == 5000u);
    /* A very long number is read without a buffer, and is not a u32. */
    used = (size_t)snprintf((char *)body, sizeof(body), "{\"value\":");
    memset(body + used, '9', 6000u);
    used += 6000u;
    body[used++] = '}';
    value = (web_value_t){.text = text, .size = sizeof(text)};
    CHECK(web_mgmt_parse_update(body, used, &value));
    CHECK(value.kind == WEB_VALUE_NUMBER && !value.number_ok);
}

static void test_the_writer_never_overruns(void)
{
    char data[16];
    web_mgmt_writer_t writer;

    web_mgmt_writer_init(&writer, data, sizeof(data));
    web_mgmt_write(&writer, "0123456789abcde");
    CHECK(!writer.overflow && writer.used == 15u && strlen(data) == 15u);
    web_mgmt_write(&writer, "x");
    CHECK(writer.overflow && strlen(data) == 15u);
    web_mgmt_write(&writer, "");
    CHECK(writer.overflow);
    web_mgmt_writer_init(&writer, data, sizeof(data));
    web_mgmt_write_string(&writer, "a\"b\\c\x01", 6u);
    CHECK(!writer.overflow && strcmp(data, "\"a\\\"b\\\\c\\u0001\"") == 0);
    web_mgmt_writer_init(&writer, data, sizeof(data));
    web_mgmt_write_string(&writer, "aaaaaaaaaaaaaa", 14u);
    CHECK(writer.overflow && strlen(data) < sizeof(data));
    web_mgmt_writer_init(&writer, data, 0u);
    CHECK(writer.overflow);
    web_mgmt_writer_init(&writer, data, sizeof(data));
    web_mgmt_write_u32(&writer, 4294967295u);
    CHECK(strcmp(data, "4294967295") == 0);
}

/* What must hold of every input. */
static void check_reader_result(const uint8_t *body, size_t length)
{
    char text[24];
    web_value_t value = {.text = text, .size = sizeof(text)};
    bool ok;

    memset(text, 0x55, sizeof(text));
    ok = web_mgmt_parse_update(body, length, &value);
    /* The text buffer is a string whether or not the body was accepted, and never overrun. */
    CHECK(memchr(text, '\0', sizeof(text)) != NULL);
    if (!ok) {
        return;
    }
    CHECK(value.kind == WEB_VALUE_BOOL || value.kind == WEB_VALUE_NUMBER ||
          value.kind == WEB_VALUE_STRING);
    if (value.kind == WEB_VALUE_NUMBER && value.number_ok) {
        char again[48];
        web_value_t other = {0};

        snprintf(again, sizeof(again), "{\"value\":%" PRIu32 "}", value.number);
        CHECK(web_mgmt_parse_update((const uint8_t *)again, strlen(again), &other));
        CHECK(other.kind == WEB_VALUE_NUMBER && other.number_ok && other.number == value.number);
    }
    if (value.kind == WEB_VALUE_STRING) {
        CHECK(value.length >= strlen(text));
        CHECK(value.length >= sizeof(text) - 1u || strlen(text) <= value.length);
    }
    /* An accepted body that ends in the object's closing brace has no complete object once that
     * brace is cut off. Trailing space may be cut, and the rest still parses. */
    if (length > 0u && body[length - 1u] == '}') {
        web_value_t cut = {0};

        CHECK(!web_mgmt_parse_update(body, length - 1u, &cut));
    }
}

#define ALPHABET "{}[]\":,\\/ntrufalsenb0123456789-+.eE\t\r\n xu\xc3\xa9\xff"

static void test_reader_survives_random_bytes(void)
{
    uint8_t body[160];

    random_state = UINT64_C(0x9e3779b97f4a7c15);
    for (unsigned round = 0; round < 40000u; ++round) {
        size_t length = below(sizeof(body) + 1u);

        for (size_t index = 0; index < length; ++index) {
            body[index] = below(4u) == 0u ? (uint8_t)next_random()
                                          : (uint8_t)ALPHABET[below(sizeof(ALPHABET) - 1u)];
        }
        check_reader_result(body, length);
    }
}

static void test_reader_survives_mutations_of_valid_bodies(void)
{
    uint8_t body[256];

    random_state = UINT64_C(0xd1b54a32d192ed03);
    for (size_t seed = 0; seed < sizeof(accepted) / sizeof(accepted[0]); ++seed) {
        for (unsigned round = 0; round < 1500u; ++round) {
            size_t length = strlen(accepted[seed].body);

            memcpy(body, accepted[seed].body, length);
            for (unsigned edits = 1u + (unsigned)below(4u); edits > 0u; --edits) {
                size_t at = length == 0u ? 0u : below(length);

                switch (below(6u)) {
                case 0: /* Flip a bit. */
                    body[at] = (uint8_t)(body[at] ^ (1u << below(8u)));
                    break;
                case 1: /* Replace a byte. */
                    body[at] = below(2u) == 0u ? (uint8_t)next_random()
                                               : (uint8_t)ALPHABET[below(sizeof(ALPHABET) - 1u)];
                    break;
                case 2: /* Delete a byte. */
                    if (length > 0u) {
                        memmove(body + at, body + at + 1u, length - at - 1u);
                        --length;
                    }
                    break;
                case 3: /* Insert a byte. */
                    if (length < sizeof(body)) {
                        memmove(body + at + 1u, body + at, length - at);
                        body[at] = (uint8_t)ALPHABET[below(sizeof(ALPHABET) - 1u)];
                        ++length;
                    }
                    break;
                case 4: /* Truncate. */
                    length = at;
                    break;
                default: /* Repeat a slice. */
                    if (length > 1u && length * 2u < sizeof(body)) {
                        size_t part = 1u + below(length);

                        memcpy(body + length, body, part);
                        length += part;
                    }
                    break;
                }
            }
            check_reader_result(body, length);
        }
    }
}

/* The routes. */

typedef struct {
    bool mode;
    uint32_t limit;
    char host[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    char small[8];
    char secret[CRUX_CONFIG_SERVICE_TEXT_SIZE];
} values_t;

static void snapshot(values_t *values)
{
    crux_config_value_t value;

    CHECK(crux_config_service_try_read(NAME_MODE, &value, NULL, 0) == CRUX_CONFIG_SERVICE_OK);
    values->mode = value.data.boolean;
    CHECK(crux_config_service_try_read(NAME_LIMIT, &value, NULL, 0) == CRUX_CONFIG_SERVICE_OK);
    values->limit = value.data.u32;
    CHECK(crux_config_service_try_read(NAME_HOST, &value, values->host, sizeof(values->host)) ==
          CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_try_read(NAME_SMALL, &value, values->small, sizeof(values->small)) ==
          CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_config_service_try_read(NAME_SECRET, &value, values->secret,
                                       sizeof(values->secret)) == CRUX_CONFIG_SERVICE_OK);
}

static bool same(const values_t *first, const values_t *second)
{
    return first->mode == second->mode && first->limit == second->limit &&
           strcmp(first->host, second->host) == 0 && strcmp(first->small, second->small) == 0 &&
           strcmp(first->secret, second->secret) == 0;
}

static const char *const seeds[] = {
    "{\"value\":true}",
    "{\"value\":12}",
    "{\"value\":\"Pump 1\"}",
    "{\"value\":\"\"}",
    "{\"value\":\"sec\\u00e9ret\"}",
    "{\"value\":4294967295}",
    "{\"value\":\"a bad one\"}",
};
static const char *const names[] = {"mode", "limit", "host_name", "small", "link_secret"};

static void mutate_into(char *out, size_t capacity, const char *seed)
{
    size_t length = strlen(seed);

    memcpy(out, seed, length);
    for (unsigned edits = 1u + (unsigned)below(3u); edits > 0u; --edits) {
        size_t at = length == 0u ? 0u : below(length);

        switch (below(4u)) {
        case 0:
            out[at] = (char)((uint8_t)out[at] ^ (1u << below(8u)));
            break;
        case 1:
            out[at] = ALPHABET[below(sizeof(ALPHABET) - 1u)];
            break;
        case 2:
            if (length > 0u) {
                memmove(out + at, out + at + 1u, length - at - 1u);
                --length;
            }
            break;
        default:
            if (length + 1u < capacity) {
                memmove(out + at + 1u, out + at, length - at);
                out[at] = ALPHABET[below(sizeof(ALPHABET) - 1u)];
                ++length;
            }
            break;
        }
    }
    /* A NUL would end the text before the length is known, so the generator avoids it. */
    for (size_t index = 0; index < length; ++index) {
        if (out[index] == '\0') {
            out[index] = ' ';
        }
    }
    out[length] = '\0';
}

static bool is_item(const char *body)
{
    return strncmp(body, "{\"name\":\"", 9u) == 0 && strstr(body, "\"unsaved\":true}") != NULL &&
           strstr(body, "\"value\"") == NULL;
}

static bool is_error_form(const char *body)
{
    static const char *const forms[] = {
        "{\"code\":\"bad_request\"}",
        "{\"code\":\"not_found\"}",
        "{\"code\":\"permission_denied\"}",
        "{\"code\":\"payload_too_large\"}",
        "{\"code\":\"busy\"}",
        "{\"code\":\"not_ready\"}",
        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"value\",\"code\":\"wrong_type\"}]}",
        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"value\",\"code\":\"out_of_range\"}]}",
        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"value\",\"code\":\"rule_not_met\"}]}",
    };

    for (size_t index = 0; index < sizeof(forms) / sizeof(forms[0]); ++index) {
        if (strcmp(body, forms[index]) == 0) {
            return true;
        }
    }
    return false;
}

static void test_updates_survive_mutated_bodies(void)
{
    const session_jar_t *jar;
    values_t before;
    values_t after;
    char body[CRUX_WEB_BODY_BUFFER];
    char url[64];
    unsigned accepted_count = 0u;
    unsigned refused_count = 0u;

    mgmt_setup();
    jar = mgmt_jar("both");
    random_state = UINT64_C(0xa0761d6478bd642f);
    for (unsigned round = 0; round < 4000u; ++round) {
        const fake_reply_t *reply;
        size_t unsaved;

        mutate_into(body, sizeof(body), seeds[below(sizeof(seeds) / sizeof(seeds[0]))]);
        snprintf(url, sizeof(url), LIST "/%s", names[below(sizeof(names) / sizeof(names[0]))]);
        snapshot(&before);
        unsaved = mgmt_unsaved();
        reply = mgmt_put(jar, url, body);
        snapshot(&after);
        /* Every answer is the core's and is a status that the specification declares for this. */
        CHECK(reply->origin == FAKE_FROM_CORE);
        CHECK(reply->status == 200 || reply->status == 400 || reply->status == 403 ||
              reply->status == 404);
        CHECK(reply->closed);
        if (reply->status == 200) {
            ++accepted_count;
            CHECK(is_item(reply->body));
        } else {
            ++refused_count;
            if (!is_error_form(reply->body)) {
                printf("  %s %s -> %d %s\n", url, body, reply->status, reply->body);
            }
            CHECK(is_error_form(reply->body));
            /* A refusal changes nothing: no value, and no mark. */
            CHECK(same(&before, &after));
            CHECK(mgmt_unsaved() == unsaved);
        }
    }
    /* The generator must reach both sides, or it shows nothing. */
    CHECK(accepted_count > 100u);
    CHECK(refused_count > 1000u);
}

static void test_requests_survive_random_names(void)
{
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.~%/?#&=+ {}\"\\\xc3\xa9";
    const session_jar_t *jar;
    char url[256];

    mgmt_setup();
    jar = mgmt_jar("both");
    random_state = UINT64_C(0xe7037ed1a0b428db);
    for (unsigned round = 0; round < 6000u; ++round) {
        size_t length = below(round % 50u == 0u ? 220u : 40u);
        size_t used = (size_t)snprintf(url, sizeof(url), LIST "/");
        const fake_reply_t *reply;

        for (size_t index = 0; index < length && used + 1u < sizeof(url); ++index) {
            url[used++] = alphabet[below(sizeof(alphabet) - 1u)];
        }
        url[used] = '\0';
        reply = below(2u) == 0u ? mgmt_get(jar, url) : mgmt_put(jar, url, "{\"value\":true}");
        /* The core's own answers, or the server's plain 404, 405 and 500 for what is not a request.
         */
        CHECK(reply->status == 200 || reply->status == 400 || reply->status == 403 ||
              reply->status == 404 || reply->status == 405 || reply->status == 500);
        if (reply->origin == FAKE_FROM_CORE) {
            CHECK(reply->status != 500 && reply->status != 405);
            CHECK(reply->status == 200 || is_error_form(reply->body));
        }
    }
}

static void test_oversized_bodies_are_refused_before_anything_is_read(void)
{
    static char body[9000];
    const session_jar_t *jar;
    values_t before;
    values_t after;

    mgmt_setup();
    jar = mgmt_jar("both");
    memset(body, ' ', sizeof(body) - 1u);
    memcpy(body, "{\"value\":true}", 14u);
    snapshot(&before);
    for (size_t length = CRUX_WEB_BODY_BUFFER + 1u; length < sizeof(body) - 1u;
         length += length < 1200u ? 1u : 331u) {
        const fake_reply_t *reply;

        body[length] = '\0';
        reply = mgmt_put(jar, LIST "/mode", body);
        body[length] = ' ';
        mgmt_expect_error(reply, 413, "payload_too_large");
        CHECK(reply->closed);
    }
    snapshot(&after);
    CHECK(same(&before, &after));
    CHECK(mgmt_unsaved() == 0u);
}

int main(void)
{
    mgmt_run_case("reader accepts", test_reader_accepts);
    mgmt_run_case("reader rejects", test_reader_rejects);
    mgmt_run_case("reader bounds a long string", test_reader_bounds_a_long_string_and_counts_it);
    mgmt_run_case("writer never overruns", test_the_writer_never_overruns);
    mgmt_run_case("reader: random bytes", test_reader_survives_random_bytes);
    mgmt_run_case("reader: mutated bodies", test_reader_survives_mutations_of_valid_bodies);
    mgmt_run_case("updates: mutated bodies", test_updates_survive_mutated_bodies);
    mgmt_run_case("requests: random names", test_requests_survive_random_names);
    mgmt_run_case("oversized bodies", test_oversized_bodies_are_refused_before_anything_is_read);
    return mgmt_finish("web_management_fuzz");
}
