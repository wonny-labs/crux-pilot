/* The shell and exec entry points through the fake provider, on the scripted access control. Each
 * parity case runs the same bytes through the real serial console on a scripted Serial and through
 * the real SSH entry points, and requires the same output and the same access calls. */
#include "cli_console.h"
#include "cli_session_test.h"
#include "fake_serial.h"
#include "ssh_cli_harness.h"

#include <crux/thread.h>

#define BOTH (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)
#define SESSION CLI_FAKE_ACCESS_SESSION
#define LOGIN_PASSWORD "correct-horse-password"
/* One text with its length, because some of them hold a NUL. */
#define BYTES(text) {text, sizeof(text) - 1u}

typedef struct {
    const char *bytes;
    size_t length;
} line_t;

/* What one run did, to compare between channels. */
typedef struct {
    char output[CRUX_SSH_FAKE_BUFFER + 1u];
    unsigned handler_runs;
    unsigned touch_calls;
    unsigned check_calls;
    unsigned check_denials;
    unsigned permitted_calls;
    unsigned change_calls;
    size_t rejected;
    crux_cli_event_outcome_t outcomes[8];
    char users[8][CRUX_ACCESS_NAME_SIZE];
    char current[CLI_FAKE_ACCESS_TEXT_MAX];
    char fresh[CLI_FAKE_ACCESS_TEXT_MAX];
} observed_t;

static crux_cli_result_t next_result;
static unsigned handler_runs;

/* The console pauses after a transport failure. The linker's --wrap option fixes these names. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_thread_sleep_for(crux_duration_t duration);

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_thread_sleep_for(crux_duration_t duration)
{
    (void)duration;
    return CRUX_OK;
}

static crux_cli_result_t run_command(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                     void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    handler_runs += 1;
    (void)crux_cli_write(call, "ran\n", 4);
    return next_result;
}

static const crux_cli_command_t table[] = {
    CRUX_CLI_COMMAND("permitted", "needs a held permission", CRUX_CLI_PERMISSION("held"), 0, 0,
                     BOTH, run_command, NULL),
    CRUX_CLI_COMMAND("refused", "needs a permission nobody holds", CRUX_CLI_PERMISSION("lacked"), 0,
                     0, BOTH, run_command, NULL),
    CRUX_CLI_COMMAND("any", "any session", CRUX_CLI_SESSION_ONLY, 0, 1, BOTH, run_command, NULL),
    CRUX_CLI_COMMAND("config get", "two words", CRUX_CLI_SESSION_ONLY, 1, 1, BOTH, run_command,
                     NULL),
};

/* A command for one channel changes help, so the comparison between channels leaves these out. */
static const crux_cli_command_t one_channel_table[] = {
    CRUX_CLI_COMMAND("serial-only", "not on SSH", CRUX_CLI_SESSION_ONLY, 0, 0,
                     CRUX_CLI_CHANNEL_SERIAL, run_command, NULL),
    CRUX_CLI_COMMAND("ssh-only", "not on serial", CRUX_CLI_SESSION_ONLY, 0, 0, CRUX_CLI_CHANNEL_SSH,
                     run_command, NULL),
};

/* Every kind of line a session meets, with no line end. */
static const line_t lines[] = {
    BYTES("permitted"),
    BYTES("refused"),
    BYTES("no-such"),
    BYTES("no-such x"),
    BYTES("help"),
    BYTES("help any"),
    BYTES("help refused"),
    BYTES("help no-such"),
    BYTES("any"),
    BYTES("any x"),
    BYTES("any x y"),
    BYTES("config"),
    BYTES("config get"),
    BYTES("config get x"),
    BYTES("config no-such"),
    BYTES("\"unterminated"),
    BYTES("any \"quoted value\""),
    BYTES("any \"caf\xC3\xA9\""),
    BYTES("any caf\xC3\xA9"),
    BYTES("one 2 3 4 5 6 7 8 9 10 11"),
    BYTES("passwd x"),
    BYTES(""),
    BYTES("   "),
};

/* Input that only a line editor sees, so it is for sessions and not for exec. */
static const line_t editing[] = {
    BYTES("permittedd\x7f"),       /* DEL */
    BYTES("permittedd\b"),         /* Backspace */
    BYTES("garbage\x15permitted"), /* Ctrl-U */
    BYTES("garbage\x03"),          /* Ctrl-C */
    BYTES("perm\x1b[Aitted"),      /* An arrow key is dropped */
    BYTES("perm\x1b[1;5Ditted"),   /* A longer sequence too */
    BYTES("perm\titted"),          /* TAB is not completion */
    BYTES("perm\x04itted"),        /* Ctrl-D is not end of input */
    BYTES("perm\0itted"),          /* NUL */
    BYTES("any \"\x9B\""),         /* A byte above 0x7F, echoed raw, as #181 declares */
    BYTES("\x7f\x7f"),             /* Backspace at the start */
};

/* The built-in commands of the Zephyr shell and the roots that its modules register. None exists.
 */
static const char *const zephyr_shell_lines[] = {
    "kernel",
    "kernel thread list",
    "kernel stacks",
    "kernel reboot cold",
    "device",
    "device list",
    "devmem",
    "devmem 0x0",
    "shell",
    "shell colors",
    "shell colors off",
    "shell echo off",
    "shell stats",
    "history",
    "resize",
    "resize default",
    "select",
    "select kernel",
    "retval",
    "clear",
    "date",
    "date get",
    "log",
    "log status",
    "net",
    "net iface",
    "net ping 127.0.0.1",
    "gpio",
    "gpio get",
    "i2c",
    "i2c scan",
    "spi",
    "flash",
    "flash read",
    "fs",
    "fs ls /",
    "nvs",
    "sensor",
    "pm",
    "stats",
    "mcuboot",
    "rtc",
    "hwinfo",
    "bt",
    "wifi",
    "uart:~$",
    "help -h",
    "kernel -h",
    "shell -h",
};

static void begin_with(const crux_cli_command_t *commands, size_t count)
{
    cli_test_begin_table(commands, count);
    cli_test_grant("held");
    next_result = CRUX_CLI_OK;
    handler_runs = 0;
    CHECK(ssh_cli_begin());
}

static void begin(void)
{
    begin_with(table, sizeof(table) / sizeof(table[0]));
}

static void observe_counters(observed_t *observed, crux_access_channel_t channel)
{
    observed->handler_runs = handler_runs;
    observed->touch_calls = cli_fake_access.touch_calls;
    observed->check_calls = cli_fake_access.check_calls;
    observed->check_denials = cli_fake_access.check_denials;
    observed->permitted_calls = cli_fake_access.permitted_calls;
    observed->change_calls = cli_fake_access.change_calls;
    snprintf(observed->current, sizeof(observed->current), "%s", cli_fake_access.last_current);
    /* The console's login also passes here, so this is the new password only after a change. */
    snprintf(observed->fresh, sizeof(observed->fresh), "%s",
             cli_fake_access.change_calls > 0 ? cli_fake_access.last_password : "");
    observed->rejected = 0;
    for (size_t index = 0; index < cli_fake_event_count(); ++index) {
        const crux_cli_event_t *event = cli_fake_event(index);
        if (event->type != CRUX_CLI_EVENT_LINE_REJECTED ||
            observed->rejected == sizeof(observed->outcomes) / sizeof(observed->outcomes[0])) {
            continue;
        }
        CHECK(event->channel == channel);
        observed->outcomes[observed->rejected] = event->outcome;
        snprintf(observed->users[observed->rejected], sizeof(observed->users[0]), "%s",
                 event->user);
        observed->rejected += 1;
    }
}

static void expect_same(const observed_t *actual, const observed_t *expected, const char *label)
{
    if (strcmp(actual->output, expected->output) != 0) {
        printf("for [%s]\n", label);
    }
    CHECK_TEXT(actual->output, expected->output);
    CHECK(actual->handler_runs == expected->handler_runs);
    CHECK(actual->touch_calls == expected->touch_calls);
    CHECK(actual->check_calls == expected->check_calls);
    CHECK(actual->check_denials == expected->check_denials);
    CHECK(actual->permitted_calls == expected->permitted_calls);
    CHECK(actual->change_calls == expected->change_calls);
    CHECK(actual->rejected == expected->rejected);
    for (size_t index = 0; index < actual->rejected && index < expected->rejected; ++index) {
        CHECK(actual->outcomes[index] == expected->outcomes[index]);
        CHECK_TEXT(actual->users[index], expected->users[index]);
    }
    CHECK_TEXT(actual->current, expected->current);
    CHECK_TEXT(actual->fresh, expected->fresh);
}

/* The bytes through the real serial console, after a login. The console maps line ends and echoes,
 * and keeps going to its login prompt after a logout, which is not part of the session. */
static void run_serial(const void *input, size_t length, observed_t *observed)
{
    static const crux_cli_console_config_t config = {
        0, {115200, 8, 1, CRUX_SERIAL_PARITY_NONE}, "administrator"};
    static const char login[] = "alice\r" LOGIN_PASSWORD "\r";
    static const char after_login[] = "password: \r\n";
    static const char prompt[] = "login: ";
    static crux_cli_console_state_t state;
    cli_fake_t *wire = &cli_fake_serial.transport;

    memset(&state, 0, sizeof(state));
    cli_fake_serial_reset();
    cli_fake_input_bytes(wire, login, sizeof(login) - 1u);
    cli_fake_input_bytes(wire, input, length);
    CHECK(crux_cli_console_cycle(&state, &cli_test_context, &config) == CRUX_CLI_ERR_OUTPUT);
    const char *session = strstr(wire->output, after_login);
    CHECK(session != NULL);
    session = session != NULL ? session + sizeof(after_login) - 1u : "";
    snprintf(observed->output, sizeof(observed->output), "%s", session);
    size_t kept = strlen(observed->output);
    if (kept >= sizeof(prompt) - 1u &&
        strcmp(observed->output + kept - (sizeof(prompt) - 1u), prompt) == 0) {
        observed->output[kept - (sizeof(prompt) - 1u)] = '\0';
    }
    observe_counters(observed, CRUX_ACCESS_CHANNEL_SERIAL);
}

static ssh_cli_result_t run_ssh_shell(const void *input, size_t length, bool terminal,
                                      observed_t *observed)
{
    int connection = -1;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, input, length, true));
    CHECK(ssh_cli_shell(connection, terminal));
    ssh_cli_result_t result = ssh_cli_result(connection);
    snprintf(observed->output, sizeof(observed->output), "%s", result.text);
    observe_counters(observed, CRUX_ACCESS_CHANNEL_SSH);
    return result;
}

/* One input on a terminal, on both channels. */
static void expect_same_on_a_terminal(const void *input, size_t length, const char *label)
{
    static observed_t serial;
    static observed_t ssh;

    memset(&serial, 0, sizeof(serial));
    memset(&ssh, 0, sizeof(ssh));
    begin();
    run_serial(input, length, &serial);
    begin();
    run_ssh_shell(input, length, true, &ssh);
    expect_same(&ssh, &serial, label);
}

/* The same line, ended as a terminal does. */
static void expect_line_same_on_a_terminal(const line_t *line)
{
    static char input[512];

    CHECK(line->length + 1u < sizeof(input));
    memcpy(input, line->bytes, line->length);
    input[line->length] = '\r';
    expect_same_on_a_terminal(input, line->length + 1u, line->bytes);
}

static void permitted_and_rejected_match_serial(void)
{
    for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); ++index) {
        expect_line_same_on_a_terminal(&lines[index]);
    }
    for (size_t index = 0; index < sizeof(editing) / sizeof(editing[0]); ++index) {
        expect_line_same_on_a_terminal(&editing[index]);
    }
}

static void handler_results_match_serial(void)
{
    static const crux_cli_result_t results[] = {
        CRUX_CLI_OK,           CRUX_CLI_ERR_FAILED,    CRUX_CLI_ERR_INVALID_ARGUMENT,
        CRUX_CLI_ERR_DENIED,   CRUX_CLI_ERR_NOT_READY, CRUX_CLI_ERR_USAGE,
        CRUX_CLI_ERR_TOO_LONG, CRUX_CLI_ERR_CANCELLED,
    };
    static observed_t serial;
    static observed_t ssh;
    for (size_t index = 0; index < sizeof(results) / sizeof(results[0]); ++index) {
        memset(&serial, 0, sizeof(serial));
        memset(&ssh, 0, sizeof(ssh));
        begin();
        next_result = results[index];
        run_serial("permitted\rany\r", 14, &serial);
        begin();
        next_result = results[index];
        run_ssh_shell("permitted\rany\r", 14, true, &ssh);
        expect_same(&ssh, &serial, "a handler's result");
        CHECK(ssh.handler_runs == 2);
    }
}

static void limits_match_serial(void)
{
    static char input[CRUX_CLI_LINE_MAX + 8u];
    /* At the limit, the line is read and looked up. Over it, it is refused before that. */
    for (size_t extra = 0; extra < 2; ++extra) {
        size_t length = CRUX_CLI_LINE_MAX + extra;
        memset(input, 'x', length);
        // NOLINTNEXTLINE(bugprone-not-null-terminated-result): the length goes with these bytes.
        memcpy(input, "any ", 4);
        input[length] = '\r';
        expect_same_on_a_terminal(input, length + 1u, "around the line limit");
    }
    /* The output of the refusal is the words, and nothing of the line runs. */
    static observed_t ssh;
    memset(input, 'x', CRUX_CLI_LINE_MAX + 1u);
    input[CRUX_CLI_LINE_MAX + 1u] = '\n';
    begin();
    run_ssh_shell(input, CRUX_CLI_LINE_MAX + 2u, false, &ssh);
    CHECK_TEXT(ssh.output, "> line too long\n> ");
    CHECK(ssh.handler_runs == 0);
}

static void password_change_matches_serial(void)
{
    /* The prompts and the outcome are the same, and neither side echoes a secret. */
    static const char success[] = "passwd\rold-password\rnew-password\rnew-password\r";
    expect_same_on_a_terminal(success, sizeof(success) - 1u, "passwd");
    static const char differ[] = "passwd\rold-password\rnew-password\rnot-the-same\r";
    expect_same_on_a_terminal(differ, sizeof(differ) - 1u, "passwd, the two entries differ");
    static const char cancelled[] = "passwd\rold\x03permitted\r";
    expect_same_on_a_terminal(cancelled, sizeof(cancelled) - 1u, "passwd, Ctrl-C at a prompt");

    static observed_t ssh;
    memset(&ssh, 0, sizeof(ssh));
    begin();
    run_ssh_shell(success, sizeof(success) - 1u, true, &ssh);
    CHECK(strstr(ssh.output, "old-password") == NULL);
    CHECK(strstr(ssh.output, "new-password") == NULL);
    CHECK(strstr(ssh.output, "password changed") != NULL);
    CHECK_TEXT(cli_fake_access.last_current, "old-password");
    CHECK(cli_fake_access.change_calls == 1);
}

static void forced_change_matches_serial(void)
{
    /* Everything but the commands that may run is unknown, in the same words. */
    static const line_t held_back[] = {BYTES("permitted"), BYTES("any"), BYTES("help")};
    static observed_t serial;
    static observed_t ssh;
    for (size_t index = 0; index < sizeof(held_back) / sizeof(held_back[0]); ++index) {
        char input[32];
        int length = snprintf(input, sizeof(input), "%s\r", held_back[index].bytes);
        memset(&serial, 0, sizeof(serial));
        memset(&ssh, 0, sizeof(ssh));
        begin();
        cli_fake_access.must_change = true;
        run_serial(input, (size_t)length, &serial);
        begin();
        cli_fake_access.must_change = true;
        run_ssh_shell(input, (size_t)length, true, &ssh);
        expect_same(&ssh, &serial, "during a forced change");
        CHECK(strstr(ssh.output, "password change required") != NULL);
    }
}

static void without_a_terminal(void)
{
    /* No echo and no CR: the serial session with the same transport shape prints the same. */
    static char input[512];
    static observed_t baseline;
    static observed_t ssh;
    const line_t *sets[] = {lines, editing};
    const size_t counts[] = {sizeof(lines) / sizeof(lines[0]),
                             sizeof(editing) / sizeof(editing[0])};
    for (size_t set = 0; set < 2; ++set) {
        for (size_t index = 0; index < counts[set]; ++index) {
            const line_t *line = &sets[set][index];
            memcpy(input, line->bytes, line->length);
            input[line->length] = '\n';
            memset(&baseline, 0, sizeof(baseline));
            memset(&ssh, 0, sizeof(ssh));
            begin();
            cli_fake_input_bytes(&cli_test_fake, input, line->length + 1u);
            CHECK(cli_test_session_scripted(false, CRUX_ACCESS_CHANNEL_SERIAL) ==
                  CRUX_CLI_ERR_OUTPUT);
            snprintf(baseline.output, sizeof(baseline.output), "%s", cli_test_output());
            observe_counters(&baseline, CRUX_ACCESS_CHANNEL_SERIAL);
            begin();
            run_ssh_shell(input, line->length + 1u, false, &ssh);
            expect_same(&ssh, &baseline, line->bytes);
            CHECK(strchr(ssh.output, '\r') == NULL);
        }
    }
    /* Word for word, for the two forms of one line. */
    begin();
    run_ssh_shell("permitted\n", 10, false, &ssh);
    CHECK_TEXT(ssh.output, "> ran\n> ");
    begin();
    run_ssh_shell("permitted\r", 10, true, &ssh);
    CHECK_TEXT(ssh.output, "> permitted\r\nran\r\n> ");
}

static void a_terminal_follows_the_connection(void)
{
    /* window-change has no effect on a session, and its size is ignored. */
    int connection = -1;
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(crux_ssh_fake_client()->channel_request(connection, "pty-req", NULL, 0));
    CHECK(crux_ssh_fake_client()->channel_request(connection, "window-change", NULL, 0));
    CHECK(ssh_cli_input(connection, "permitted\r", 10, true));
    CHECK(crux_ssh_fake_client()->channel_request(connection, "shell", NULL, 0));
    CHECK_TEXT(ssh_cli_result(connection).text, "> permitted\r\nran\r\n> ");

    /* A terminal on one connection says nothing about the next. */
    int other = -1;
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_connect(SESSION, &other));
    CHECK(ssh_cli_input(connection, "permitted\r", 10, true));
    CHECK(ssh_cli_input(other, "permitted\n", 10, true));
    CHECK(ssh_cli_shell(connection, true));
    CHECK(ssh_cli_shell(other, false));
    CHECK_TEXT(ssh_cli_result(connection).text, "> permitted\r\nran\r\n> ");
    CHECK_TEXT(ssh_cli_result(other).text, "> ran\n> ");

    /* The line ends of one short write do not carry to another session. */
    begin();
    ssh_cli_shim.write_limit = 1;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, "permitted\r", 10, true));
    CHECK(ssh_cli_shell(connection, true));
    CHECK_TEXT(ssh_cli_result(connection).text, "> permitted\r\nran\r\n> ");
}

static void shell_exit_status(void)
{
    int connection = -1;

    /* A logout is the only clean end, and the session's end stays with connection_closed. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, "logout\n", 7, false));
    CHECK(ssh_cli_shell(connection, false));
    ssh_cli_result_t result = ssh_cli_result(connection);
    CHECK(result.has_status && result.status == 0);
    CHECK(result.eof);
    CHECK(ssh_cli_shim.close_calls == 1);
    CHECK(!ssh_cli_shim.used_after_close);
    CHECK(ssh_cli_connections_closed() == 1);
    CHECK(cli_fake_access.logout_calls == 0);

    /* Client EOF ends the shell as a transport end, which the contract gives 1. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, "permitted\n", 10, true));
    CHECK(ssh_cli_shell(connection, false));
    result = ssh_cli_result(connection);
    CHECK(result.has_status && result.status == 1);
    CHECK(ssh_cli_shim.close_calls == 1);
    CHECK(cli_fake_access.logout_calls == 0);

    /* Expiry: the words, then 1. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    cli_fake_access.user_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    CHECK(ssh_cli_shell(connection, false));
    result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "> session ended\n");
    CHECK(result.has_status && result.status == 1);

    /* A registry that is not ready: nothing was written and nothing read. */
    begin();
    crux_cli_test_reset();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_shell(connection, true));
    result = ssh_cli_result(connection);
    CHECK(result.length == 0);
    CHECK(result.has_status && result.status == 1);
    CHECK(ssh_cli_shim.read_calls == 0);
    CHECK(ssh_cli_shim.write_calls == 0);
}

static void expect_exec_like_serial(const char *line, size_t length, const char *label)
{
    static observed_t baseline;
    static observed_t ssh;
    int connection = -1;
    int status = -1;

    memset(&baseline, 0, sizeof(baseline));
    memset(&ssh, 0, sizeof(ssh));
    crux_cli_result_t chosen = next_result;
    begin();
    next_result = chosen;
    crux_cli_result_t answer =
        cli_test_exec_bytes(line, length, CRUX_ACCESS_CHANNEL_SERIAL, &status);
    CHECK(answer == CRUX_CLI_OK);
    snprintf(baseline.output, sizeof(baseline.output), "%s", cli_test_output());
    observe_counters(&baseline, CRUX_ACCESS_CHANNEL_SERIAL);
    begin();
    next_result = chosen;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_exec(connection, line, length));
    ssh_cli_result_t result = ssh_cli_result(connection);
    snprintf(ssh.output, sizeof(ssh.output), "%s", result.text);
    observe_counters(&ssh, CRUX_ACCESS_CHANNEL_SSH);
    expect_same(&ssh, &baseline, label);
    CHECK(result.has_status && result.status == status);
    CHECK(result.eof);
    CHECK(ssh_cli_shim.read_calls == 0);
    CHECK(ssh_cli_shim.close_calls == 1);
    CHECK(cli_fake_access.logout_calls == 0);
}

static void exec_matches_serial(void)
{
    for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); ++index) {
        next_result = CRUX_CLI_OK;
        expect_exec_like_serial(lines[index].bytes, lines[index].length, lines[index].bytes);
    }
}

static void exec_status_table(void)
{
    /* The contract's rows, end to end. */
    static const struct {
        const char *line;
        const char *output;
        crux_cli_result_t handler_result;
        int status;
    } rows[] = {
        {"permitted", "ran\n", CRUX_CLI_OK, 0},
        {"permitted", "ran\ncommand failed\n", CRUX_CLI_ERR_FAILED, 1},
        {"permitted", "ran\ncommand failed\n", CRUX_CLI_ERR_INVALID_ARGUMENT, 1},
        {"permitted", "ran\npermission denied\n", CRUX_CLI_ERR_DENIED, 1},
        {"permitted", "ran\nservice unavailable\n", CRUX_CLI_ERR_NOT_READY, 1},
        {"permitted", "ran\nusage: permitted: needs a held permission\n", CRUX_CLI_ERR_USAGE, 2},
        {"permitted", "ran\ninput too long\n", CRUX_CLI_ERR_TOO_LONG, 2},
        {"permitted", "ran\n", CRUX_CLI_ERR_CANCELLED, 1},
        {"any a b", "usage: any: any session\n", CRUX_CLI_OK, 2},
        {"passwd", "not available for exec\n", CRUX_CLI_OK, 2},
        {"refused", "unknown command\n", CRUX_CLI_OK, 127},
        {"no-such", "unknown command\n", CRUX_CLI_OK, 127},
        {"permitted\n", "invalid line\n", CRUX_CLI_OK, 2},
        {"perm\titted", "invalid line\n", CRUX_CLI_OK, 2},
        {"\"unterminated", "invalid line\n", CRUX_CLI_OK, 2},
        {"", "", CRUX_CLI_OK, 0},
        {"logout", "", CRUX_CLI_OK, 0},
    };
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        int connection = -1;
        begin();
        next_result = rows[index].handler_result;
        CHECK(ssh_cli_connect(SESSION, &connection));
        CHECK(ssh_cli_exec(connection, rows[index].line, strlen(rows[index].line)));
        ssh_cli_result_t result = ssh_cli_result(connection);
        CHECK_TEXT(result.text, rows[index].output);
        CHECK(result.has_status && result.status == rows[index].status);
        CHECK(ssh_cli_shim.read_calls == 0);
    }

    /* Over the line limit, at once. The fake accepts a command up to its buffer. */
    static char line[CRUX_CLI_LINE_MAX + 2u];
    int connection = -1;
    memset(line, 'x', CRUX_CLI_LINE_MAX + 1u);
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_exec(connection, line, CRUX_CLI_LINE_MAX + 1u));
    ssh_cli_result_t result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "line too long\n");
    CHECK(result.has_status && result.status == 2);

    /* Input from the client stays unread, and the status arrives. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, "permitted\n", 10, false));
    CHECK(ssh_cli_exec(connection, "any", 3));
    result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "ran\n");
    CHECK(handler_runs == 1);
    CHECK(ssh_cli_shim.read_calls == 0);

    /* A session that ended before the command: the words, and 1, and nothing runs. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    cli_fake_access.touch_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    CHECK(ssh_cli_exec(connection, "permitted", 9));
    result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "session ended\n");
    CHECK(result.has_status && result.status == 1);
    CHECK(handler_runs == 0);

    /* A registry that is not ready. */
    begin();
    crux_cli_test_reset();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_exec(connection, "permitted", 9));
    result = ssh_cli_result(connection);
    CHECK(result.length == 0);
    CHECK(result.has_status && result.status == 1);

    /* The output path of exec has no terminal: a client's pty-req does not reach it. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(crux_ssh_fake_client()->channel_request(connection, "pty-req", NULL, 0));
    CHECK(ssh_cli_exec(connection, "permitted", 9));
    CHECK_TEXT(ssh_cli_result(connection).text, "ran\n");
}

static void one_channel_commands(void)
{
    static const size_t count = sizeof(one_channel_table) / sizeof(one_channel_table[0]);
    int connection = -1;

    /* A command for serial does not exist here, in either form, and help does not list it. */
    begin_with(one_channel_table, count);
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_exec(connection, "serial-only", 11));
    ssh_cli_result_t result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "unknown command\n");
    CHECK(result.status == 127);
    CHECK(handler_runs == 0);
    CHECK(cli_fake_event_count() == 1);
    CHECK(cli_fake_event(0)->outcome == CRUX_CLI_EVENT_DENIED);
    CHECK(cli_fake_event(0)->channel == CRUX_ACCESS_CHANNEL_SSH);

    begin_with(one_channel_table, count);
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, "serial-only\nhelp serial-only\nhelp\n", 34, true));
    CHECK(ssh_cli_shell(connection, false));
    CHECK_TEXT(ssh_cli_result(connection).text, "> unknown command\n"
                                                "> unknown command\n"
                                                "> help - list commands, or describe one\n"
                                                "logout - end this session\n"
                                                "passwd - change your password\n"
                                                "ssh-only - not on serial\n"
                                                "> ");
    CHECK(handler_runs == 0);

    /* A command for SSH does, which shows that the entry points name the SSH channel. */
    begin_with(one_channel_table, count);
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_exec(connection, "ssh-only", 8));
    result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "ran\n");
    CHECK(result.status == 0);
    begin_with(one_channel_table, count);
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, "ssh-only\n", 9, true));
    CHECK(ssh_cli_shell(connection, false));
    CHECK_TEXT(ssh_cli_result(connection).text, "> ran\n> ");
    CHECK(handler_runs == 1);
}

static void expect_zephyr_line_unknown(const char *line)
{
    int connection = -1;
    char input[64];
    int length = snprintf(input, sizeof(input), "%s\r", line);

    /* A shell with a terminal, where the Zephyr shell's prompt and completion would be. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, input, (size_t)length, true));
    CHECK(ssh_cli_shell(connection, true));
    char expected[128];
    snprintf(expected, sizeof(expected), "> %s\r\nunknown command\r\n> ", line);
    CHECK_TEXT(ssh_cli_result(connection).text, expected);
    CHECK(handler_runs == 0);
    /* Help answers for itself, and any other line is rejected and recorded. */
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_LINE_REJECTED) ==
          (strncmp(line, "help ", 5) == 0 ? 0u : 1u));

    /* A shell with no terminal. */
    input[length - 1] = '\n';
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, input, (size_t)length, true));
    CHECK(ssh_cli_shell(connection, false));
    CHECK_TEXT(ssh_cli_result(connection).text, "> unknown command\n> ");
    CHECK(handler_runs == 0);

    /* exec, which gives the status a script reads. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_exec(connection, line, strlen(line)));
    ssh_cli_result_t result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "unknown command\n");
    CHECK(result.has_status && result.status == 127);
    CHECK(handler_runs == 0);
}

static void zephyr_shell_is_not_reachable(void)
{
    /* "help -h" and the like reach the registry's help, which knows no such command. */
    for (size_t index = 0; index < sizeof(zephyr_shell_lines) / sizeof(zephyr_shell_lines[0]);
         ++index) {
        expect_zephyr_line_unknown(zephyr_shell_lines[index]);
    }

    /* The Zephyr shell's own help lists its commands and tells the user to press Tab. This one
     * lists the registry, in exactly the registry's order. */
    int connection = -1;
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_exec(connection, "help", 4));
    ssh_cli_result_t result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "help - list commands, or describe one\n"
                            "logout - end this session\n"
                            "passwd - change your password\n"
                            "permitted - needs a held permission\n"
                            "any - any session\n"
                            "config get - two words\n");
    CHECK(result.status == 0);

    /* Tab completes nothing, and the arrow keys of its history recall nothing. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    static const char keys[] = "ker\t\rpermitted\r\x1b[A\x1b[A\r";
    CHECK(ssh_cli_input(connection, keys, sizeof(keys) - 1u, true));
    CHECK(ssh_cli_shell(connection, true));
    CHECK_TEXT(ssh_cli_result(connection).text,
               "> ker\r\nunknown command\r\n> permitted\r\nran\r\n> \r\n> ");
    CHECK(handler_runs == 1);
}

static void refused_without_a_session_or_a_provider(void)
{
    int connection = -1;

    /* A connection whose attachment holds no session: nothing is read or written. */
    begin();
    CHECK(ssh_cli_connect((crux_access_session_t){0}, &connection));
    CHECK(ssh_cli_input(connection, "permitted\n", 10, true));
    CHECK(ssh_cli_shell(connection, true));
    ssh_cli_result_t result = ssh_cli_result(connection);
    CHECK(result.length == 0);
    CHECK(result.has_status && result.status == 255);
    CHECK(ssh_cli_shim.read_calls == 0);
    CHECK(ssh_cli_shim.write_calls == 0);
    CHECK(handler_runs == 0);
    begin();
    CHECK(ssh_cli_connect((crux_access_session_t){0}, &connection));
    CHECK(ssh_cli_exec(connection, "permitted", 9));
    result = ssh_cli_result(connection);
    CHECK(result.length == 0);
    CHECK(result.has_status && result.status == 255);
    CHECK(handler_runs == 0);
    CHECK(cli_fake_access.touch_calls == 0);

    /* No attachment, no channel or no command, which a provider must not do, is refused the same
     * way, and the channel is not touched. */
    static char not_a_channel;
    crux_ssh_channel_t *channel = (crux_ssh_channel_t *)&not_a_channel;
    begin();
    crux_ssh_attachment_t *attachment = crux_ssh_fake_attachment(SESSION);
    CHECK(crux_ssh_cli_shell(NULL, NULL, channel, false) == 255);
    CHECK(crux_ssh_cli_exec(NULL, NULL, channel, "permitted", 9) == 255);
    CHECK(crux_ssh_cli_shell(NULL, attachment, NULL, false) == 255);
    CHECK(crux_ssh_cli_exec(NULL, attachment, NULL, "permitted", 9) == 255);
    CHECK(crux_ssh_cli_exec(NULL, attachment, channel, NULL, 0) == 255);
    CHECK(ssh_cli_shim.close_calls == 0);
    CHECK(handler_runs == 0);

    /* A provider that cannot do the job leaves no provider, so sessions stay refused. */
    begin();
    CHECK(crux_ssh_cli_set_provider(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_shell(connection, false));
    result = ssh_cli_result(connection);
    CHECK(result.status == 255);
    CHECK(result.length == 0);
    CHECK(ssh_cli_shim.close_calls == 0);

    crux_ssh_provider_t missing = *crux_ssh_fake_provider();
    missing.read = NULL;
    begin();
    CHECK(crux_ssh_cli_set_provider(&missing) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_exec(connection, "permitted", 9));
    CHECK(ssh_cli_result(connection).status == 255);
    missing = *crux_ssh_fake_provider();
    missing.write = NULL;
    CHECK(crux_ssh_cli_set_provider(&missing) == CRUX_ERR_INVALID_ARGUMENT);
    missing = *crux_ssh_fake_provider();
    missing.close = NULL;
    CHECK(crux_ssh_cli_set_provider(&missing) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(handler_runs == 0);
}

static void transport_statuses(void)
{
    /* The contract's mapping for a read: each failure ends a shell, with status 1. */
    static const crux_status_t read_ends[] = {CRUX_ERR_NO_DATA, CRUX_ERR_IO, CRUX_ERR_INVALID_STATE,
                                              CRUX_ERR_INVALID_ARGUMENT};
    int connection = -1;
    for (size_t index = 0; index < sizeof(read_ends) / sizeof(read_ends[0]); ++index) {
        begin();
        ssh_cli_shim.read_result = read_ends[index];
        CHECK(ssh_cli_connect(SESSION, &connection));
        CHECK(ssh_cli_shell(connection, false));
        CHECK(ssh_cli_result(connection).status == 1);
        CHECK(ssh_cli_shim.read_calls == 1);
    }
    /* A write's: the prompt is the first write, for exec the command's output. */
    static const crux_status_t write_ends[] = {CRUX_ERR_IO, CRUX_ERR_INVALID_STATE,
                                               CRUX_ERR_INVALID_ARGUMENT};
    for (size_t index = 0; index < sizeof(write_ends) / sizeof(write_ends[0]); ++index) {
        begin();
        ssh_cli_shim.write_result = write_ends[index];
        CHECK(ssh_cli_connect(SESSION, &connection));
        CHECK(ssh_cli_shell(connection, false));
        CHECK(ssh_cli_result(connection).status == 1);
        CHECK(ssh_cli_shim.read_calls == 0);
        CHECK(ssh_cli_shim.write_calls == 1);
        begin();
        ssh_cli_shim.write_result = write_ends[index];
        CHECK(ssh_cli_connect(SESSION, &connection));
        CHECK(ssh_cli_exec(connection, "no-such", 7));
        CHECK(ssh_cli_result(connection).status == 1);
    }
    /* A provider against its contract does not hang the session or overrun the buffer. */
    begin();
    ssh_cli_shim.read_says_nothing = true;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_shell(connection, false));
    CHECK(ssh_cli_result(connection).status == 1);
    CHECK(ssh_cli_shim.read_calls == 1);
    begin();
    ssh_cli_shim.read_overclaims = true;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_shell(connection, false));
    CHECK(ssh_cli_result(connection).status == 1);
    begin();
    ssh_cli_shim.write_says_nothing = true;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_shell(connection, false));
    CHECK(ssh_cli_result(connection).status == 1);
    CHECK(ssh_cli_shim.write_calls == 1);
    begin();
    ssh_cli_shim.write_overclaims = true;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_exec(connection, "no-such", 7));
    CHECK(ssh_cli_result(connection).status == 1);

    /* The CLI checks a count that it sees, and the CR of a line end is one that it does not. */
    begin();
    ssh_cli_shim.line_end_overclaims = true;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, "permitted\r", 10, true));
    CHECK(ssh_cli_shell(connection, true));
    ssh_cli_result_t overclaimed = ssh_cli_result(connection);
    CHECK_TEXT(overclaimed.text, "> permitted");
    CHECK(overclaimed.status == 1);

    /* A connection that drops: no exit status can arrive. */
    begin();
    CHECK(ssh_cli_connect(SESSION, &connection));
    crux_ssh_fake_client()->drop_on_write(connection);
    CHECK(ssh_cli_exec(connection, "permitted", 9));
    ssh_cli_result_t result = ssh_cli_result(connection);
    CHECK(!result.has_status);
    CHECK(ssh_cli_connections_closed() == 1);
}

static void end_session_on_the_tenth_timeout(void *context)
{
    unsigned *seen = context;
    *seen += 1;
    if (*seen == 10) {
        cli_fake_access.user_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    }
}

static void slow_clients(void)
{
    int connection = -1;
    unsigned seen = 0;

    /* A client slow to send: read timeouts are its own wait, and the session carries on. */
    begin();
    ssh_cli_shim.read_timeouts = 5;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, "permitted\n", 10, true));
    CHECK(ssh_cli_shell(connection, false));
    CHECK_TEXT(ssh_cli_result(connection).text, "> ran\n> ");
    CHECK(ssh_cli_shim.read_timeouts_seen == 5);
    CHECK(handler_runs == 1);

    /* A client slow to read: writes time out, then short writes, and nothing is lost or doubled. */
    begin();
    ssh_cli_shim.write_timeouts = 5;
    ssh_cli_shim.write_limit = 1;
    CHECK(ssh_cli_connect(SESSION, &connection));
    CHECK(ssh_cli_input(connection, "permitted\r", 10, true));
    CHECK(ssh_cli_shell(connection, true));
    CHECK_TEXT(ssh_cli_result(connection).text, "> permitted\r\nran\r\n> ");
    CHECK(ssh_cli_shim.write_timeouts_seen == 5);

    /* A window that fills in the middle of an exec's output, for as long as the session lasts: the
     * session's limits end it, not the client, and every wait was a timeout of its own. */
    begin();
    seen = 0;
    ssh_cli_shim.on_write_timeout = end_session_on_the_tenth_timeout;
    ssh_cli_shim.hook_context = &seen;
    CHECK(ssh_cli_connect(SESSION, &connection));
    crux_ssh_fake_client()->set_window(connection, 2);
    CHECK(ssh_cli_exec(connection, "permitted", 9));
    ssh_cli_result_t result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "ra");
    CHECK(seen >= 10);
    CHECK(result.has_status && result.status == 1);

    /* The same on a shell's prompt, a client that never read a byte. */
    begin();
    seen = 0;
    ssh_cli_shim.on_write_timeout = end_session_on_the_tenth_timeout;
    ssh_cli_shim.hook_context = &seen;
    CHECK(ssh_cli_connect(SESSION, &connection));
    crux_ssh_fake_client()->set_window(connection, 0);
    CHECK(ssh_cli_shell(connection, true));
    result = ssh_cli_result(connection);
    CHECK(result.length == 0);
    CHECK(seen >= 10);
    CHECK(result.has_status && result.status == 1);
    CHECK(ssh_cli_shim.read_calls == 0);
}

typedef struct {
    int stuck_status;
    ssh_cli_result_t other;
    bool ran;
} nested_t;

/* Runs a whole session of another client from inside the stuck write of the first. */
static void run_other_session(void *context)
{
    nested_t *nested = context;
    if (nested->ran) {
        return;
    }
    nested->ran = true;
    int other = -1;
    CHECK(ssh_cli_connect(SESSION, &other));
    CHECK(ssh_cli_exec(other, "permitted", 9));
    nested->other = ssh_cli_result(other);
    cli_fake_access.user_result = CRUX_ACCESS_ERR_SESSION_INVALID;
}

static void slow_client_holds_only_itself(void)
{
    /* In the middle of a blocked write, no lock of the adapter or the CLI is held. The threaded
     * suite shows the same on two real threads. */
    static nested_t nested;
    int stuck = -1;

    memset(&nested, 0, sizeof(nested));
    begin();
    ssh_cli_shim.on_write_timeout = run_other_session;
    ssh_cli_shim.hook_context = &nested;
    CHECK(ssh_cli_connect(SESSION, &stuck));
    crux_ssh_fake_client()->set_window(stuck, 0);
    CHECK(ssh_cli_exec(stuck, "permitted", 9));
    CHECK(nested.ran);
    CHECK_TEXT(nested.other.text, "ran\n");
    CHECK(nested.other.has_status && nested.other.status == 0);
    ssh_cli_result_t result = ssh_cli_result(stuck);
    CHECK(result.length == 0);
    CHECK(result.has_status && result.status == 1);
    CHECK(ssh_cli_connections_closed() == 2);
}

static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void random_input_runs_no_hidden_command(void)
{
    /* From a fixed seed, through every form. tests/cli/fuzz.c covers the parser in depth. */
    static const char alphabet[] = "refused permitted\t\r\n\x03\x15\x1b[A\"\\ \x7f\xC3\xA9";
    uint32_t state = 0x193u;
    static char input[300];
    for (unsigned round = 0; round < 300; ++round) {
        size_t length = next_random(&state) % sizeof(input);
        for (size_t index = 0; index < length; ++index) {
            input[index] = alphabet[next_random(&state) % (sizeof(alphabet) - 1u)];
        }
        int connection = -1;
        begin();
        CHECK(ssh_cli_connect(SESSION, &connection));
        bool terminal = (round % 3u) == 0;
        if ((round % 3u) == 2) {
            CHECK(ssh_cli_exec(connection, input, length));
        } else {
            CHECK(ssh_cli_input(connection, input, length, true));
            CHECK(ssh_cli_shell(connection, terminal));
        }
        ssh_cli_result_t result = ssh_cli_result(connection);
        CHECK(result.has_status);
        CHECK(result.status == 0 || result.status == 1 || result.status == 2 ||
              result.status == 127);
        CHECK(cli_fake_access.check_denials == 0 || strstr(result.text, "unknown command") != NULL);
        CHECK(cli_fake_access.logout_calls == 0);
    }
}

int main(void)
{
    permitted_and_rejected_match_serial();
    handler_results_match_serial();
    limits_match_serial();
    password_change_matches_serial();
    forced_change_matches_serial();
    without_a_terminal();
    a_terminal_follows_the_connection();
    shell_exit_status();
    exec_matches_serial();
    exec_status_table();
    one_channel_commands();
    zephyr_shell_is_not_reachable();
    refused_without_a_session_or_a_provider();
    transport_statuses();
    slow_clients();
    slow_client_holds_only_itself();
    random_input_runs_no_hidden_command();
    return cli_test_finish("cli over the fake SSH provider");
}
