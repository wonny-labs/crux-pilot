/* The reader of the PUT body against generated and mutated input, then the routes against the same.
 * Deterministic: every stream has a fixed seed, so a failure repeats. For each input it checks what
 * must hold whether the body is accepted or not: the reader never reads outside the body, an
 * accepted body re-encodes to a body that reads the same, a cut or extended body is refused, and
 * through the route nothing changes without a 202 and nothing answers outside the specification. */
#include "network_web.h"

#include <inttypes.h>
#include <stdlib.h>

#define SET_A_FOR_FUZZ                                                                             \
    "{\"address\":\"198.51.100.20\",\"prefix_length\":24,\"gateway\":\"198.51.100.1\"}"

static uint64_t random_state;

static uint32_t next_random(void)
{
    random_state = random_state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(random_state >> 32);
}

static const char *const spaces[] = {"", "", "", " ", "\n", "\t ", "\r\n"};

static const char *space(void)
{
    return spaces[next_random() % (sizeof(spaces) / sizeof(spaces[0]))];
}

/* The value of a character as it may be spelled inside a JSON string: itself, or as an escape. */
static size_t put_char(char *out, char c)
{
    if ((next_random() & 7u) == 0u) {
        return (size_t)sprintf(out, "\\u%04x", (unsigned)(uint8_t)c);
    }
    out[0] = c;
    return 1u;
}

static size_t put_text(char *out, const char *text)
{
    size_t used = 0;

    for (; *text != '\0'; ++text) {
        used += put_char(out + used, *text);
    }
    return used;
}

typedef struct {
    char text[1024]; /* Longer than the route's limit, so that the core's 413 is reached too. */
    size_t length;
    crux_network_settings_t expected;
} generated_t;

/* A valid body, in a random member order, with random white space and escapes. */
static void generate(generated_t *out)
{
    char address[CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE];
    char gateway[CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE];
    char members[3][256];
    size_t count = 0;
    size_t order[3];
    bool has_gateway = (next_random() & 1u) != 0u;
    uint32_t prefix = next_random() % 40u;
    size_t used = 0;
    size_t member;

    out->expected.address = next_random();
    out->expected.prefix_length = prefix;
    out->expected.has_gateway = has_gateway;
    out->expected.gateway = has_gateway ? next_random() : 0u;
    crux_network_settings_format_ipv4(out->expected.address, address);
    crux_network_settings_format_ipv4(out->expected.gateway, gateway);

    member = 0;
    used = (size_t)sprintf(members[member], "\"");
    used += put_text(members[member] + used, "address");
    used += (size_t)sprintf(members[member] + used, "\"%s:%s\"", space(), space());
    used += put_text(members[member] + used, address);
    members[member][used++] = '"';
    members[member][used] = '\0';
    ++member;
    used = (size_t)sprintf(members[member], "\"");
    used += put_text(members[member] + used, "prefix_length");
    used +=
        (size_t)sprintf(members[member] + used, "\"%s:%s%s%" PRIu32, space(), space(), "", prefix);
    members[member][used] = '\0';
    ++member;
    if (has_gateway) {
        used = (size_t)sprintf(members[member], "\"");
        used += put_text(members[member] + used, "gateway");
        used += (size_t)sprintf(members[member] + used, "\"%s:%s\"", space(), space());
        used += put_text(members[member] + used, gateway);
        members[member][used++] = '"';
        members[member][used] = '\0';
        ++member;
    }
    count = member;
    for (size_t index = 0; index < count; ++index) {
        order[index] = index;
    }
    for (size_t index = count; index > 1u; --index) {
        size_t other = next_random() % index;
        size_t swap = order[index - 1u];

        order[index - 1u] = order[other];
        order[other] = swap;
    }
    out->length = (size_t)sprintf(out->text, "%s{%s", space(), space());
    for (size_t index = 0; index < count; ++index) {
        out->length += (size_t)sprintf(out->text + out->length, "%s%s%s%s", index > 0u ? "," : "",
                                       space(), members[order[index]], space());
    }
    out->length += (size_t)sprintf(out->text + out->length, "}%s", space());
}

static bool settings_equal(const crux_network_settings_t *a, const crux_network_settings_t *b)
{
    return a->address == b->address && a->prefix_length == b->prefix_length &&
           a->has_gateway == b->has_gateway && (!a->has_gateway || a->gateway == b->gateway);
}

/* One canonical spelling of settings, to hold the reader to. */
static size_t encode(const crux_network_settings_t *settings, char *out, size_t size)
{
    char address[CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE];
    char gateway[CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE];

    crux_network_settings_format_ipv4(settings->address, address);
    crux_network_settings_format_ipv4(settings->gateway, gateway);
    if (settings->has_gateway) {
        return (size_t)snprintf(
            out, size, "{\"address\":\"%s\",\"prefix_length\":%" PRIu32 ",\"gateway\":\"%s\"}",
            address, settings->prefix_length, gateway);
    }
    return (size_t)snprintf(out, size, "{\"address\":\"%s\",\"prefix_length\":%" PRIu32 "}",
                            address, settings->prefix_length);
}

static void generated_bodies_read_as_generated(void)
{
    for (unsigned round = 0; round < 20000u; ++round) {
        generated_t body;
        crux_network_settings_t read = {0};

        generate(&body);
        if (!web_mgmt_parse_network((const uint8_t *)body.text, body.length, &read) ||
            !settings_equal(&read, &body.expected)) {
            printf("  round %u: %.*s\n", round, (int)body.length, body.text);
            CHECK(false);
            return;
        }
        /* A cut body is never a body: the last brace ends the object. */
        for (size_t cut = 0; cut < body.length; ++cut) {
            crux_network_settings_t untouched = {0xDEADBEEFu, 1, false, 0};
            crux_network_settings_t again = untouched;

            size_t last = cut;

            while (last > 0u && strchr(" \t\r\n", body.text[last - 1u]) != NULL) {
                --last;
            }
            if (last != 0u && body.text[last - 1u] == '}') {
                continue; /* Only white space was cut. */
            }
            if (web_mgmt_parse_network((const uint8_t *)body.text, cut, &again) ||
                !settings_equal(&again, &untouched)) {
                printf("  round %u cut %zu\n", round, cut);
                CHECK(false);
                return;
            }
        }
        /* Text after the object is refused. */
        body.text[body.length] = 'x';
        CHECK(!web_mgmt_parse_network((const uint8_t *)body.text, body.length + 1u, &read));
        body.text[body.length] = '{';
        CHECK(!web_mgmt_parse_network((const uint8_t *)body.text, body.length + 1u, &read));
    }
}

static void mutate(generated_t *body)
{
    unsigned edits = 1u + next_random() % 3u;

    while (edits-- > 0u && body->length > 0u && body->length + 2u < sizeof(body->text)) {
        size_t at = next_random() % body->length;

        switch (next_random() % 5u) {
        case 0:
            body->text[at] = (char)next_random();
            break;
        case 1:
            memmove(body->text + at, body->text + at + 1u, body->length - at - 1u);
            --body->length;
            break;
        case 2:
            memmove(body->text + at + 1u, body->text + at, body->length - at);
            body->text[at] = "\"{}[],:\\-.0e9 u\x01\x80"[next_random() % 15u];
            ++body->length;
            break;
        case 3:
            body->length = at;
            break;
        default:
            body->text[at] = body->text[next_random() % body->length];
            break;
        }
    }
}

static void mutated_bodies_hold_the_rules(void)
{
    unsigned accepted = 0;
    unsigned refused = 0;

    random_state = 0x2131u;
    for (unsigned round = 0; round < 60000u; ++round) {
        generated_t body;
        crux_network_settings_t read = {0};
        crux_network_settings_t sentinel = {0xDEADBEEFu, 7u, true, 0xFEEDu};
        char again[256];
        crux_network_settings_t reread = {0};
        size_t length;

        generate(&body);
        mutate(&body);
        /* Exactly the length of the body, with nothing after it that the reader could use. */
        uint8_t *copy = malloc(body.length != 0u ? body.length : 1u);

        if (copy == NULL) {
            exit(2);
        }
        memcpy(copy, body.text, body.length);
        read = sentinel;
        if (web_mgmt_parse_network(copy, body.length, &read)) {
            ++accepted;
            length = encode(&read, again, sizeof(again));
            if (!web_mgmt_parse_network((const uint8_t *)again, length, &reread) ||
                !settings_equal(&read, &reread)) {
                printf("  round %u: accepted %.*s, which does not re-encode\n", round,
                       (int)body.length, body.text);
                CHECK(false);
            }
        } else {
            ++refused;
            CHECK(settings_equal(&read, &sentinel));
        }
        free(copy);
    }
    /* A run in which the mutation never or always broke the body would show nothing. */
    if (accepted <= 500u || refused <= 2000u) {
        printf("  accepted %u, refused %u\n", accepted, refused);
    }
    CHECK(accepted > 500u && refused > 2000u);
}

static void every_prefix_of_a_body_and_random_bytes(void)
{
    static const char valid[] = SET_A_FOR_FUZZ;
    crux_network_settings_t read;

    for (size_t cut = 0; cut + 1u < sizeof(valid); ++cut) {
        CHECK(!web_mgmt_parse_network((const uint8_t *)valid, cut, &read));
    }
    CHECK(web_mgmt_parse_network((const uint8_t *)valid, sizeof(valid) - 1u, &read));
    random_state = 0x21310u;
    for (unsigned round = 0; round < 40000u; ++round) {
        uint8_t bytes[96];
        size_t length = next_random() % sizeof(bytes);

        for (size_t index = 0; index < length; ++index) {
            bytes[index] = (uint8_t)next_random();
        }
        if (web_mgmt_parse_network(bytes, length, &read)) {
            /* Random bytes cannot be an object of these members. */
            CHECK(false);
        }
    }
    CHECK(!web_mgmt_parse_network(NULL, 0, &read));
    CHECK(!web_mgmt_parse_network((const uint8_t *)valid, sizeof(valid) - 1u, NULL));
}

/* Through the route: nothing changes without a 202, and every answer is one the specification has.
 */
static void the_route_answers_only_what_the_specification_has(void)
{
    const session_jar_t *jar;
    unsigned accepted = 0;
    unsigned refused = 0;

    random_state = 0x21311u;
    web_net_setup();
    jar = web_net_jar("both");
    for (unsigned round = 0; round < 3000u; ++round) {
        generated_t body;
        const fake_reply_t *reply;
        size_t writes;
        bool queued;

        /* The fake store keeps the last 64 writes. */
        config_fault_write_log_clear();
        writes = 0u;
        generate(&body);
        if ((next_random() & 1u) != 0u) {
            mutate(&body);
        }
        body.text[body.length] = '\0';
        for (size_t index = 0; index < body.length; ++index) {
            if (body.text[index] == '\0') {
                body.text[index] = ' '; /* The fake transport takes a C string. */
            }
        }
        reply = web_net_put(jar, body.text);
        CHECK(reply->origin == FAKE_FROM_CORE);
        queued = reply->status == 202;
        if (reply->status == 413) {
            CHECK(strcmp(reply->body, "{\"code\":\"payload_too_large\"}") == 0);
            CHECK(body.length > WEB_MGMT_NETWORK_BODY_MAX);
            CHECK(config_fault_write_log_count() == writes);
            continue;
        }
        CHECK(reply->closed);
        if (queued) {
            CHECK(strcmp(reply->body, "{\"state\":\"pending\"}") == 0);
            ++accepted;
        } else {
            ++refused;
            CHECK(reply->status == 400);
            CHECK(strcmp(reply->body, "{\"code\":\"bad_request\"}") == 0 ||
                  strncmp(reply->body, "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"",
                          43u) == 0);
            /* Nothing was written, whatever the body. */
            CHECK(config_fault_write_log_count() == writes);
        }
        CHECK(config_fault_write_log_count() == writes);
        if (queued) {
            CHECK(crux_network_settings_work_once());
            CHECK(config_fault_write_log_count() == writes + 5u);
            crux_network_settings_t reboot;
            CHECK(net_reboot_decision(&reboot) == CRUX_NETWORK_BOOT_SAVED);
        }
        CHECK(!crux_network_settings_work_once());
    }
    CHECK(accepted > 100u && refused > 100u);
}

int main(void)
{
    random_state = 0x2130u;
    net_run_case("generated bodies read as generated", generated_bodies_read_as_generated);
    net_run_case("mutated bodies hold the rules", mutated_bodies_hold_the_rules);
    net_run_case("prefixes of a body and random bytes", every_prefix_of_a_body_and_random_bytes);
    net_run_case("the route answers only what the specification has",
                 the_route_answers_only_what_the_specification_has);
    return net_finish("web_network_fuzz");
}
