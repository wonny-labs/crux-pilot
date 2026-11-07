/* The transport as the SSH adapter of Issue #193 will build it, and what a client then sees.
 *
 * The provider interface of Issue #205 is not merged, so this holds a small stand-in with its
 * statuses: a read gives CRUX_ERR_TIMEOUT, CRUX_ERR_NO_DATA at end of input, or CRUX_ERR_IO, and a
 * write can be short. provider_read_bytes and provider_write_bytes are where #193 calls the
 * provider, and ssh_read and ssh_write are its mapping, line for line. */
#include "cli_session_test.h"

#include <crux/status.h>

#define BOTH (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)

static crux_status_t next_read_error = CRUX_ERR_NO_DATA;
static crux_status_t next_write_error = CRUX_ERR_IO;

static crux_status_t provider_read_bytes(void *buffer, size_t length, size_t *transferred)
{
    int result = cli_fake_read(&cli_test_fake, buffer, length, 0);
    *transferred = result > 0 ? (size_t)result : 0;
    return result > 0 ? CRUX_OK : result == 0 ? CRUX_ERR_TIMEOUT : next_read_error;
}

static crux_status_t provider_write_bytes(const void *buffer, size_t length, size_t *transferred)
{
    int result = cli_fake_write(&cli_test_fake, buffer, length, 0);
    *transferred = result > 0 ? (size_t)result : 0;
    return result > 0 ? CRUX_OK : result == 0 ? CRUX_ERR_TIMEOUT : next_write_error;
}

/* The provider applies its own io_timeout_ms, so the CLI's bound goes unused. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the contract fixes this signature.
static int ssh_read(void *transport, void *buffer, size_t length, uint32_t timeout_ms)
{
    (void)transport;
    (void)timeout_ms;
    size_t transferred = 0;
    crux_status_t status = provider_read_bytes(buffer, length, &transferred);
    if (status == CRUX_OK) {
        return (int)transferred;
    }
    return status == CRUX_ERR_TIMEOUT ? 0 : -1;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the contract fixes this signature.
static int ssh_write(void *transport, const void *buffer, size_t length, uint32_t timeout_ms)
{
    (void)transport;
    (void)timeout_ms;
    size_t transferred = 0;
    crux_status_t status = provider_write_bytes(buffer, length, &transferred);
    if (status == CRUX_OK) {
        return (int)transferred;
    }
    return status == CRUX_ERR_TIMEOUT ? 0 : -1;
}

static crux_cli_line_ends_t line_ends;

/* On a channel with a terminal, the same write goes through the line discipline. */
static int ssh_terminal_write(void *transport, const void *buffer, size_t length,
                              uint32_t timeout_ms)
{
    return crux_cli_write_terminal(&line_ends, ssh_write, transport, buffer, length, timeout_ms);
}

static crux_cli_result_t next_result;
static unsigned runs;

static crux_cli_result_t run_command(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                     void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    runs += 1;
    (void)crux_cli_write(call, "ran\n", 4);
    return next_result;
}

static const crux_cli_command_t table[] = {
    CRUX_CLI_COMMAND("permitted", "needs a held permission", CRUX_CLI_PERMISSION("held"), 0, 0,
                     BOTH, run_command, NULL),
    CRUX_CLI_COMMAND("refused", "needs a permission nobody holds", CRUX_CLI_PERMISSION("lacked"), 0,
                     0, BOTH, run_command, NULL),
    CRUX_CLI_COMMAND("any", "any session", CRUX_CLI_SESSION_ONLY, 0, 1, BOTH, run_command, NULL),
};

static void begin(void)
{
    cli_test_begin_table(table, sizeof(table) / sizeof(table[0]));
    cli_test_grant("held");
    next_result = CRUX_CLI_OK;
    next_read_error = CRUX_ERR_NO_DATA;
    next_write_error = CRUX_ERR_IO;
    line_ends.carriage_return_sent = false;
    runs = 0;
}

static crux_cli_result_t shell(const crux_cli_io_t *io)
{
    return crux_cli_run(&cli_test_context, io, CRUX_ACCESS_CHANNEL_SSH, CLI_FAKE_ACCESS_SESSION);
}

static const crux_cli_io_t plain = {ssh_read, ssh_write, NULL, false};
static const crux_cli_io_t terminal = {ssh_read, ssh_terminal_write, NULL, true};

static void same_as_serial(void)
{
    /* Permitted and rejected commands behave the same as on the serial console, as #193 requires.
     */
    static char serial_output[1024];
    const char *const lines[] = {"permitted\n", "refused\n",        "no-such\n",
                                 "help\n",      "help any\n",       "help refused\n",
                                 "any a b\n",   "\"unterminated\n", "any x\n"};
    for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); ++index) {
        begin();
        CHECK(cli_test_session(lines[index], false, CRUX_ACCESS_CHANNEL_SERIAL) ==
              CRUX_CLI_ERR_OUTPUT);
        snprintf(serial_output, sizeof(serial_output), "%s", cli_test_output());
        begin();
        cli_fake_input(&cli_test_fake, lines[index]);
        CHECK(shell(&plain) == CRUX_CLI_ERR_OUTPUT);
        CHECK_TEXT(cli_test_output(), serial_output);
    }
}

static void shell_channel(void)
{
    /* Client EOF, or a closed channel, ends the session as a transport end. */
    begin();
    cli_fake_input(&cli_test_fake, "permitted\n");
    CHECK(shell(&plain) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> ran\n> ");
    CHECK(runs == 1);
    /* A provider's other failures end it too. */
    const crux_status_t ends[] = {CRUX_ERR_IO, CRUX_ERR_INVALID_STATE, CRUX_ERR_NO_DATA};
    for (size_t index = 0; index < 3; ++index) {
        begin();
        next_read_error = ends[index];
        CHECK(shell(&plain) == CRUX_CLI_ERR_OUTPUT);
    }
    /* A logout is not a failure, so the exit status can tell them apart. */
    begin();
    cli_fake_input(&cli_test_fake, "logout\n");
    CHECK(shell(&plain) == CRUX_CLI_OK);

    /* A slow client: a read timeout and a full window, in its own session only. */
    begin();
    cli_fake_input_timeouts(&cli_test_fake, 3);
    cli_fake_input(&cli_test_fake, "permitted\n");
    cli_test_fake.write_timeouts = 2;
    cli_test_fake.write_limit = 1;
    CHECK(shell(&plain) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> ran\n> ");
    CHECK(cli_test_fake.read_timeouts_seen == 3);
    CHECK(cli_test_fake.write_timeouts_seen == 2);

    /* The session's limits end a client that stopped reading. */
    begin();
    cli_fake_input(&cli_test_fake, "permitted\n");
    cli_test_fake.write_stuck = true;
    cli_fake_access.user_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    CHECK(shell(&plain) == CRUX_CLI_ERR_OUTPUT);

    /* A failed write ends it. */
    begin();
    cli_fake_input(&cli_test_fake, "permitted\n");
    cli_test_fake.write_fail_after = 2;
    CHECK(shell(&plain) == CRUX_CLI_ERR_OUTPUT);

    /* With a terminal: echo and CR LF, as on serial. */
    begin();
    cli_fake_input(&cli_test_fake, "permitted\r");
    cli_test_fake.write_limit = 1;
    CHECK(shell(&terminal) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> permitted\r\nran\r\n> ");
}

static void exec_channel(void)
{
    static const struct {
        const char *line;
        crux_cli_result_t handler_result;
        int status;
    } rows[] = {
        {"permitted", CRUX_CLI_OK, 0},   {"permitted", CRUX_CLI_ERR_FAILED, 1},
        {"any", CRUX_CLI_ERR_DENIED, 1}, {"any a b", CRUX_CLI_OK, 2},
        {"passwd", CRUX_CLI_OK, 2},      {"refused", CRUX_CLI_OK, 127},
        {"no-such", CRUX_CLI_OK, 127},
    };
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        int status = -1;
        begin();
        next_result = rows[index].handler_result;
        CHECK(crux_cli_exec(&cli_test_context, &plain, CRUX_ACCESS_CHANNEL_SSH,
                            CLI_FAKE_ACCESS_SESSION, rows[index].line, strlen(rows[index].line),
                            &status) == CRUX_CLI_OK);
        CHECK(status == rows[index].status);
        /* exec reads nothing: a client's input stays for the channel. */
        CHECK(cli_test_fake.read_calls == 0);
    }
    /* A connection that dropped reaches the caller, and the status is in the provider's range. */
    int status = -1;
    begin();
    cli_test_fake.write_failed = true;
    CHECK(crux_cli_exec(&cli_test_context, &plain, CRUX_ACCESS_CHANNEL_SSH, CLI_FAKE_ACCESS_SESSION,
                        "permitted", 9, &status) == CRUX_CLI_ERR_OUTPUT);
    CHECK(status == 1);
}

int main(void)
{
    same_as_serial();
    shell_channel();
    exec_channel();
    return cli_test_finish("cli over an SSH-shaped transport");
}
