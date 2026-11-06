/* The checks of the specification, the address text, and the check key: the shared vectors, the
 * rules one by one, deterministic random input, and every way to mix two sets. Pure functions, so
 * this suite needs no service. */
#include "net_test.h"

#include <inttypes.h>

#ifndef NETWORK_VECTORS_PATH
#error "NETWORK_VECTORS_PATH names tests/network_settings/validation-vectors.tsv"
#endif

#define ip(a, b, c, d)                                                                             \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static crux_network_settings_t make(uint32_t address, uint32_t prefix, bool has_gateway,
                                    uint32_t gateway)
{
    crux_network_settings_t settings = {address, prefix, has_gateway, gateway};

    return settings;
}

static const char *field_name(crux_network_field_t field)
{
    switch (field) {
    case CRUX_NETWORK_FIELD_ADDRESS:
        return "address";
    case CRUX_NETWORK_FIELD_PREFIX_LENGTH:
        return "prefix_length";
    default:
        return "gateway";
    }
}

static const char *problem_name(crux_network_problem_t problem)
{
    switch (problem) {
    case CRUX_NETWORK_PROBLEM_NOT_UNICAST:
        return "not_unicast";
    case CRUX_NETWORK_PROBLEM_PREFIX_NOT_USABLE:
        return "prefix_not_usable";
    default:
        return "gateway_outside_subnet";
    }
}

static void describe(const crux_network_findings_t *findings, char *out, size_t size)
{
    size_t used = 0;

    out[0] = '\0';
    if (findings->count == 0u) {
        snprintf(out, size, "ok");
        return;
    }
    for (size_t index = 0; index < findings->count; ++index) {
        used += (size_t)snprintf(out + used, size - used, "%s%s:%s", index > 0u ? "," : "",
                                 field_name(findings->items[index].field),
                                 problem_name(findings->items[index].problem));
    }
}

/* The prefix column: an integer, where a negative or a huge one is not usable. */
static uint32_t prefix_from_text(const char *text)
{
    if (text[0] == '-') {
        return 0u;
    }
    if (strlen(text) > 9u) {
        return UINT32_MAX;
    }
    return (uint32_t)strtoul(text, NULL, 10);
}

static void vectors(void)
{
    FILE *file = fopen(NETWORK_VECTORS_PATH, "r");
    char line[256];
    unsigned rows = 0;

    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[4];
        size_t count = 0;
        char *cursor = line;
        crux_network_settings_t settings;
        crux_network_findings_t findings;
        uint32_t address = 0;
        uint32_t gateway = 0;
        char got[160];
        bool has_gateway;
        bool parsed;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        while (count < 4u) {
            fields[count++] = cursor;
            cursor = strchr(cursor, '\t');
            if (cursor == NULL) {
                break;
            }
            *cursor++ = '\0';
        }
        CHECK(count == 4u);
        if (count != 4u) {
            continue;
        }
        ++rows;
        has_gateway = strcmp(fields[2], "-") != 0;
        parsed = crux_network_settings_parse_ipv4(fields[0], strlen(fields[0]), &address) &&
                 (!has_gateway ||
                  crux_network_settings_parse_ipv4(fields[2], strlen(fields[2]), &gateway));
        if (!parsed) {
            snprintf(got, sizeof(got), "bad_request");
        } else {
            settings = make(address, prefix_from_text(fields[1]), has_gateway, gateway);
            memset(&findings, 0xA5, sizeof(findings));
            bool ok = crux_network_settings_validate(&settings, &findings);
            CHECK(ok == (findings.count == 0u));
            describe(&findings, got, sizeof(got));
        }
        if (strcmp(got, fields[3]) != 0) {
            printf("  vector %s %s %s: expected %s, got %s\n", fields[0], fields[1], fields[2],
                   fields[3], got);
        }
        CHECK(strcmp(got, fields[3]) == 0);
    }
    fclose(file);
    CHECK(rows >= 40u);
}

static void parse_and_format(void)
{
    uint32_t value = 77;
    char text[CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE];

    CHECK(crux_network_settings_parse_ipv4("192.0.2.10", 10, &value) && value == 0xC000020Au);
    CHECK(crux_network_settings_parse_ipv4("0.0.0.0", 7, &value) && value == 0u);
    CHECK(crux_network_settings_parse_ipv4("255.255.255.255", 15, &value) && value == 0xFFFFFFFFu);
    /* The length is the text's: a longer buffer and a terminator count for nothing. */
    CHECK(!crux_network_settings_parse_ipv4("192.0.2.10", 8, &value));
    CHECK(!crux_network_settings_parse_ipv4("192.0.2.1\0"
                                            "0",
                                            11, &value));
    value = 77;
    static const char *const refused[] = {
        "",           "1.2.3",     "1.2.3.4.5", "256.0.0.1", "1.2.3.x",  "01.2.3.4", "1.2.3.4 ",
        " 1.2.3.4",   "1..2.3",    "-1.2.3.4",  "+1.2.3.4",  "1.2.3.4.", ".1.2.3",   "1.2.3.04",
        "1.2.3.1000", "0x1.2.3.4", "1.2.3.4\n", "1,2,3,4",   "١.٢.٣.٤"};
    for (size_t index = 0; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        if (crux_network_settings_parse_ipv4(refused[index], strlen(refused[index]), &value)) {
            printf("  accepted %s\n", refused[index]);
            CHECK(false);
        }
    }
    CHECK(value == 77u);
    CHECK(!crux_network_settings_parse_ipv4(NULL, 0, &value));
    CHECK(!crux_network_settings_parse_ipv4("1.2.3.4", 7, NULL));

    for (uint32_t address = 0; address < 0xFFFFFFF0u; address += 0x01234567u) {
        size_t length;

        memset(text, 'x', sizeof(text));
        crux_network_settings_format_ipv4(address, text);
        length = strlen(text);
        CHECK(length >= 7u && length < CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE);
        CHECK(crux_network_settings_parse_ipv4(text, length, &value) && value == address);
    }
    crux_network_settings_format_ipv4(0xFFFFFFFFu, text);
    CHECK(strcmp(text, "255.255.255.255") == 0);
    crux_network_settings_format_ipv4(0u, text);
    CHECK(strcmp(text, "0.0.0.0") == 0);
}

static void rules_one_by_one(void)
{
    crux_network_findings_t findings;
    crux_network_settings_t settings;

    /* An absent findings pointer is allowed. */
    settings = make(ip(192, 0, 2, 20), 24, false, 0);
    CHECK(crux_network_settings_validate(&settings, NULL));
    settings = make(ip(192, 0, 2, 0), 24, false, 0);
    CHECK(!crux_network_settings_validate(&settings, NULL));
    CHECK(!crux_network_settings_validate(NULL, &findings));

    /* The network and broadcast addresses of every usable prefix length. */
    /* From /3 up, the whole subnet of 192.0.2.0 is of unicast class. Below, the class decides. */
    for (uint32_t prefix = 3; prefix <= 30; ++prefix) {
        uint32_t hosts = UINT32_MAX >> prefix;
        uint32_t base = ip(192, 0, 2, 0) & ~hosts;

        settings = make(base, prefix, false, 0);
        CHECK(!crux_network_settings_validate(&settings, &findings));
        CHECK(findings.count == 1u &&
              findings.items[0].problem == CRUX_NETWORK_PROBLEM_NOT_UNICAST);
        settings = make(base | hosts, prefix, false, 0);
        CHECK(!crux_network_settings_validate(&settings, &findings));
        settings = make(base | 1u, prefix, false, 0);
        CHECK(crux_network_settings_validate(&settings, &findings));
        settings = make(base | (hosts - 1u), prefix, false, 0);
        CHECK(crux_network_settings_validate(&settings, &findings));
    }

    /* A gateway that is the 0.0.0.0 address is not "no gateway". */
    settings = make(ip(192, 0, 2, 20), 24, true, 0);
    CHECK(!crux_network_settings_validate(&settings, &findings));
    CHECK(findings.count == 1u && findings.items[0].field == CRUX_NETWORK_FIELD_GATEWAY);
    settings = make(ip(192, 0, 2, 20), 24, false, 0xDEADBEEFu);
    CHECK(crux_network_settings_validate(&settings, &findings));

    /* The findings come in a fixed order, whatever the values. */
    settings = make(ip(224, 0, 0, 1), 99, true, ip(224, 0, 0, 1));
    CHECK(!crux_network_settings_validate(&settings, &findings));
    CHECK(findings.count == 3u);
    CHECK(findings.items[0].field == CRUX_NETWORK_FIELD_ADDRESS);
    CHECK(findings.items[1].field == CRUX_NETWORK_FIELD_PREFIX_LENGTH);
    CHECK(findings.items[2].field == CRUX_NETWORK_FIELD_GATEWAY);

    /* The edges of the prefix length, including the largest number. */
    for (uint32_t prefix = 0; prefix <= 33; ++prefix) {
        settings = make(ip(192, 0, 2, 21), prefix, false, 0);
        CHECK(crux_network_settings_validate(&settings, NULL) == (prefix >= 1u && prefix <= 30u));
    }
    settings = make(ip(192, 0, 2, 20), UINT32_MAX, false, 0);
    CHECK(!crux_network_settings_validate(&settings, NULL));
    settings = make(ip(192, 0, 2, 20), 256u + 24u, false, 0);
    CHECK(!crux_network_settings_validate(&settings, NULL));
}

/* A small generator with a fixed seed, so a failure repeats. */
static uint64_t random_state;

static uint32_t next_random(void)
{
    random_state = random_state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(random_state >> 32);
}

/* A restatement of the rules by arithmetic on the numbers, to hold the implementation to. */
static bool oracle(const crux_network_settings_t *s, bool *address_bad, bool *prefix_bad,
                   bool *gateway_bad)
{
    bool usable = s->prefix_length >= 1u && s->prefix_length <= 30u;
    uint32_t hosts = usable ? UINT32_MAX >> s->prefix_length : 0u;
    uint32_t first = s->address >> 24;

    *address_bad = first == 0u || first == 127u || first >= 224u ||
                   (usable && ((s->address & hosts) == 0u || (s->address & hosts) == hosts));
    *prefix_bad = !usable;
    *gateway_bad = false;
    if (s->has_gateway) {
        uint32_t gfirst = s->gateway >> 24;

        *gateway_bad = s->gateway == 0u || s->gateway == s->address || gfirst == 0u ||
                       gfirst == 127u || gfirst >= 224u;
        if (usable && !*gateway_bad) {
            *gateway_bad = (s->gateway & ~hosts) != (s->address & ~hosts) ||
                           (s->gateway & hosts) == 0u || (s->gateway & hosts) == hosts;
        }
    }
    return !*address_bad && !*prefix_bad && !*gateway_bad;
}

static void random_against_the_oracle(void)
{
    unsigned accepted = 0;

    random_state = 0x213u;
    for (unsigned round = 0; round < 200000u; ++round) {
        crux_network_settings_t settings;
        crux_network_findings_t findings;
        bool a, p, g;
        uint32_t address = next_random();
        uint32_t prefix = next_random() % 40u;
        uint32_t gateway;

        /* Half the gateways are near the address, so that the subnet rules are exercised. */
        gateway = (next_random() & 1u) ? (address ^ (1u << (next_random() % 32u))) : next_random();
        if ((next_random() & 7u) == 0u) {
            address = ip(192, 0, 2, next_random() % 256u);
        }
        settings = make(address, prefix, (next_random() & 3u) != 0u, gateway);
        bool expected = oracle(&settings, &a, &p, &g);
        bool got = crux_network_settings_validate(&settings, &findings);

        if (got != expected || findings.count != (size_t)(a + p + g)) {
            printf("  round %u: %08" PRIx32 "/%" PRIu32 " gw %08" PRIx32 "\n", round, address,
                   prefix, gateway);
            CHECK(false);
            return;
        }
        accepted += got ? 1u : 0u;
    }
    /* A run in which nothing or everything passes would show nothing. */
    CHECK(accepted > 1000u && accepted < 190000u);
}

static void random_text(void)
{
    static const char alphabet[] = "0123456789.. -+x\n\t";

    random_state = 0x1213u;
    for (unsigned round = 0; round < 100000u; ++round) {
        char text[24];
        size_t length = next_random() % sizeof(text);
        uint32_t value = 0xA5A5A5A5u;

        for (size_t index = 0; index < length; ++index) {
            text[index] = alphabet[next_random() % (sizeof(alphabet) - 1u)];
        }
        if (crux_network_settings_parse_ipv4(text, length, &value)) {
            char again[CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE];

            /* What parses is exactly what the formatter writes: no other spelling is accepted. */
            crux_network_settings_format_ipv4(value, again);
            CHECK(strlen(again) == length && memcmp(again, text, length) == 0);
        } else {
            CHECK(value == 0xA5A5A5A5u);
        }
    }
}

/* The check key must refuse every set that is made of parts of two sets. */
static void check_key_mixes(void)
{
    static const uint32_t addresses[] = {ip(192, 0, 2, 20), ip(192, 0, 2, 21), ip(198, 51, 100, 7),
                                         ip(10, 0, 0, 2), ip(172, 16, 5, 9)};
    static const uint32_t prefixes[] = {8, 24, 25, 30};
    static const uint32_t gateways[] = {0, ip(192, 0, 2, 1), ip(192, 0, 2, 2), ip(10, 0, 0, 1)};
    size_t sets = 0;
    crux_network_settings_t all[5 * 4 * 4];
    unsigned mixes = 0;

    for (size_t a = 0; a < 5; ++a) {
        for (size_t p = 0; p < 4; ++p) {
            for (size_t g = 0; g < 4; ++g) {
                all[sets++] = make(addresses[a], prefixes[p], gateways[g] != 0u, gateways[g]);
            }
        }
    }
    for (size_t x = 0; x < sets; ++x) {
        for (size_t y = 0; y < sets; ++y) {
            for (unsigned pick = 1; pick < 7; ++pick) {
                /* Bits: which of the three members come from y. 0 and 7 are whole sets. */
                crux_network_settings_t mixed = {
                    (pick & 1u) ? all[y].address : all[x].address,
                    (pick & 2u) ? all[y].prefix_length : all[x].prefix_length,
                    (pick & 4u) ? all[y].has_gateway : all[x].has_gateway,
                    (pick & 4u) ? all[y].gateway : all[x].gateway};
                uint32_t key_x = crux_network_settings_check_key(&all[x]);
                uint32_t key_y = crux_network_settings_check_key(&all[y]);
                uint32_t key_m = crux_network_settings_check_key(&mixed);

                /* A mixed set with the old or the new check key is acceptable only if it is one of
                 * the two sets. */
                if (!net_same(&mixed, &all[x]) && !net_same(&mixed, &all[y])) {
                    ++mixes;
                    if (key_m == key_x || key_m == key_y) {
                        printf("  a mix of sets %zu and %zu (%u) passes\n", x, y, pick);
                        CHECK(false);
                        return;
                    }
                }
            }
        }
    }
    CHECK(mixes > 10000u);

    /* Distinct sets have distinct keys here, and the key depends on every member. */
    crux_network_settings_t base = make(ip(192, 0, 2, 20), 24, true, ip(192, 0, 2, 1));
    uint32_t key = crux_network_settings_check_key(&base);
    crux_network_settings_t changed = base;

    changed.address ^= 1u;
    CHECK(crux_network_settings_check_key(&changed) != key);
    changed = base;
    changed.prefix_length = 25;
    CHECK(crux_network_settings_check_key(&changed) != key);
    changed = base;
    changed.gateway ^= 0x100u;
    CHECK(crux_network_settings_check_key(&changed) != key);
    changed = base;
    changed.has_gateway = false;
    changed.gateway = 0;
    CHECK(crux_network_settings_check_key(&changed) != key);
    /* A gateway that is not there does not count, whatever the field holds. */
    changed = base;
    changed.has_gateway = false;
    changed.gateway = 5;
    crux_network_settings_t none = changed;
    none.gateway = 0;
    CHECK(crux_network_settings_check_key(&changed) == crux_network_settings_check_key(&none));
    /* The default of an empty store never passes: all zero is not a key of anything usable. */
    crux_network_settings_t zero = make(0, 0, false, 0);
    CHECK(crux_network_settings_check_key(&zero) != 0u);
}

/* Random sets: a mix of two never passes, over a million tries with a fixed seed. */
static void check_key_random_mixes(void)
{
    unsigned mixes = 0;

    random_state = 0x2130u;
    for (unsigned round = 0; round < 250000u; ++round) {
        crux_network_settings_t x = make(next_random(), next_random() % 31u, true, next_random());
        crux_network_settings_t y = make(next_random(), next_random() % 31u, true, next_random());

        for (unsigned pick = 1; pick < 7; ++pick) {
            crux_network_settings_t mixed = {(pick & 1u) ? y.address : x.address,
                                             (pick & 2u) ? y.prefix_length : x.prefix_length, true,
                                             (pick & 4u) ? y.gateway : x.gateway};
            uint32_t key_m = crux_network_settings_check_key(&mixed);

            if (net_same(&mixed, &x) || net_same(&mixed, &y)) {
                continue;
            }
            ++mixes;
            if (key_m == crux_network_settings_check_key(&x) ||
                key_m == crux_network_settings_check_key(&y)) {
                CHECK(false);
                return;
            }
        }
    }
    CHECK(mixes > 1000000u);
}

int main(void)
{
    net_run_case("the shared vectors", vectors);
    net_run_case("parse and format", parse_and_format);
    net_run_case("the rules one by one", rules_one_by_one);
    net_run_case("random settings against an oracle", random_against_the_oracle);
    net_run_case("random text", random_text);
    net_run_case("check key refuses every mix", check_key_mixes);
    net_run_case("check key refuses random mixes", check_key_random_mixes);
    return net_finish("network_settings_validation");
}
