/* The console over a scripted crux_serial: open, close, IO latch, quarantine and line ends. */
#include "cli_console.h"
#include "cli_session_test.h"
#include "fake_serial.h"

#include <crux/thread.h>
#include <setjmp.h>

#define PASSWORD "correct-horse-password"

static crux_cli_console_state_t state;
static const crux_cli_console_config_t config = {
    0, {115200, 8, 1, CRUX_SERIAL_PARITY_NONE}, "administrator"};

static void begin(void)
{
    cli_test_begin();
    cli_fake_serial_reset();
    CHECK(crux_cli_init(NULL, NULL, 0) == CRUX_CLI_OK);
    memset(&state, 0, sizeof(state));
}

static cli_fake_t *wire(void)
{
    return &cli_fake_serial.transport;
}

static crux_cli_result_t cycle(void)
{
    return crux_cli_console_cycle(&state, &cli_test_context, &config);
}

static void opens_with_the_configuration(void)
{
    begin();
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_serial.open_calls == 1);
    CHECK(cli_fake_serial.last_port == 0);
    CHECK(cli_fake_serial.last_config.baud_rate == 115200);
    CHECK(cli_fake_serial.last_config.data_bits == 8);
    CHECK(cli_fake_serial.last_config.stop_bits == 1);
    CHECK(cli_fake_serial.last_config.parity == CRUX_SERIAL_PARITY_NONE);
    /* The prompt, with the line discipline's mapping applied only to line ends. */
    CHECK_TEXT(wire()->output, "login: ");
    /* The transport ended, so the port is closed again and the failure is recorded. */
    CHECK(cli_fake_serial.close_calls == 1);
    CHECK(cli_fake_serial.handles_open == 0);
    CHECK(!state.open);
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_CONSOLE_ERROR) == 1);
    CHECK(cli_fake_event(0)->outcome == CRUX_CLI_EVENT_FAILED);
    CHECK(cli_fake_event(0)->channel == CRUX_ACCESS_CHANNEL_SERIAL);
    CHECK_TEXT(cli_fake_event(0)->user, "");
}

static void line_ends_on_the_wire(void)
{
    /* A terminal needs CR LF. The CLI wrote LF, and the console mapped it. */
    begin();
    cli_fake_input(wire(), "alice\r" PASSWORD "\rhelp logout\rlogout\r");
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(wire()->output, "login: alice\r\npassword: \r\n> help logout\r\n"
                               "logout - end this session\r\n> logout\r\nlogin: ");
    CHECK(cli_fake_access.logout_calls == 1);

    /* The mapping survives every short write. */
    begin();
    wire()->write_limit = 1;
    cli_fake_input(wire(), "alice\r" PASSWORD "\rlogout\r");
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(wire()->output, "login: alice\r\npassword: \r\n> logout\r\nlogin: ");

    /* And a CR accepted with its LF timed out: the CR is not sent twice. */
    begin();
    cli_fake_input(wire(), "\r");
    wire()->write_limit = 1;
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(wire()->output, "login: \r\nlogin: ");
}

/* A raw write that accepts the CR and times out on the LF, so the helper must keep its place. */
static unsigned raw_calls, raw_refuse_at;
static char raw_output[64];
static size_t raw_length;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the contract fixes this signature.
static int raw_write(void *transport, const void *buffer, size_t length, uint32_t timeout_ms)
{
    (void)transport;
    (void)timeout_ms;
    raw_calls += 1;
    if (raw_calls == raw_refuse_at) {
        return 0;
    }
    if (raw_refuse_at == UINT32_MAX) {
        return -1;
    }
    memcpy(raw_output + raw_length, buffer, length);
    raw_length += length;
    raw_output[raw_length] = '\0';
    return (int)length;
}

static void terminal_helper(void)
{
    crux_cli_line_ends_t ends = {false};
    /* A run of text goes out in one write, up to the line end. */
    raw_calls = raw_refuse_at = 0;
    raw_length = 0;
    CHECK(crux_cli_write_terminal(&ends, raw_write, NULL, "ab\ncd", 5, 10) == 2);
    CHECK(raw_calls == 1);
    CHECK_TEXT(raw_output, "ab");
    CHECK(crux_cli_write_terminal(&ends, raw_write, NULL, "\ncd", 3, 10) == 1);
    CHECK_TEXT(raw_output, "ab\r\n");
    CHECK(crux_cli_write_terminal(&ends, raw_write, NULL, "cd", 2, 10) == 2);
    CHECK_TEXT(raw_output, "ab\r\ncd");
    CHECK(crux_cli_write_terminal(&ends, raw_write, NULL, "", 0, 10) == 0);

    /* Refused at the CR: a timeout, and nothing consumed. */
    raw_calls = 0;
    raw_refuse_at = 1;
    raw_length = 0;
    CHECK(crux_cli_write_terminal(&ends, raw_write, NULL, "\n", 1, 10) == 0);
    CHECK(raw_length == 0);
    CHECK(!ends.carriage_return_sent);
    /* Refused at the LF: a timeout, and the retry sends only the LF. */
    raw_calls = 0;
    raw_refuse_at = 2;
    CHECK(crux_cli_write_terminal(&ends, raw_write, NULL, "\n", 1, 10) == 0);
    CHECK_TEXT(raw_output, "\r");
    CHECK(ends.carriage_return_sent);
    raw_refuse_at = 0;
    CHECK(crux_cli_write_terminal(&ends, raw_write, NULL, "\n", 1, 10) == 1);
    CHECK_TEXT(raw_output, "\r\n");
    CHECK(!ends.carriage_return_sent);
    /* A failed raw write is a failure. */
    raw_refuse_at = UINT32_MAX;
    CHECK(crux_cli_write_terminal(&ends, raw_write, NULL, "x", 1, 10) < 0);
    CHECK(crux_cli_write_terminal(&ends, raw_write, NULL, "\n", 1, 10) < 0);
}

static void waits_are_not_failures(void)
{
    /* CRUX_ERR_TIMEOUT and CRUX_ERR_BUSY both only mean that nothing is there yet. */
    const crux_status_t waits[] = {CRUX_ERR_TIMEOUT, CRUX_ERR_BUSY};
    for (size_t index = 0; index < 2; ++index) {
        begin();
        cli_fake_serial.read_wait_status = waits[index];
        cli_fake_input(wire(), "ali");
        cli_fake_input_timeouts(wire(), 3);
        cli_fake_input(wire(), "ce\r");
        CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
        CHECK_TEXT(wire()->output, "login: alice\r\npassword: ");
        CHECK(cli_fake_serial.open_calls == 1);
        CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_CONSOLE_ERROR) == 1);
    }
}

static void serial_error_during_a_session(void)
{
    /* The Serial contract latches IO until close and reopen. The console ends the session, closes
     * and reopens the port, and starts at the prompt. */
    begin();
    cli_fake_input(wire(), "alice\r" PASSWORD "\r");
    cli_fake_serial.read_end_status = CRUX_ERR_IO;
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.logout_calls == 1);
    CHECK(cli_fake_access.last_logout.value == CLI_FAKE_ACCESS_SESSION.value);
    CHECK(cli_fake_serial.close_calls == 1);
    CHECK(cli_fake_serial.handles_open == 0);
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_CONSOLE_ERROR) == 1);
    /* The next cycle starts clean, at the prompt, with no leftovers of the old session. */
    cli_fake_output_clear(wire());
    cli_fake_input(wire(), "alice\r" PASSWORD "\rlogout\r");
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_serial.open_calls == 2);
    CHECK_TEXT(wire()->output, "login: alice\r\npassword: \r\n> logout\r\nlogin: ");

    /* A failed write counts the same. */
    begin();
    wire()->write_fail_after = 7;
    cli_fake_input(wire(), "alice\r" PASSWORD "\r");
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_serial.close_calls == 1);
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_CONSOLE_ERROR) == 1);
    CHECK(cli_fake_access.login_calls == 0);
}

static void open_failures(void)
{
    /* A port that will not open: an event for each attempt, and no close of a handle it lacks. */
    const crux_status_t refused[] = {CRUX_ERR_IO, CRUX_ERR_BUSY, CRUX_ERR_NOT_READY,
                                     CRUX_ERR_NOT_SUPPORTED};
    for (size_t index = 0; index < 4; ++index) {
        begin();
        cli_fake_serial.open_result = refused[index];
        CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
        CHECK(cli_fake_serial.close_calls == 0);
        CHECK(!state.open);
        CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_CONSOLE_ERROR) == 1);
        CHECK_TEXT(wire()->output, "");
    }
    /* It starts working when the port does. */
    cli_fake_serial.open_result = CRUX_OK;
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(wire()->output, "login: ");
}

static void quarantined_binding(void)
{
    /* A close that cannot finish draining consumes the handle and quarantines the binding, so
     * every later open returns IO. The console stays down, and says so at each attempt. */
    begin();
    cli_fake_serial.close_result = CRUX_ERR_IO;
    cli_fake_input(wire(), "alice\r" PASSWORD "\r");
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK(!state.open);
    /* One for the read failure, and one for the close. */
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_CONSOLE_ERROR) == 2);
    cli_fake_serial.open_result = CRUX_ERR_IO;
    size_t events = cli_fake_event_count();
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    }
    CHECK(cli_fake_event_count() == events + 3u);
    CHECK(cli_fake_serial.open_calls == 4);
}

static void close_refused(void)
{
    /* Close keeps the handle on BUSY. Nothing else overlaps here, so this should not happen, and
     * the console holds on to the handle rather than opening a second one. */
    begin();
    cli_fake_serial.close_busy_count = 1;
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK(state.open);
    CHECK(cli_fake_serial.handles_open == 1);
    CHECK(cli_fake_serial.open_calls == 1);
    /* The next cycle closes it first. */
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_serial.open_calls == 2);
    CHECK(cli_fake_serial.close_calls == 3);
    CHECK(cli_fake_serial.handles_open == 0);
    CHECK(!state.open);
    /* If it is still refused, the cycle goes no further. */
    begin();
    cli_fake_serial.close_busy_count = 2;
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK(state.open);
    CHECK(cycle() == CRUX_CLI_ERR_OUTPUT);
    CHECK(state.open);
    CHECK(cli_fake_serial.open_calls == 1);
    CHECK(cli_fake_serial.handles_open == 1);
}

/* The pause between cycles. The linker's --wrap option fixes these names, and it takes no time. */
#define PAUSES_MAX 16u
static int64_t pauses_ns[PAUSES_MAX];
static unsigned pause_count;
static unsigned pause_status_failures;
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_thread_sleep_for(crux_duration_t duration);

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_thread_sleep_for(crux_duration_t duration)
{
    if (pause_count < PAUSES_MAX) {
        pauses_ns[pause_count] = duration.private_ns;
    }
    pause_count += 1;
    return pause_status_failures > 0 ? CRUX_ERR_IO : CRUX_OK;
}

/* The run loop never returns, so a test leaves it from inside the fake's open. */
static jmp_buf leave_run;
static unsigned opens_until_leave;
static unsigned opens_without_pause;

static void leave_after_opens(void)
{
    /* Every open after the first follows a pause, so a persistent failure cannot spin. */
    if (pause_count != cli_fake_serial.open_calls) {
        opens_without_pause += 1;
    }
    if (opens_until_leave == 0) {
        longjmp(leave_run, 1);
    }
    opens_until_leave -= 1;
}

static void expect_five_second_pauses(unsigned count)
{
    CHECK(pause_count == count);
    CHECK(opens_without_pause == 0);
    for (unsigned index = 0; index < count && index < PAUSES_MAX; ++index) {
        CHECK(pauses_ns[index] == INT64_C(5000000000));
    }
}

static void run_loop(void)
{
    /* Arguments are checked first, and the only return is for a wrong one. */
    static const crux_cli_console_config_t no_role = {
        0, {115200, 8, 1, CRUX_SERIAL_PARITY_NONE}, NULL};
    begin();
    CHECK(crux_cli_console_run(NULL, &config) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_console_run(&cli_test_context, NULL) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_console_run(&cli_test_context, &no_role) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(cli_fake_serial.open_calls == 0);

    /* It keeps retrying a port that will not open. */
    begin();
    cli_fake_serial.open_result = CRUX_ERR_IO;
    cli_fake_serial.on_open = leave_after_opens;
    opens_until_leave = 4;
    pause_count = 0;
    opens_without_pause = 0;
    if (setjmp(leave_run) == 0) {
        (void)crux_cli_console_run(&cli_test_context, &config);
        CHECK(false);
    }
    CHECK(cli_fake_serial.open_calls == 4);
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_CONSOLE_ERROR) == 4);
    expect_five_second_pauses(4);

    /* And comes back after a serial error. */
    begin();
    cli_fake_serial.on_open = leave_after_opens;
    opens_until_leave = 2;
    pause_count = 0;
    opens_without_pause = 0;
    cli_fake_input(wire(), "alice\r" PASSWORD "\r");
    if (setjmp(leave_run) == 0) {
        (void)crux_cli_console_run(&cli_test_context, &config);
        CHECK(false);
    }
    CHECK(cli_fake_serial.open_calls == 2);
    CHECK(cli_fake_access.logout_calls == 1);
    expect_five_second_pauses(2);

    /* A sleep that fails is no reason to stop, and it cannot be a reason to spin either. */
    begin();
    cli_fake_serial.open_result = CRUX_ERR_IO;
    cli_fake_serial.on_open = leave_after_opens;
    opens_until_leave = 2;
    pause_count = 0;
    opens_without_pause = 0;
    pause_status_failures = 1;
    if (setjmp(leave_run) == 0) {
        (void)crux_cli_console_run(&cli_test_context, &config);
        CHECK(false);
    }
    pause_status_failures = 0;
    CHECK(cli_fake_serial.open_calls == 2);
    expect_five_second_pauses(2);
}

int main(void)
{
    opens_with_the_configuration();
    line_ends_on_the_wire();
    terminal_helper();
    waits_are_not_failures();
    serial_error_during_a_session();
    open_failures();
    quarantined_binding();
    close_refused();
    run_loop();
    return cli_test_finish("cli console");
}
