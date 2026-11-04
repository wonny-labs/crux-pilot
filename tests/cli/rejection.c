/* Rule L3: a hidden command and an unknown command give the same output and exit status, on both
 * channels and in both session forms. */
#include "cli_session_test.h"

#define BOTH (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)

static unsigned handler_runs;

static crux_cli_result_t counted(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                 void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    (void)context;
    handler_runs += 1;
    return CRUX_CLI_OK;
}

static const crux_cli_command_t table[] = {
    CRUX_CLI_COMMAND("visible", "a command this session holds", CRUX_CLI_PERMISSION("held"), 0, 1,
                     BOTH, counted, NULL),
    CRUX_CLI_COMMAND("session-cmd", "any session", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, counted,
                     NULL),
    CRUX_CLI_COMMAND("hidden", "a command this session lacks", CRUX_CLI_PERMISSION("lacked"), 1, 1,
                     BOTH, counted, NULL),
    CRUX_CLI_COMMAND("wizard", "hidden and reads a prompt", CRUX_CLI_PERMISSION("lacked"), 0, 0,
                     BOTH | CRUX_CLI_FLAG_INTERACTIVE, counted, NULL),
    CRUX_CLI_COMMAND("config get", "visible beside a hidden sibling", CRUX_CLI_PERMISSION("held"),
                     0, 1, BOTH, counted, NULL),
    CRUX_CLI_COMMAND("config save", "hidden beside a visible sibling",
                     CRUX_CLI_PERMISSION("lacked"), 0, 0, BOTH, counted, NULL),
    CRUX_CLI_COMMAND("serial-only", "not on SSH", CRUX_CLI_PERMISSION("held"), 0, 0,
                     CRUX_CLI_CHANNEL_SERIAL, counted, NULL),
    CRUX_CLI_COMMAND("ssh-only", "not on serial", CRUX_CLI_SESSION_ONLY, 0, 0, CRUX_CLI_CHANNEL_SSH,
                     counted, NULL),
};

static const crux_access_channel_t channels[] = {CRUX_ACCESS_CHANNEL_SERIAL,
                                                 CRUX_ACCESS_CHANNEL_SSH};

static void begin(void)
{
    cli_test_begin_table(table, sizeof(table) / sizeof(table[0]));
    cli_test_grant("held");
    handler_runs = 0;
}

/* One line through both forms: it must print exactly this, exit with this status under exec, and
 * run nothing. */
static void expect_line(const char *line, crux_access_channel_t channel, const char *expected,
                        int expected_status)
{
    static char session_output[256];
    int status = -1;
    begin();
    CHECK(cli_test_exec(line, channel, &status) == CRUX_CLI_OK);
    CHECK(status == expected_status);
    CHECK_TEXT(cli_test_output(), expected);
    CHECK(handler_runs == 0);

    begin();
    cli_fake_input(&cli_test_fake, line);
    CHECK(cli_test_session("\n", false, channel) == CRUX_CLI_ERR_OUTPUT);
    snprintf(session_output, sizeof(session_output), "> %s> ", expected);
    CHECK_TEXT(cli_test_output(), session_output);
    CHECK(handler_runs == 0);
}

static void hidden_looks_unknown(void)
{
    const char *const lines[] = {
        "no-such",          /* unknown */
        "no-such x",        /* unknown with an argument */
        "hidden",           /* hidden, missing its required argument */
        "hidden x",         /* hidden, with the right count */
        "hidden x y z",     /* hidden, with too many */
        "wizard",           /* hidden and interactive, which exec would otherwise refuse */
        "wizard x",         /* the same with a wrong count */
        "config save",      /* hidden, beside a visible first word */
        "config save x",    /* the same with an argument */
        "config",           /* only the first word of real commands */
        "config no-such",   /* a second word that does not exist */
        "config no-such x", /* the same with an argument */
        "help hidden",      /* help follows the rule */
        "help wizard",      /* for each form */
        "help config save", /* for a two-word name */
        "help config",      /* for a first word */
        "help config no-such",
        "help no-such",
        "help logout foo", /* a real name with a word after it is not that name */
        "help visible foo",
        "visible-not", /* a longer word */
        "VISIBLE",     /* names are lower case */
        "!setup",      /* login's input means nothing in a session */
        "!setup x",
    };
    for (size_t channel = 0; channel < 2; ++channel) {
        for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); ++index) {
            expect_line(lines[index], channels[channel], "unknown command\n", 127);
        }
    }
    /* A command for the other channel looks the same. */
    expect_line("ssh-only", CRUX_ACCESS_CHANNEL_SERIAL, "unknown command\n", 127);
    expect_line("ssh-only x", CRUX_ACCESS_CHANNEL_SERIAL, "unknown command\n", 127);
    expect_line("help ssh-only", CRUX_ACCESS_CHANNEL_SERIAL, "unknown command\n", 127);
    expect_line("serial-only", CRUX_ACCESS_CHANNEL_SSH, "unknown command\n", 127);
    expect_line("serial-only x", CRUX_ACCESS_CHANNEL_SSH, "unknown command\n", 127);
    expect_line("help serial-only", CRUX_ACCESS_CHANNEL_SSH, "unknown command\n", 127);
}

static void limits_come_before_lookup(void)
{
    /* Too many tokens and a bad form are refused alike for visible, hidden and unknown names. */
    const char *const lines[] = {
        "visible 1 2 3 4 5 6 7 8 9 10",
        "hidden 1 2 3 4 5 6 7 8 9 10",
        "wizard 1 2 3 4 5 6 7 8 9 10",
        "config save 1 2 3 4 5 6 7 8 9",
        "no-such 1 2 3 4 5 6 7 8 9 10",
        "visible \"x",
        "hidden \"x",
        "no-such \"x",
        "visible \xC3\xA9",
        "hidden \xC3\xA9",
        "no-such \xC3\xA9",
    };
    for (size_t channel = 0; channel < 2; ++channel) {
        for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); ++index) {
            expect_line(lines[index], channels[channel], "invalid line\n", 2);
        }
    }
    /* An overlong line too, with no check of what it names. */
    static char line[CRUX_CLI_LINE_MAX + 2];
    const char *const names[] = {"visible ", "hidden ", "no-such "};
    for (size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        size_t length = strlen(names[index]);
        memcpy(line, names[index], length);
        memset(line + length, 'x', CRUX_CLI_LINE_MAX + 1u - length);
        line[CRUX_CLI_LINE_MAX + 1] = '\0';
        expect_line(line, CRUX_ACCESS_CHANNEL_SSH, "line too long\n", 2);
        CHECK(cli_fake_access.check_calls == 0);
    }
}

static void visible_commands_answer(void)
{
    /* The same words succeed or report usage for a command the session holds, so a usage line and
     * the refusal for exec are reached only by what the session can run. */
    int status = -1;
    begin();
    CHECK(cli_test_exec("visible", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK(handler_runs == 1);
    begin();
    CHECK(cli_test_exec("visible a b", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 2);
    CHECK_TEXT(cli_test_output(), "usage: visible: a command this session holds\n");
    CHECK(handler_runs == 0);
    begin();
    CHECK(cli_test_exec("config get", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    begin();
    CHECK(cli_test_exec("config get a b", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 2);
    CHECK_TEXT(cli_test_output(), "usage: config get: visible beside a hidden sibling\n");
    begin();
    CHECK(cli_test_exec("help visible", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK_TEXT(cli_test_output(), "visible - a command this session holds\n");
    begin();
    CHECK(cli_test_exec("help config get", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK_TEXT(cli_test_output(), "config get - visible beside a hidden sibling\n");
}

static void rejection_events(void)
{
    /* An unknown name leaves one fixed event with no input in it, and names the user. */
    int status = -1;
    begin();
    CHECK(cli_test_exec("my-password-by-mistake", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(cli_fake_event_count() == 1);
    const crux_cli_event_t *event = cli_fake_event(0);
    CHECK(event->type == CRUX_CLI_EVENT_LINE_REJECTED);
    CHECK(event->outcome == CRUX_CLI_EVENT_INVALID);
    CHECK(event->channel == CRUX_ACCESS_CHANNEL_SSH);
    CHECK_TEXT(event->user, "alice");
    CHECK(!cli_test_contains(event, sizeof(*event), "my-password-by-mistake"));

    /* A real command on the wrong channel is a denial, for an operator. */
    begin();
    CHECK(cli_test_exec("serial-only", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(cli_fake_event_count() == 1);
    CHECK(cli_fake_event(0)->outcome == CRUX_CLI_EVENT_DENIED);

    /* A typed hidden name leaves Access's denial event, with no CLI event on top of it. */
    begin();
    CHECK(cli_test_exec("hidden x", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(cli_fake_access.check_denials == 1);
    CHECK(cli_fake_event_count() == 0);

    /* The session may have ended by the time the event is written: the event names nobody. */
    begin();
    cli_fake_access.user_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    CHECK(cli_test_exec("no-such", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(cli_fake_event_count() == 1);
    CHECK_TEXT(cli_fake_event(0)->user, "");
}

static void zephyr_shell_commands_do_not_exist(void)
{
    /* The built-in commands of the Zephyr shell mean nothing here, on either channel. */
    const char *const lines[] = {"kernel",      "kernel thread list",
                                 "device list", "devmem",
                                 "shell",       "shell colors off",
                                 "history",     "resize",
                                 "select",      "retval",
                                 "clear",       "date"};
    for (size_t channel = 0; channel < 2; ++channel) {
        for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); ++index) {
            expect_line(lines[index], channels[channel], "unknown command\n", 127);
        }
    }
}

int main(void)
{
    hidden_looks_unknown();
    limits_come_before_lookup();
    visible_commands_answer();
    rejection_events();
    zephyr_shell_commands_do_not_exist();
    return cli_test_finish("cli rejection");
}
