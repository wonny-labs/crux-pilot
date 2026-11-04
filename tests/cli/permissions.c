/* Access classes, forced password change, help, handler results and the output path, against the
 * scripted access control. */
#include "cli_session_test.h"

#define BOTH (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)

static unsigned permission_runs, session_runs, forced_ok_runs;

static crux_cli_result_t run_permission(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                        void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    (void)context;
    permission_runs += 1;
    return CRUX_CLI_OK;
}

static crux_cli_result_t run_session(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                     void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    session_runs += 1;
    CHECK(crux_cli_session(call).value == CLI_FAKE_ACCESS_SESSION.value);
    return CRUX_CLI_OK;
}

static crux_cli_result_t run_forced_ok(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                       void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    (void)context;
    forced_ok_runs += 1;
    return CRUX_CLI_OK;
}

static crux_cli_result_t next_result;

static crux_cli_result_t run_result(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                    void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    (void)context;
    return next_result;
}

/* What a handler's read_secret saw, for the prompts below. */
static crux_cli_result_t secret_result;
static char secret_value[32];
static size_t secret_length;
static bool secret_in_line;

static crux_cli_result_t run_secret(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                    void *context)
{
    (void)argc;
    (void)argv;
    crux_cli_context_t *memory = context;
    secret_result =
        crux_cli_read_secret(call, "secret: ", secret_value, sizeof(secret_value), &secret_length);
    if (memory != NULL && secret_length > 0) {
        secret_in_line = cli_test_contains(memory->line, sizeof(memory->line), secret_value);
    }
    return secret_result;
}

/* A handler that writes a large body. */
static char large_body[4096];

static crux_cli_result_t run_large(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                   void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    return crux_cli_write(call, large_body, sizeof(large_body));
}

static const crux_cli_command_t table[] = {
    CRUX_CLI_COMMAND("perm", "needs a permission", CRUX_CLI_PERMISSION("held"), 0, 0, BOTH,
                     run_permission, NULL),
    CRUX_CLI_COMMAND("lacked", "needs another", CRUX_CLI_PERMISSION("lacked"), 0, 0, BOTH,
                     run_permission, NULL),
    CRUX_CLI_COMMAND("sess", "any session", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, run_session, NULL),
    CRUX_CLI_COMMAND("forced-ok", "runs during a forced change", CRUX_CLI_SESSION_ONLY, 0, 0,
                     BOTH | CRUX_CLI_FLAG_FORCED_CHANGE_OK, run_forced_ok, NULL),
    CRUX_CLI_COMMAND("perm-forced", "permission and the flag", CRUX_CLI_PERMISSION("held"), 0, 0,
                     BOTH | CRUX_CLI_FLAG_FORCED_CHANGE_OK, run_permission, NULL),
    CRUX_CLI_COMMAND("result", "returns next_result", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, run_result,
                     NULL),
    CRUX_CLI_COMMAND("prompting", "reads a secret", CRUX_CLI_SESSION_ONLY, 0, 0,
                     BOTH | CRUX_CLI_FLAG_INTERACTIVE, run_secret, &cli_test_context),
    CRUX_CLI_COMMAND("sneaky", "reads a secret without the flag", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH,
                     run_secret, NULL),
    CRUX_CLI_COMMAND("large", "writes a large body", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, run_large,
                     NULL),
};

static void begin(void)
{
    cli_test_begin_table(table, sizeof(table) / sizeof(table[0]));
    cli_test_grant("held");
    permission_runs = session_runs = forced_ok_runs = 0;
    next_result = CRUX_CLI_OK;
    secret_result = CRUX_CLI_OK;
    secret_length = 0;
    secret_in_line = false;
}

static void access_classes(void)
{
    /* A session and the permission: each class runs. */
    begin();
    CHECK(cli_test_session("perm\nsess\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(permission_runs == 1);
    CHECK(session_runs == 1);
    CHECK_TEXT(cli_fake_access.last_permission, "held");
    CHECK(cli_fake_access.touch_calls == 2);
    CHECK(cli_fake_access.check_denials == 0);

    /* No permission: hidden, and the check itself is what leaves the denial. */
    begin();
    CHECK(cli_test_session("lacked\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> unknown command\n> ");
    CHECK(permission_runs == 0);
    CHECK(cli_fake_access.check_denials == 1);

    /* A session command makes no permission check of its own. */
    begin();
    CHECK(cli_test_session("sess\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.check_calls == 0);
    CHECK(session_runs == 1);

    /* No session, or an expired one: found first, at the touch, before any lookup. Nothing runs. */
    const crux_access_result_t ends[] = {CRUX_ACCESS_ERR_SESSION_INVALID,
                                         CRUX_ACCESS_ERR_NOT_READY};
    const char *const commands[] = {"perm\n", "sess\n", "no-such\n", "\n"};
    for (size_t result = 0; result < 2; ++result) {
        for (size_t index = 0; index < 4; ++index) {
            begin();
            cli_fake_access.touch_result = ends[result];
            CHECK(cli_test_session(commands[index], false, CRUX_ACCESS_CHANNEL_SERIAL) ==
                  CRUX_CLI_ERR_DENIED);
            CHECK_TEXT(cli_test_output(), "> session ended\n");
            CHECK(permission_runs == 0);
            CHECK(session_runs == 0);
            CHECK(cli_fake_access.check_calls == 0);
        }
        int status = -1;
        begin();
        cli_fake_access.touch_result = ends[result];
        CHECK(cli_test_exec("sess", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_DENIED);
        CHECK(status == 1);
        CHECK(session_runs == 0);
    }

    /* A check that says the session ended between the touch and the check. */
    begin();
    cli_fake_access.check_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    CHECK(cli_test_session("perm\nsess\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_DENIED);
    CHECK_TEXT(cli_test_output(), "> unknown command\nsession ended\n");
    CHECK(session_runs == 0);

    /* NOT_READY from the check looks like a denial, and the session ends. */
    begin();
    cli_fake_access.check_result = CRUX_ACCESS_ERR_NOT_READY;
    CHECK(cli_test_session("perm\nsess\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_DENIED);
    CHECK_TEXT(cli_test_output(), "> unknown command\nsession ended\n");
    CHECK(permission_runs == 0);
    CHECK(session_runs == 0);
    int status = -1;
    begin();
    cli_fake_access.check_result = CRUX_ACCESS_ERR_NOT_READY;
    CHECK(cli_test_exec("perm", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_DENIED);
    CHECK(status == 127);
    CHECK_TEXT(cli_test_output(), "unknown command\nsession ended\n");
}

static void forced_password_change(void)
{
    /* Without the flag nothing runs, for every class. */
    begin();
    cli_fake_access.must_change = true;
    CHECK(cli_test_session("perm\nsess\nlacked\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "password change required, use passwd\n"
                                  "> unknown command\n> unknown command\n> unknown command\n> ");
    CHECK(permission_runs == 0);
    CHECK(session_runs == 0);
    /* The forced-change rule comes first, so no permission check runs and no denial is left. */
    CHECK(cli_fake_access.check_calls == 0);
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_LINE_REJECTED) == 3);

    /* With the flag a session command runs, and a permission command still needs its check, which
     * Access denies during a forced change. */
    begin();
    cli_fake_access.must_change = true;
    CHECK(cli_test_session("forced-ok\nperm-forced\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(forced_ok_runs == 1);
    CHECK(permission_runs == 0);
    CHECK(cli_fake_access.check_denials == 1);

    /* help shows only what can run. */
    begin();
    cli_fake_access.must_change = true;
    CHECK(cli_test_session("help\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "password change required, use passwd\n> "
                                  "help - list commands, or describe one\n"
                                  "logout - end this session\n"
                                  "passwd - change your password\n"
                                  "forced-ok - runs during a forced change\n> ");

    /* The flag is queried at each dispatch, so it clears as soon as the change succeeds. */
    begin();
    cli_fake_access.must_change = true;
    CHECK(cli_test_session("sess\npasswd\nold-password-1\nnew-password-22\nnew-password-22\nsess\n",
                           false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(session_runs == 1);
    CHECK(cli_fake_access.change_calls == 1);
    CHECK_TEXT(cli_fake_access.last_current, "old-password-1");
    CHECK_TEXT(cli_fake_access.last_password, "new-password-22");

    /* Under exec: refused the same way, help and logout work, passwd needs a prompt. */
    int status = -1;
    begin();
    cli_fake_access.must_change = true;
    CHECK(cli_test_exec("sess", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 127);
    CHECK_TEXT(cli_test_output(), "unknown command\n");
    CHECK(session_runs == 0);
    cli_test_output_clear();
    CHECK(cli_test_exec("help logout", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    cli_test_output_clear();
    CHECK(cli_test_exec("logout", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    cli_test_output_clear();
    CHECK(cli_test_exec("passwd", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 2);
    CHECK_TEXT(cli_test_output(), "not available for exec\n");
    CHECK(cli_fake_access.change_calls == 0);

    /* A failed query fails closed, and ends the session if it says the session is over. */
    begin();
    cli_fake_access.must_change_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    CHECK(cli_test_session("sess\nsess\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_DENIED);
    CHECK(session_runs == 0);
    begin();
    cli_fake_access.must_change_result = CRUX_ACCESS_ERR_NOT_READY;
    CHECK(cli_test_exec("sess", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_DENIED);
    CHECK(session_runs == 0);
}

static void help_listing(void)
{
    begin();
    CHECK(cli_test_session("help\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> help - list commands, or describe one\n"
                                  "logout - end this session\n"
                                  "passwd - change your password\n"
                                  "perm - needs a permission\n"
                                  "sess - any session\n"
                                  "forced-ok - runs during a forced change\n"
                                  "perm-forced - permission and the flag\n"
                                  "result - returns next_result\n"
                                  "prompting - reads a secret\n"
                                  "sneaky - reads a secret without the flag\n"
                                  "large - writes a large body\n> ");
    /* Filtering uses the quiet probe: no denial and no event, however much is hidden. */
    CHECK(cli_fake_access.check_calls == 0);
    CHECK(cli_fake_access.check_denials == 0);
    CHECK(cli_fake_access.permitted_calls > 0);
    CHECK(cli_fake_event_count() == 0);

    /* A probe that fails hides the command. */
    begin();
    cli_fake_access.permitted_result = CRUX_ACCESS_ERR_NOT_READY;
    CHECK(cli_test_session("help perm\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> unknown command\n> ");
    /* help itself leaves no event for a hidden name, unlike typing it. */
    CHECK(cli_fake_event_count() == 0);
    CHECK(cli_fake_access.check_denials == 0);

    /* A wrong number of words is help's own usage, whatever they name. */
    int status = -1;
    begin();
    CHECK(cli_test_exec("help a b c", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 2);
    CHECK_TEXT(cli_test_output(), "usage: help: list commands, or describe one\n");
}

static void handler_results(void)
{
    static const struct {
        const char *line;
        crux_cli_result_t result;
        int status;
    } rows[] = {
        {"", CRUX_CLI_OK, 0},
        {"command failed\n", CRUX_CLI_ERR_FAILED, 1},
        {"permission denied\n", CRUX_CLI_ERR_DENIED, 1},
        {"usage: result: returns next_result\n", CRUX_CLI_ERR_USAGE, 2},
        {"input too long\n", CRUX_CLI_ERR_TOO_LONG, 2},
        {"service unavailable\n", CRUX_CLI_ERR_NOT_READY, 1},
        {"command failed\n", CRUX_CLI_ERR_INVALID_ARGUMENT, 1},
        {"", CRUX_CLI_ERR_CANCELLED, 1},
        /* A value outside the enumeration fails closed, with no native text. */
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        {"command failed\n", (crux_cli_result_t)-99, 1},
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        {"command failed\n", (crux_cli_result_t)5, 1},
    };
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        int status = -1;
        begin();
        next_result = rows[index].result;
        CHECK(cli_test_exec("result", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
        CHECK(status == rows[index].status);
        CHECK_TEXT(cli_test_output(), rows[index].line);
    }
    /* A handler that reports the transport is gone ends the session. */
    int status = -1;
    begin();
    next_result = CRUX_CLI_ERR_OUTPUT;
    CHECK(cli_test_exec("result", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_OUTPUT);
    CHECK(status == 1);
    begin();
    next_result = CRUX_CLI_ERR_OUTPUT;
    CHECK(cli_test_session("result\nsess\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(session_runs == 0);
}

static void exec_refuses_interactive(void)
{
    int status = -1;
    begin();
    CHECK(cli_test_exec("prompting", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 2);
    CHECK_TEXT(cli_test_output(), "not available for exec\n");
    CHECK(secret_length == 0);
    /* A handler with no flag that prompts anyway gets USAGE from the prompt. */
    begin();
    CHECK(cli_test_exec("sneaky", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(secret_result == CRUX_CLI_ERR_USAGE);
    CHECK(status == 2);
    CHECK(cli_test_fake.read_calls == 0);
    CHECK(strstr(cli_test_output(), "secret:") == NULL);
}

static void end_at_touch(void *context)
{
    (void)context;
    cli_fake_access.touch_result = CRUX_ACCESS_ERR_SESSION_INVALID;
}

static void secret_prompts(void)
{
    /* Prompted, not echoed, read with backspace and line kill, kept out of the line buffer. */
    begin();
    CHECK(cli_test_session("prompting\nab\x7f"
                           "c-hunter2-secret\n",
                           true, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(secret_result == CRUX_CLI_OK);
    CHECK_TEXT(secret_value, "ac-hunter2-secret");
    CHECK(secret_length == 17);
    CHECK(!secret_in_line);
    CHECK_TEXT(cli_test_output(), "> prompting\nsecret: \n> ");
    CHECK(!cli_test_contains(cli_test_fake.output, cli_test_fake.output_length, "hunter2"));
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "hunter2"));
    CHECK(cli_fake_event_count() == 0);
    /* Finishing the prompt was activity. */
    CHECK(cli_fake_access.touch_calls == 2);

    begin();
    CHECK(cli_test_session("prompting\nthrown-away\x15kept\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(secret_value, "kept");

    /* Without echo on the transport nothing moves the cursor for us either. */
    begin();
    CHECK(cli_test_session("prompting\nvalue\n", false, CRUX_ACCESS_CHANNEL_SSH) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> secret: > ");

    /* Ctrl-C stops it, and the buffer is cleared. */
    begin();
    CHECK(cli_test_session("prompting\npartial\x03", true, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(secret_result == CRUX_CLI_ERR_CANCELLED);
    CHECK(secret_length == 0);
    CHECK(secret_value[0] == '\0');
    CHECK_TEXT(cli_test_output(), "> prompting\nsecret: ^C\n> ");

    /* Too long for the buffer: read to the end of the line and refused. */
    begin();
    static char input[640];
    memset(input, 'x', 500);
    memcpy(input + 500, "\nsess\n", 7);
    cli_fake_input(&cli_test_fake, "prompting\n");
    CHECK(cli_test_session(input, false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(secret_result == CRUX_CLI_ERR_TOO_LONG);
    CHECK(secret_length == 0);
    CHECK(secret_value[0] == '\0');
    CHECK(session_runs == 1);
    CHECK(strstr(cli_test_output(), "input too long\n") != NULL);

    /* At the buffer's limit it fits. */
    begin();
    memset(input, 'y', sizeof(secret_value) - 1u);
    input[sizeof(secret_value) - 1u] = '\n';
    input[sizeof(secret_value)] = '\0';
    cli_fake_input(&cli_test_fake, "prompting\n");
    CHECK(cli_test_session(input, false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(secret_result == CRUX_CLI_OK);
    CHECK(secret_length == sizeof(secret_value) - 1u);

    /* The transport ends in the middle: the session ends, with the buffer cleared. */
    begin();
    CHECK(cli_test_session("prompting\npartial", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(secret_result == CRUX_CLI_ERR_OUTPUT);
    CHECK(secret_value[0] == '\0');

    /* The session expires while the prompt waits. */
    begin();
    cli_fake_input(&cli_test_fake, "prompting\npartial");
    cli_fake_input_timeouts(&cli_test_fake, 1);
    cli_fake_access.user_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    CHECK(cli_test_session_scripted(false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_DENIED);
    CHECK(strstr(cli_test_output(), "session ended\n") != NULL);
    CHECK(secret_result == CRUX_CLI_ERR_OUTPUT);
    CHECK(secret_value[0] == '\0');

    /* The session ends just as the entry finishes: the entry is not handed over. */
    begin();
    cli_fake_input(&cli_test_fake, "prompting\nvalue");
    cli_test_fake.hook_position = cli_test_fake.input_length;
    cli_test_fake.on_position = end_at_touch;
    cli_fake_input(&cli_test_fake, "\n");
    CHECK(cli_test_session_scripted(false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_DENIED);
    CHECK(secret_result == CRUX_CLI_ERR_OUTPUT);
    CHECK(secret_length == 0);
    CHECK(secret_value[0] == '\0');

    /* Arguments are checked. */
    crux_cli_call_t *no_call = NULL;
    char buffer[4];
    size_t length = 9;
    CHECK(crux_cli_read_secret(no_call, "x", buffer, sizeof(buffer), &length) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_write(no_call, "x", 1) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_session(no_call).value == 0);
}

static void expire_session(void *context)
{
    (void)context;
    cli_fake_access.user_result = CRUX_ACCESS_ERR_SESSION_INVALID;
}

static void output_path(void)
{
    memset(large_body, 'L', sizeof(large_body));
    int status = -1;

    /* Short writes are retried until everything is accepted. */
    begin();
    cli_test_fake.write_limit = 7;
    CHECK(cli_test_exec("large", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK(cli_test_fake.output_length == sizeof(large_body));
    CHECK(cli_test_fake.write_calls > sizeof(large_body) / 7u);

    /* Timed-out attempts are retried, with the session checked between them. */
    begin();
    cli_test_fake.write_timeouts = 5;
    CHECK(cli_test_exec("large", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK(cli_test_fake.output_length == sizeof(large_body));
    CHECK(cli_fake_access.user_calls >= 5);

    /* A stuck client: the write ends when the session does, and blocks nothing else. */
    begin();
    cli_test_fake.write_stuck = true;
    cli_test_fake.on_write_timeout = expire_session;
    CHECK(cli_test_exec("large", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_OUTPUT);
    CHECK(status == 1);
    CHECK(cli_test_fake.write_timeouts_seen == 1);
    begin();
    cli_test_fake.write_stuck = true;
    cli_test_fake.on_write_timeout = expire_session;
    CHECK(cli_test_session("large\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);

    /* A failed write ends the session. */
    begin();
    cli_test_fake.write_failed = true;
    CHECK(cli_test_exec("large", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_OUTPUT);
    CHECK(status == 1);
    begin();
    cli_test_fake.write_fail_after = 100;
    CHECK(cli_test_exec("large", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_test_fake.output_length == 100);
    /* Failing while writing the prompt or the echo ends it too. */
    begin();
    cli_test_fake.write_failed = true;
    CHECK(cli_test_session("sess\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(session_runs == 0);
    begin();
    cli_test_fake.write_fail_after = 3;
    CHECK(cli_test_session("sess\n", true, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(session_runs == 0);

    /* A transport that claims more than it was given is broken, in either direction. */
    begin();
    cli_test_fake.write_overclaims = true;
    CHECK(cli_test_exec("large", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_ERR_OUTPUT);
    /* It stops there, without believing the count and running on past the end of the data. */
    CHECK(cli_test_fake.write_calls == 1);
    begin();
    cli_test_fake.read_overclaims = true;
    CHECK(cli_test_session("sess\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(session_runs == 0);
    CHECK(cli_test_fake.read_calls == 1);

    /* A zero-length write does nothing, and the transport is not asked. */
    begin();
    cli_test_fake.write_failed = true;
    crux_cli_call_t *no_call = NULL;
    CHECK(crux_cli_write(no_call, NULL, 0) == CRUX_CLI_ERR_INVALID_ARGUMENT);
}

static void session_end_while_reading(void)
{
    /* Found at a read timeout. */
    begin();
    cli_fake_input(&cli_test_fake, "abandoned-line");
    cli_fake_input_timeouts(&cli_test_fake, 1);
    cli_test_fake.on_read_timeout = expire_session;
    CHECK(cli_test_session_scripted(false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_DENIED);
    CHECK_TEXT(cli_test_output(), "> session ended\n");
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "abandoned-line"));

    /* A stream with no line end and no timeout cannot outlive the limits. */
    begin();
    for (unsigned index = 0; index < 4096u; ++index) {
        cli_fake_input(&cli_test_fake, "x");
    }
    cli_test_fake.hook_position = 100;
    cli_test_fake.on_position = expire_session;
    CHECK(cli_test_session_scripted(false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_DENIED);
    CHECK(cli_test_fake.input_position < 1024u);
    /* Discarded control bytes count as well. */
    begin();
    static char controls[4096];
    memset(controls, 0x01, sizeof(controls));
    cli_fake_input_bytes(&cli_test_fake, controls, sizeof(controls));
    cli_test_fake.hook_position = 100;
    cli_test_fake.on_position = expire_session;
    CHECK(cli_test_session_scripted(false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_DENIED);
    CHECK(cli_test_fake.input_position < 1024u);
}

static void logout_command(void)
{
    /* It ends the run, and the caller ends the session, as for any other way out. */
    begin();
    CHECK(cli_test_session("logout\nsess\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_OK);
    CHECK(session_runs == 0);
    CHECK(cli_fake_access.logout_calls == 0);
    CHECK_TEXT(cli_test_output(), "> ");
    /* It takes no argument. */
    begin();
    CHECK(cli_test_session("logout now\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> usage: logout: end this session\n> ");
}

static void passwd_command(void)
{
    static const struct {
        crux_access_result_t result;
        uint32_t retry_after_ms;
        const char *line;
    } rows[] = {
        {CRUX_ACCESS_OK, 0, "password changed\n"},
        {CRUX_ACCESS_ERR_DENIED, 0, "password not changed\n"},
        {CRUX_ACCESS_ERR_RETRY_LATER, 2500, "try again in 3 s\n"},
        {CRUX_ACCESS_ERR_BUSY, 0, "busy, try again\n"},
        {CRUX_ACCESS_ERR_PASSWORD_REJECTED, 0, "password not accepted\n"},
        {CRUX_ACCESS_ERR_STORE, 0, "could not save, a later boot may use either\n"},
        {CRUX_ACCESS_ERR_NOT_READY, 0, "service unavailable\n"},
        {CRUX_ACCESS_ERR_INVALID_ARGUMENT, 0, "password not changed\n"},
    };
    static char expected[256];
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        begin();
        cli_fake_access.change_result = rows[index].result;
        cli_fake_access.change_retry_after_ms = rows[index].retry_after_ms;
        CHECK(cli_test_session("passwd\nold-pass-word\nnew-pass-word\nnew-pass-word\n", false,
                               CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
        snprintf(expected, sizeof(expected), "> current password: new password: repeat: %s> ",
                 rows[index].line);
        CHECK_TEXT(cli_test_output(), expected);
        CHECK(cli_fake_access.change_calls == 1);
        CHECK(cli_fake_access.last_current_length == 13);
        CHECK(cli_fake_access.last_length == 13);
        CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "pass-word"));
    }

    /* The session ended during the change. */
    begin();
    cli_fake_access.change_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    CHECK(cli_test_session("passwd\nold-pass-word\nnew-pass-word\nnew-pass-word\nsess\n", false,
                           CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_DENIED);
    CHECK(session_runs == 0);

    /* The rule's text follows the fixed words when one is configured. */
    begin();
    CHECK(crux_cli_set_password_rule("use 12 to 128 bytes") == CRUX_CLI_OK);
    cli_fake_access.change_result = CRUX_ACCESS_ERR_PASSWORD_REJECTED;
    CHECK(cli_test_session("passwd\nold\nnew\nnew\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(strstr(cli_test_output(), "password not accepted: use 12 to 128 bytes\n") != NULL);

    /* Two entries that differ make no call. */
    begin();
    CHECK(cli_test_session("passwd\nold-pass-word\nnew-one-word\nnew-two-word\n", false,
                           CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.change_calls == 0);
    CHECK_TEXT(cli_test_output(), "> current password: new password: repeat: passwords differ\n> ");
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "word"));

    /* A prefix of the first entry is not the same entry, whichever came first. */
    const char *const prefixes[] = {"passwd\nold-pass-word\nnew-pass-word\nnew-pass\n",
                                    "passwd\nold-pass-word\nnew-pass\nnew-pass-word\n"};
    for (size_t index = 0; index < 2; ++index) {
        begin();
        CHECK(cli_test_session(prefixes[index], false, CRUX_ACCESS_CHANNEL_SERIAL) ==
              CRUX_CLI_ERR_OUTPUT);
        CHECK(cli_fake_access.change_calls == 0);
        CHECK(strstr(cli_test_output(), "passwords differ\n") != NULL);
    }

    /* Ctrl-C at any of the three prompts stops it with no call. */
    const char *const stops[] = {"passwd\n\x03", "passwd\na\n\x03", "passwd\na\nb\n\x03"};
    for (size_t index = 0; index < 3; ++index) {
        begin();
        CHECK(cli_test_session(stops[index], false, CRUX_ACCESS_CHANNEL_SERIAL) ==
              CRUX_CLI_ERR_OUTPUT);
        CHECK(cli_fake_access.change_calls == 0);
    }

    /* An entry too long for any account is refused with no call. */
    static char line[512];
    memset(line, 'x', 200);
    memcpy(line + 200, "\n", 2);
    begin();
    cli_fake_input(&cli_test_fake, "passwd\n");
    CHECK(cli_test_session(line, false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.change_calls == 0);
    CHECK(strstr(cli_test_output(), "input too long\n") != NULL);

    /* It takes no arguments, and a transport failure ends the session with the buffers clear. */
    begin();
    CHECK(cli_test_session("passwd now\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> usage: passwd: change your password\n> ");
    begin();
    CHECK(cli_test_session("passwd\nold-pass-word\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "pass-word"));
}

static void account_list_command(void)
{
    int status = -1;
    /* Hidden without access.manage. */
    begin();
    CHECK(cli_test_exec("account list", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 127);
    CHECK(cli_fake_access.list_calls == 0);

    static const crux_access_account_info_t accounts[2] = {
        {"admin", "administrator", true, false, 0},
        {"alice", "operator", false, true, 2},
    };
    begin();
    cli_test_grant(CRUX_ACCESS_PERMISSION_MANAGE);
    memcpy(cli_fake_access.accounts, accounts, sizeof(accounts));
    cli_fake_access.account_count = 2;
    CHECK(cli_test_exec("account list", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK_TEXT(cli_test_output(), "admin administrator enabled keys=0\n"
                                  "alice operator disabled must-change keys=2\n");

    /* More than it holds: says so, and shows what fits. */
    begin();
    cli_test_grant(CRUX_ACCESS_PERMISSION_MANAGE);
    for (size_t index = 0; index < CLI_FAKE_ACCESS_ACCOUNTS_MAX; ++index) {
        cli_fake_access.accounts[index] = accounts[0];
    }
    cli_fake_access.account_count = CLI_FAKE_ACCESS_ACCOUNTS_MAX;
    CHECK(cli_test_exec("account list", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK(strstr(cli_test_output(), "and 4 more\n") != NULL);

    /* A service refusal after the registry's check is one fixed line. */
    begin();
    cli_test_grant(CRUX_ACCESS_PERMISSION_MANAGE);
    cli_fake_access.list_result = CRUX_ACCESS_ERR_DENIED;
    CHECK(cli_test_exec("account list", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 1);
    CHECK_TEXT(cli_test_output(), "permission denied\n");
    begin();
    cli_test_grant(CRUX_ACCESS_PERMISSION_MANAGE);
    cli_fake_access.list_result = CRUX_ACCESS_ERR_NOT_READY;
    CHECK(cli_test_exec("account list", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 1);
    CHECK_TEXT(cli_test_output(), "service unavailable\n");
}

int main(void)
{
    access_classes();
    forced_password_change();
    help_listing();
    handler_results();
    exec_refuses_interactive();
    secret_prompts();
    output_path();
    session_end_while_reading();
    logout_command();
    passwd_command();
    account_list_command();
    return cli_test_finish("cli permissions");
}
