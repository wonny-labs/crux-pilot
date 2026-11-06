/* The reason of a refusal is text from the application's callback, and it reaches a client through
 * the 423 and the state. The service shows it only when it is one to 63 bytes of well-formed UTF-8
 * with no control character and ends inside the buffer, and otherwise shows the fixed default. This
 * suite holds that to named vectors and to a deterministic fuzz against an oracle written apart
 * from crux_config_text_valid(), which the service uses. */
#include "reboot_test.h"

#define SIZE CRUX_REBOOT_REASON_SIZE

/* The oracle: decode code points and judge each by its range. */
static bool oracle_valid(const unsigned char *text, size_t length)
{
    for (size_t index = 0; index < length;) {
        unsigned char lead = text[index];
        uint32_t code;
        size_t extra;

        if (lead < 0x80u) {
            if (lead < 0x20u || lead == 0x7Fu) {
                return false;
            }
            ++index;
            continue;
        }
        if (lead >= 0xC2u && lead <= 0xDFu) {
            code = lead & 0x1Fu;
            extra = 1u;
        } else if (lead >= 0xE0u && lead <= 0xEFu) {
            code = lead & 0x0Fu;
            extra = 2u;
        } else if (lead >= 0xF0u && lead <= 0xF4u) {
            code = lead & 0x07u;
            extra = 3u;
        } else {
            return false;
        }
        if (index + extra >= length) {
            return false;
        }
        for (size_t step = 1; step <= extra; ++step) {
            if ((text[index + step] & 0xC0u) != 0x80u) {
                return false;
            }
            code = (code << 6) | (text[index + step] & 0x3Fu);
        }
        if ((extra == 2u && code < 0x800u) || (extra == 3u && code < 0x10000u) ||
            code > 0x10FFFFu || (code >= 0xD800u && code <= 0xDFFFu) ||
            (code >= 0x80u && code <= 0x9Fu)) {
            return false;
        }
        index += extra + 1u;
    }
    return true;
}

/* What the service must show for these 64 bytes. */
static void oracle(const unsigned char bytes[SIZE], char expected[SIZE])
{
    size_t length = 0;

    while (length < SIZE && bytes[length] != 0u) {
        ++length;
    }
    memset(expected, 0, SIZE);
    if (length == SIZE || length == 0u || !oracle_valid(bytes, length)) {
        memcpy(expected, CRUX_REBOOT_DEFAULT_REASON, sizeof(CRUX_REBOOT_DEFAULT_REASON));
        return;
    }
    memcpy(expected, bytes, length);
}

/* Refuses with these bytes and returns what the request and the state showed. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two outputs, named for what they hold
static void shown(const unsigned char bytes[SIZE], size_t given, char from_request[SIZE],
                  char from_state[SIZE])
{
    crux_reboot_state_t state;

    reboot_policy_always(false);
    reboot_policy_reason(bytes, given);
    memset(from_request, 0xA5, SIZE);
    CHECK(crux_reboot_request(s_rebooter, from_request) == CRUX_REBOOT_ERR_REFUSED);
    state = reboot_state_of(s_admin);
    CHECK(state.state == CRUX_REBOOT_STATE_REFUSED);
    memcpy(from_state, state.reason, SIZE);
}

static void expect_text(const char *name, const void *bytes, size_t length, const char *expected)
{
    unsigned char padded[SIZE] = {0};
    char from_request[SIZE], from_state[SIZE];

    memcpy(padded, bytes, length);
    shown(padded, length, from_request, from_state);
    if (strcmp(from_request, expected) != 0 || strcmp(from_state, expected) != 0) {
        printf("  %s: expected \"%s\", got \"%s\" and \"%s\"\n", name, expected, from_request,
               from_state);
    }
    CHECK(strcmp(from_request, expected) == 0);
    CHECK(strcmp(from_state, expected) == 0);
}

static void named_vectors(void)
{
    static const char def[] = CRUX_REBOOT_DEFAULT_REASON;
    char sixty_three[64], sixty_four[64], straddle[64];

    reboot_start();
    memset(sixty_three, 'a', 63);
    sixty_three[63] = '\0';
    expect_text("plain", "Machine cycle running", 22u, "Machine cycle running");
    expect_text("63 bytes", sixty_three, 63u, sixty_three);
    memset(sixty_four, 'b', 64);
    expect_text("64 bytes and no terminator", sixty_four, 64u, def);
    memset(straddle, 'c', 61);
    memcpy(straddle + 61, "\xC3\xA9", 2u); /* 63 bytes, the last one a pair */
    straddle[63] = '\0';
    expect_text("a pair that ends at 63", straddle, 64u, straddle);
    memset(straddle, 'c', 62);
    memcpy(straddle + 62, "\xC3\xA9", 2u); /* 64 bytes: no room for the terminator */
    expect_text("a pair that fills 64", straddle, 64u, def);
    expect_text("two-byte", "Caf\xC3\xA9 open", 11u, "Caf\xC3\xA9 open");
    expect_text("three-byte", "\xE2\x82\xAC ok", 6u, "\xE2\x82\xAC ok");
    expect_text("four-byte", "go \xF0\x9F\x98\x80", 8u, "go \xF0\x9F\x98\x80");
    expect_text("no-break space",
                "a\xC2\xA0"
                "b",
                4u,
                "a\xC2\xA0"
                "b");
    expect_text("empty", "", 0u, def);
    expect_text("a control character", "bad\x01text", 8u, def);
    expect_text("a tab", "bad\ttext", 8u, def);
    expect_text("a line feed", "bad\ntext", 8u, def);
    expect_text("an escape", "bad\x1B[0m", 7u, def);
    expect_text("delete", "bad\x7Ftext", 8u, def);
    expect_text("a C1 control", "bad\xC2\x85text", 9u, def);
    expect_text("a lone continuation", "bad\x80text", 8u, def);
    expect_text("an overlong two-byte", "bad\xC0\x80text", 9u, def);
    expect_text("an overlong three-byte", "bad\xE0\x80\x80text", 10u, def);
    expect_text("a bad lead", "bad\xC1\xBFtext", 9u, def);
    expect_text("a cut three-byte at the end", "bad\xE2\x82", 5u, def);
    expect_text("a surrogate", "bad\xED\xA0\x80", 6u, def);
    expect_text("above U+10FFFF", "bad\xF4\x90\x80\x80", 7u, def);
    expect_text("the highest lead", "bad\xFF", 4u, def);
    /* The text ends at the first terminator, and what follows it is not shown. */
    expect_text("after the terminator", "ok\0\x01\xFF junk", 11u, "ok");
    /* A refusal with no text at all, as a callback that writes nothing. */
    expect_text("nothing written", "", 0u, def);
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t next_random(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* A valid sequence of one random code point, and sometimes a mutated one. */
static size_t random_piece(unsigned char *out)
{
    static const uint32_t points[] = {0x41u,    0x7Eu,    0xA0u,    0xE9u,   0x7FFu,
                                      0x800u,   0x20ACu,  0xD7FFu,  0xE000u, 0xFFFDu,
                                      0x10000u, 0x1F600u, 0x10FFFFu};
    uint32_t code = points[next_random() % (sizeof(points) / sizeof(points[0]))];

    if (code < 0x80u) {
        out[0] = (unsigned char)code;
        return 1u;
    }
    if (code < 0x800u) {
        out[0] = (unsigned char)(0xC0u | (code >> 6));
        out[1] = (unsigned char)(0x80u | (code & 0x3Fu));
        return 2u;
    }
    if (code < 0x10000u) {
        out[0] = (unsigned char)(0xE0u | (code >> 12));
        out[1] = (unsigned char)(0x80u | ((code >> 6) & 0x3Fu));
        out[2] = (unsigned char)(0x80u | (code & 0x3Fu));
        return 3u;
    }
    out[0] = (unsigned char)(0xF0u | (code >> 18));
    out[1] = (unsigned char)(0x80u | ((code >> 12) & 0x3Fu));
    out[2] = (unsigned char)(0x80u | ((code >> 6) & 0x3Fu));
    out[3] = (unsigned char)(0x80u | (code & 0x3Fu));
    return 4u;
}

static void fuzz_against_the_oracle(void)
{
    enum { ROUNDS = 60000 };
    unsigned accepted_texts = 0, refused_texts = 0;

    reboot_start();
    for (unsigned round = 0; round < ROUNDS; ++round) {
        unsigned char bytes[SIZE] = {0};
        char expected[SIZE], from_request[SIZE], from_state[SIZE];
        size_t length = 0;
        unsigned mode = (unsigned)(next_random() % 4u);

        if (mode == 0u) {
            /* Any bytes at all. */
            length = (size_t)(next_random() % (SIZE + 1u));
            for (size_t index = 0; index < length; ++index) {
                bytes[index] = (unsigned char)next_random();
            }
        } else {
            /* Pieces that are mostly valid, so that the accepted side is exercised, then maybe one
             * damaged byte. */
            size_t target = 1u + (size_t)(next_random() % (SIZE - 1u));

            while (length < target) {
                unsigned char piece[4];
                size_t size = random_piece(piece);

                if (length + size > SIZE) {
                    break;
                }
                memcpy(bytes + length, piece, size);
                length += size;
            }
            if (mode == 2u && length > 0u) {
                bytes[next_random() % length] = (unsigned char)next_random();
            } else if (mode == 3u && length > 0u) {
                bytes[next_random() % length] = (unsigned char)(next_random() % 0x20u);
            }
        }
        oracle(bytes, expected);
        shown(bytes, length, from_request, from_state);
        if (strcmp(from_request, expected) != 0 || strcmp(from_state, expected) != 0) {
            printf("  round %u: length %zu: expected \"%s\"\n", round, length, expected);
            CHECK(false);
            return;
        }
        /* Whatever was shown is text that the rule accepts, and fits. */
        CHECK(strlen(from_request) >= 1u && strlen(from_request) < SIZE);
        CHECK(oracle_valid((const unsigned char *)from_request, strlen(from_request)));
        if (strcmp(expected, CRUX_REBOOT_DEFAULT_REASON) == 0) {
            ++refused_texts;
        } else {
            ++accepted_texts;
        }
    }
    /* Both sides were exercised in numbers. */
    CHECK(accepted_texts > 5000u && refused_texts > 5000u);
}

int main(void)
{
    reboot_run_case("named vectors", named_vectors);
    reboot_run_case("fuzz against the oracle", fuzz_against_the_oracle);
    return reboot_finish("reboot_reason");
}
