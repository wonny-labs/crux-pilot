/* The bounded JSON reader of the session routes. */
#include "session_test.h"

static char user[16];
static char password[16];

static bool parse(const char *body)
{
    web_json_field_t fields[2] = {{"user", user, sizeof(user), 0u, false, false},
                                  {"password", password, sizeof(password), 0u, false, false}};
    bool parsed = web_json_parse_strings((const uint8_t *)body, strlen(body), fields, 2u);

    return parsed;
}

static void test_accepts_the_form(void)
{
    CHECK(parse("{\"user\":\"admin\",\"password\":\"secret\"}"));
    CHECK(strcmp(user, "admin") == 0 && strcmp(password, "secret") == 0);
    CHECK(parse(" \r\n\t{ \"password\" : \"p\" ,\n \"user\" : \"u\" } \n"));
    CHECK(strcmp(user, "u") == 0 && strcmp(password, "p") == 0);
    CHECK(parse("{\"user\":\"\",\"password\":\"\"}"));
    CHECK(user[0] == '\0' && password[0] == '\0');
    /* A member name may itself be escaped. */
    CHECK(parse("{\"\\u0075ser\":\"a\",\"password\":\"b\"}"));
}

static void test_escapes(void)
{
    web_json_field_t fields[2] = {{"user", user, sizeof(user), 0u, false, false},
                                  {"password", password, sizeof(password), 0u, false, false}};
    static const char body[] = "{\"user\":\"\\\"\\\\\\/\\b\\f\\n\\r\\t\",\"password\":\"\\u00e9\\ud83d\\ude00\"}";

    CHECK(web_json_parse_strings((const uint8_t *)body, strlen(body), fields, 2u));
    CHECK(strcmp(user, "\"\\/\b\f\n\r\t") == 0);
    CHECK(fields[0].length == 8u);
    CHECK(strcmp(password, "\xc3\xa9\xf0\x9f\x98\x80") == 0);
    CHECK(fields[1].length == 6u && !fields[1].holds_nul);
}

static void test_nul_is_reported(void)
{
    web_json_field_t fields[2] = {{"user", user, sizeof(user), 0u, false, false},
                                  {"password", password, sizeof(password), 0u, false, false}};
    static const char body[] = "{\"user\":\"ad\\u0000min\",\"password\":\"x\"}";

    CHECK(web_json_parse_strings((const uint8_t *)body, strlen(body), fields, 2u));
    CHECK(fields[0].holds_nul && fields[0].length == 6u && !fields[1].holds_nul);
}

static void test_overflow_counts_and_cuts(void)
{
    web_json_field_t fields[2] = {{"user", user, sizeof(user), 0u, false, false},
                                  {"password", password, sizeof(password), 0u, false, false}};
    static const char body[] = "{\"user\":\"abcdefghijklmnopqrstuvwxyz\",\"password\":\"x\"}";

    CHECK(web_json_parse_strings((const uint8_t *)body, strlen(body), fields, 2u));
    CHECK(fields[0].length == 26u);
    CHECK(strlen(user) == sizeof(user) - 1u && strncmp(user, "abcdefghijklmno", 15) == 0);
}

static void test_rejects_malformed(void)
{
    static const char *const bodies[] = {
        "",
        "   ",
        "[]",
        "\"user\"",
        "{}",
        "{\"user\":\"a\"}",
        "{\"password\":\"a\"}",
        "{\"user\":\"a\",\"password\":\"b\",\"role\":\"c\"}",
        "{\"user\":\"a\",\"user\":\"b\",\"password\":\"c\"}",
        "{\"user\":1,\"password\":\"b\"}",
        "{\"user\":null,\"password\":\"b\"}",
        "{\"user\":true,\"password\":\"b\"}",
        "{\"user\":{},\"password\":\"b\"}",
        "{\"user\":[\"a\"],\"password\":\"b\"}",
        "{\"user\":\"a\",\"password\":\"b\",}",
        "{\"user\":\"a\" \"password\":\"b\"}",
        "{\"user\" \"a\",\"password\":\"b\"}",
        "{\"user\":\"a\",\"password\":\"b\"}x",
        "{\"user\":\"a\",\"password\":\"b\"}{}",
        "{\"user\":\"a\",\"password\":\"b\"",
        "{\"user\":\"a,\"password\":\"b\"}",
        "{\"user\":\"\\x\",\"password\":\"b\"}",
        "{\"user\":\"\\u12\",\"password\":\"b\"}",
        "{\"user\":\"\\u12g4\",\"password\":\"b\"}",
        "{\"user\":\"\\ud800\",\"password\":\"b\"}",
        "{\"user\":\"\\udc00\",\"password\":\"b\"}",
        "{\"user\":\"\\ud800\\u0041\",\"password\":\"b\"}",
        "{\"user\":\"a\tb\",\"password\":\"b\"}",
        "{\"user\":\"a\nb\",\"password\":\"b\"}",
        "{'user':'a','password':'b'}",
        "{\"an extremely long member name that exceeds the key\":\"a\",\"password\":\"b\"}",
    };

    for (size_t index = 0; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
        if (parse(bodies[index])) {
            printf("accepted: %s\n", bodies[index]);
            CHECK(false);
        }
    }
    /* A NUL byte in the body is a raw control character. */
    {
        web_json_field_t fields[2] = {{"user", user, sizeof(user), 0u, false, false},
                                      {"password", password, sizeof(password), 0u, false, false}};
        static const uint8_t body[] = "{\"user\":\"a\0b\",\"password\":\"c\"}";

        CHECK(!web_json_parse_strings(body, sizeof(body) - 1u, fields, 2u));
    }
}

int main(void)
{
    test_accepts_the_form();
    test_escapes();
    test_nul_is_reported();
    test_overflow_counts_and_cuts();
    test_rejects_malformed();
    return session_test_finish("web_session_json");
}
