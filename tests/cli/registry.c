/* What crux_cli_init() accepts and refuses, and what a registry that is not ready does. */
#include "cli_session_test.h"

#define BOTH (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)

static unsigned handler_runs;
static void *last_context;
static crux_cli_result_t next_result;

static crux_cli_result_t handler(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                 void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    handler_runs += 1;
    last_context = context;
    return next_result;
}

static crux_cli_result_t init_one(const crux_cli_command_t *table, size_t count)
{
    crux_cli_test_reset();
    /* The registry borrows these for as long as it stays ready. */
    static const crux_cli_command_t *tables[1];
    static size_t counts[1];
    tables[0] = table;
    counts[0] = count;
    return crux_cli_init(tables, counts, 1);
}

static void expect_refused(const crux_cli_command_t *command)
{
    CHECK(init_one(command, 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    /* A failed init leaves the registry not ready. */
    CHECK(!crux_cli_registry_ready());
}

static void expect_accepted(const crux_cli_command_t *command)
{
    CHECK(init_one(command, 1) == CRUX_CLI_OK);
    CHECK(crux_cli_registry_ready());
}

static void entry_validation(void)
{
    const crux_cli_command_t zero = {0};
    expect_refused(&zero);

    const char *const bad_names[] = {NULL,    "",         " ",    "a ",   " a",     "a  b",
                                     "a b c", "A",        "a_b",  "a.b",  "!setup", "a\tb",
                                     "a\nb",  "\xC3\xA9", "a b ", "\"a\""};
    for (size_t index = 0; index < sizeof(bad_names) / sizeof(bad_names[0]); ++index) {
        const crux_cli_command_t command = CRUX_CLI_COMMAND(
            bad_names[index], "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
        expect_refused(&command);
    }
    const char *const good_names[] = {"a", "a-b", "a9", "9", "-", "a b", "a-b c-d"};
    for (size_t index = 0; index < sizeof(good_names) / sizeof(good_names[0]); ++index) {
        const crux_cli_command_t command = CRUX_CLI_COMMAND(
            good_names[index], "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
        expect_accepted(&command);
    }

    /* A summary is printed by help, so it holds nothing a terminal would act on. */
    const char *const bad_summaries[] = {NULL, "a\nb", "a\x1b[2J", "\x7f", "\xC2\x9B"};
    for (size_t index = 0; index < sizeof(bad_summaries) / sizeof(bad_summaries[0]); ++index) {
        const crux_cli_command_t command = CRUX_CLI_COMMAND(
            "cmd", bad_summaries[index], CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
        expect_refused(&command);
    }

    /* Class and permission pairs. There is no class that runs with no session. */
    const crux_cli_command_t invalid_class = {
        "cmd", "s", handler, NULL, NULL, CRUX_CLI_ACCESS_INVALID, 0, 0, BOTH};
    expect_refused(&invalid_class);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const crux_cli_command_t unknown_class = {
        "cmd", "s", handler, NULL, NULL, (crux_cli_access_class_t)3, 0, 0, BOTH};
    expect_refused(&unknown_class);
    const crux_cli_command_t session_with_permission = {
        "cmd", "s", handler, NULL, "status.read", CRUX_CLI_ACCESS_SESSION, 0, 0, BOTH};
    expect_refused(&session_with_permission);
    const crux_cli_command_t permission_null =
        CRUX_CLI_COMMAND("cmd", "s", CRUX_CLI_PERMISSION(NULL), 0, 0, BOTH, handler, NULL);
    expect_refused(&permission_null);
    const crux_cli_command_t permission_empty =
        CRUX_CLI_COMMAND("cmd", "s", CRUX_CLI_PERMISSION(""), 0, 0, BOTH, handler, NULL);
    expect_refused(&permission_empty);
    char too_long[CRUX_ACCESS_PERMISSION_MAX + 2];
    memset(too_long, 'p', sizeof(too_long) - 1u);
    too_long[sizeof(too_long) - 1u] = '\0';
    const crux_cli_command_t permission_long =
        CRUX_CLI_COMMAND("cmd", "s", CRUX_CLI_PERMISSION(too_long), 0, 0, BOTH, handler, NULL);
    expect_refused(&permission_long);
    too_long[sizeof(too_long) - 2u] = '\0';
    const crux_cli_command_t permission_at_bound =
        CRUX_CLI_COMMAND("cmd", "s", CRUX_CLI_PERMISSION(too_long), 0, 0, BOTH, handler, NULL);
    expect_accepted(&permission_at_bound);

    /* Argument bounds. A count the token limit cannot reach is a mistake. */
    const crux_cli_command_t min_over_max =
        CRUX_CLI_COMMAND("cmd", "s", CRUX_CLI_SESSION_ONLY, 2, 1, BOTH, handler, NULL);
    expect_refused(&min_over_max);
    const crux_cli_command_t one_word_over = CRUX_CLI_COMMAND(
        "cmd", "s", CRUX_CLI_SESSION_ONLY, 0, CRUX_CLI_TOKENS_MAX, BOTH, handler, NULL);
    expect_refused(&one_word_over);
    const crux_cli_command_t one_word_at = CRUX_CLI_COMMAND(
        "cmd", "s", CRUX_CLI_SESSION_ONLY, 0, CRUX_CLI_TOKENS_MAX - 1, BOTH, handler, NULL);
    expect_accepted(&one_word_at);
    const crux_cli_command_t two_word_over = CRUX_CLI_COMMAND(
        "cmd x", "s", CRUX_CLI_SESSION_ONLY, 0, CRUX_CLI_TOKENS_MAX - 1, BOTH, handler, NULL);
    expect_refused(&two_word_over);

    /* Flags. */
    const crux_cli_command_t no_channel = CRUX_CLI_COMMAND(
        "cmd", "s", CRUX_CLI_SESSION_ONLY, 0, 0, CRUX_CLI_FLAG_FORCED_CHANGE_OK, handler, NULL);
    expect_refused(&no_channel);
    const crux_cli_command_t unknown_bit =
        CRUX_CLI_COMMAND("cmd", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH | (1u << 7), handler, NULL);
    expect_refused(&unknown_bit);
    const crux_cli_command_t no_flags =
        CRUX_CLI_COMMAND("cmd", "s", CRUX_CLI_SESSION_ONLY, 0, 0, 0, handler, NULL);
    expect_refused(&no_flags);

    const crux_cli_command_t no_handler =
        CRUX_CLI_COMMAND("cmd", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, NULL, NULL);
    expect_refused(&no_handler);
}

static void table_validation(void)
{
    static const crux_cli_command_t one =
        CRUX_CLI_COMMAND("cmd", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
    static const crux_cli_command_t same = CRUX_CLI_COMMAND("cmd", "t", CRUX_CLI_SESSION_ONLY, 0, 0,
                                                            CRUX_CLI_CHANNEL_SSH, handler, NULL);
    static const crux_cli_command_t two_word =
        CRUX_CLI_COMMAND("cmd sub", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
    static const crux_cli_command_t other_word =
        CRUX_CLI_COMMAND("cmd other", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
    static const crux_cli_command_t builtin =
        CRUX_CLI_COMMAND("help", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
    static const crux_cli_command_t builtin_prefix =
        CRUX_CLI_COMMAND("account", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
    static const crux_cli_command_t builtin_sibling =
        CRUX_CLI_COMMAND("account show", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
    static const crux_cli_command_t help_sub =
        CRUX_CLI_COMMAND("help sub", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);

    /* Duplicates and overlaps, within one table and across two, in both orders. */
    const crux_cli_command_t duplicate[] = {one, same};
    CHECK(init_one(duplicate, 2) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    const crux_cli_command_t overlap[] = {one, two_word};
    CHECK(init_one(overlap, 2) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    const crux_cli_command_t overlap_reversed[] = {two_word, one};
    CHECK(init_one(overlap_reversed, 2) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    const crux_cli_command_t siblings[] = {two_word, other_word};
    CHECK(init_one(siblings, 2) == CRUX_CLI_OK);

    static const crux_cli_command_t *pair[2];
    static size_t pair_counts[2] = {1, 1};
    pair[0] = &one;
    pair[1] = &same;
    crux_cli_test_reset();
    CHECK(crux_cli_init(pair, pair_counts, 2) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    pair[1] = &two_word;
    crux_cli_test_reset();
    CHECK(crux_cli_init(pair, pair_counts, 2) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    pair[0] = &two_word;
    pair[1] = &one;
    crux_cli_test_reset();
    CHECK(crux_cli_init(pair, pair_counts, 2) == CRUX_CLI_ERR_INVALID_ARGUMENT);

    /* The built-ins are always there, so a project clashes with them. */
    CHECK(init_one(&builtin, 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(init_one(&builtin_prefix, 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(init_one(&help_sub, 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(init_one(&builtin_sibling, 1) == CRUX_CLI_OK);

    /* Bad table shapes. */
    static size_t count;
    count = 1;
    crux_cli_test_reset();
    CHECK(crux_cli_init(NULL, &count, 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    static const crux_cli_command_t *const one_table[] = {&one};
    CHECK(crux_cli_init(one_table, NULL, 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    static const crux_cli_command_t *const null_table[] = {NULL};
    CHECK(crux_cli_init(null_table, &count, 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    /* A NULL table of no commands is empty, and no tables at all leaves only the built-ins. */
    count = 0;
    CHECK(crux_cli_init(null_table, &count, 1) == CRUX_CLI_OK);
    crux_cli_test_reset();
    CHECK(crux_cli_init(NULL, NULL, 0) == CRUX_CLI_OK);
}

static void lifecycle(void)
{
    static const crux_cli_command_t first =
        CRUX_CLI_COMMAND("first", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
    static const crux_cli_command_t second =
        CRUX_CLI_COMMAND("second", "s", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, handler, NULL);
    int status = -1;

    /* Before any init, nothing runs and nothing is written. */
    cli_test_begin();
    handler_runs = 0;
    CHECK(cli_test_session("first\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_NOT_READY);
    CHECK_TEXT(cli_test_output(), "");
    CHECK(cli_test_exec("first", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_NOT_READY);
    CHECK(status == 1);
    CHECK_TEXT(cli_test_output(), "");
    CHECK(handler_runs == 0);
    CHECK(cli_fake_access.touch_calls == 0);

    /* After a failed init too, until a retry succeeds. */
    const crux_cli_command_t zero = {0};
    CHECK(init_one(&zero, 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(cli_test_exec("first", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_NOT_READY);
    CHECK(init_one(&first, 1) == CRUX_CLI_OK);
    CHECK(cli_test_exec("first", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK(handler_runs == 1);

    /* A repeated init changes nothing. */
    const crux_cli_command_t *const repeated[] = {&second};
    const size_t repeated_counts[] = {1};
    CHECK(crux_cli_init(repeated, repeated_counts, 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    cli_test_output_clear();
    CHECK(cli_test_exec("first", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK(cli_test_exec("second", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 127);
}

/* #213 registers a serial recovery command that saves use_saved as false. This is its shape: a
 * two-word, argument-free, serial-only permission command with its own context, whose handler
 * reports a failed save. */
static void recovery_command_shape(void)
{
    static int recovery_state;
    static const crux_cli_command_t recovery = CRUX_CLI_COMMAND(
        "network recover", "use the composition's default address at the next boot",
        CRUX_CLI_PERMISSION("network.write"), 0, 0, CRUX_CLI_CHANNEL_SERIAL, handler,
        &recovery_state);
    int status = -1;

    cli_test_begin_table(&recovery, 1);
    cli_test_grant("network.write");
    handler_runs = 0;
    next_result = CRUX_CLI_OK;
    CHECK(cli_test_session("network recover\nhelp\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(handler_runs == 1);
    CHECK(last_context == &recovery_state);
    CHECK_TEXT(cli_fake_access.last_permission, "network.write");
    CHECK(strstr(cli_test_output(), "network recover - use the composition") != NULL);

    /* It does not exist for SSH, by any form. */
    cli_test_output_clear();
    handler_runs = 0;
    CHECK(cli_test_exec("network recover", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 127);
    CHECK_TEXT(cli_test_output(), "unknown command\n");
    cli_test_output_clear();
    CHECK(cli_test_exec("help network recover", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 127);
    CHECK_TEXT(cli_test_output(), "unknown command\n");
    CHECK(handler_runs == 0);

    /* It takes a session and a permission, so it cannot run before login or without the right. */
    cli_test_begin_table(&recovery, 1);
    cli_test_output_clear();
    CHECK(cli_test_session("network recover\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> unknown command\n> ");
    CHECK(handler_runs == 0);
    CHECK(cli_fake_access.check_denials == 1);

    /* A forced password change holds it back too. */
    cli_test_begin_table(&recovery, 1);
    cli_test_grant("network.write");
    cli_fake_access.must_change = true;
    CHECK(cli_test_session("network recover\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(handler_runs == 0);

    /* Its failed save reaches the user as one fixed line, and the session goes on. */
    cli_test_begin_table(&recovery, 1);
    cli_test_grant("network.write");
    next_result = CRUX_CLI_ERR_FAILED;
    CHECK(cli_test_session("network recover\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> command failed\n> ");
    next_result = CRUX_CLI_OK;
}

static void password_rule_text(void)
{
    crux_cli_test_reset();
    CHECK(crux_cli_set_password_rule("12 to 128 bytes") == CRUX_CLI_OK);
    CHECK_TEXT(crux_cli_registry_password_rule(), "12 to 128 bytes");
    CHECK(crux_cli_set_password_rule("bad\nrule") == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK_TEXT(crux_cli_registry_password_rule(), "12 to 128 bytes");
    CHECK(crux_cli_set_password_rule(NULL) == CRUX_CLI_OK);
    CHECK(crux_cli_registry_password_rule() == NULL);
}

int main(void)
{
    entry_validation();
    table_validation();
    lifecycle();
    recovery_command_shape();
    password_rule_text();
    return cli_test_finish("cli registry");
}
