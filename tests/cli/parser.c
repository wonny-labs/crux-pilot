/* Line editing and tokenising limits, through the public session entry points. */
#include "cli_session_test.h"

#define BOTH (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)

static unsigned echo_runs;
static char last_config_value[256];

static crux_cli_result_t echo_args(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                   void *context)
{
    (void)context;
    echo_runs += 1;
    for (size_t index = 0; index < argc; ++index) {
        (void)crux_cli_write(call, "[", 1);
        (void)crux_cli_write(call, argv[index], strlen(argv[index]));
        (void)crux_cli_write(call, "]", 1);
    }
    return crux_cli_write(call, "\n", 1);
}

static crux_cli_result_t config_set(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                    void *context)
{
    (void)call;
    (void)context;
    CHECK(argc == 2);
    CHECK(strlen(argv[1]) < sizeof(last_config_value));
    snprintf(last_config_value, sizeof(last_config_value), "%s", argv[1]);
    return CRUX_CLI_OK;
}

static const crux_cli_command_t table[] = {
    CRUX_CLI_COMMAND("echo-args", "print arguments", CRUX_CLI_SESSION_ONLY, 0, 9, BOTH, echo_args,
                     NULL),
    CRUX_CLI_COMMAND("config set", "set a value", CRUX_CLI_SESSION_ONLY, 2, 2, BOTH, config_set,
                     NULL),
};

static void begin(void)
{
    cli_test_begin_table(table, sizeof(table) / sizeof(table[0]));
    echo_runs = 0;
}

/* Runs one input with echo off, and expects this exact output. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test's own helper.
static void expect_session(const char *input, const char *expected)
{
    begin();
    CHECK(cli_test_session(input, false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), expected);
}

/* The same with echo on. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test's own helper.
static void expect_echo(const char *input, const char *expected)
{
    begin();
    CHECK(cli_test_session(input, true, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), expected);
}

static void line_limit(void)
{
    static char input[CRUX_CLI_LINE_MAX + 64];
    static char expected[CRUX_CLI_LINE_MAX + 64];
    const char prefix[] = "echo-args ";
    size_t prefix_length = sizeof(prefix) - 1u;

    /* Exactly at the bound: runs. */
    memcpy(input, prefix, prefix_length);
    memset(input + prefix_length, 'x', CRUX_CLI_LINE_MAX - prefix_length);
    input[CRUX_CLI_LINE_MAX] = '\n';
    input[CRUX_CLI_LINE_MAX + 1] = '\0';
    memset(expected, 'x', sizeof(expected) - 1u);
    expected[0] = '>';
    expected[1] = ' ';
    expected[2] = '[';
    snprintf(expected + 3 + CRUX_CLI_LINE_MAX - prefix_length, 5, "]\n> ");
    expect_session(input, expected);
    CHECK(echo_runs == 1);

    /* One byte over: rejected as a whole, with nothing run. */
    input[CRUX_CLI_LINE_MAX] = 'x';
    input[CRUX_CLI_LINE_MAX + 1] = '\n';
    input[CRUX_CLI_LINE_MAX + 2] = '\0';
    expect_session(input, "> line too long\n> ");
    CHECK(echo_runs == 0);
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_LINE_REJECTED) == 1);
    CHECK(cli_fake_event(0)->outcome == CRUX_CLI_EVENT_LIMIT);

    /* Far over, and followed by a good line, which runs. */
    begin();
    for (unsigned index = 0; index < 5u * CRUX_CLI_LINE_MAX; ++index) {
        cli_fake_input(&cli_test_fake, "z");
    }
    CHECK(cli_test_session("\necho-args ok\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> line too long\n> [ok]\n> ");
    CHECK(echo_runs == 1);

    /* Backspace does not recover a line that already overflowed. */
    begin();
    for (unsigned index = 0; index < CRUX_CLI_LINE_MAX + 1u; ++index) {
        cli_fake_input(&cli_test_fake, "z");
    }
    CHECK(cli_test_session("\x7f\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> line too long\n> ");

    /* Line kill does, because it clears the line. */
    begin();
    for (unsigned index = 0; index < CRUX_CLI_LINE_MAX + 1u; ++index) {
        cli_fake_input(&cli_test_fake, "z");
    }
    CHECK(cli_test_session("\x15"
                           "echo-args ok\n",
                           false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> [ok]\n> ");

    /* Echo stops at the bound. */
    begin();
    for (unsigned index = 0; index < CRUX_CLI_LINE_MAX + 50u; ++index) {
        cli_fake_input(&cli_test_fake, "z");
    }
    CHECK(cli_test_session("\n", true, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    /* The prompt, the bound's worth of echo, the line end, the message and the prompt. */
    CHECK(cli_test_fake.output_length == 2u + CRUX_CLI_LINE_MAX + 1u + 14u + 2u);
}

static void token_limit(void)
{
    /* Nine arguments and the name make the limit of ten tokens. */
    expect_session("echo-args 1 2 3 4 5 6 7 8 9\n", "> [1][2][3][4][5][6][7][8][9]\n> ");
    CHECK(echo_runs == 1);
    expect_session("echo-args 1 2 3 4 5 6 7 8 9 10\n", "> invalid line\n> ");
    CHECK(echo_runs == 0);
    CHECK(cli_fake_event_count_of(CRUX_CLI_EVENT_LINE_REJECTED) == 1);
    CHECK(cli_fake_event(0)->outcome == CRUX_CLI_EVENT_LIMIT);
    /* Regardless of the command, and before any lookup. */
    expect_session("no-such 1 2 3 4 5 6 7 8 9 10\n", "> invalid line\n> ");
    CHECK(cli_fake_access.check_calls == 0);
}

static void quoting(void)
{
    expect_session("echo-args \"a b\"\n", "> [a b]\n> ");
    expect_session("echo-args \"a\\\"b\"\n", "> [a\"b]\n> ");
    expect_session("echo-args \"a\\\\b\"\n", "> [a\\b]\n> ");
    expect_session("echo-args \"\"\n", "> []\n> ");
    expect_session("echo-args \"\" x\n", "> [][x]\n> ");
    expect_session("echo-args a\\b\n", "> [a\\b]\n> ");
    expect_session("  echo-args   a    b  \n", "> [a][b]\n> ");
    /* Each of these is refused before any lookup, so a hidden name cannot show through. */
    const char *const refused[] = {
        "echo-args \"a\n",    "echo-args \"a\\\n",      "echo-args \"a\\nb\"\n", "echo-args a\"b\n",
        "echo-args \"a\"b\n", "echo-args \"a\"\"b\"\n", "no-such \"a\n",         "\"\n"};
    for (size_t index = 0; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        expect_session(refused[index], "> invalid line\n> ");
        CHECK(echo_runs == 0);
        CHECK(cli_fake_event(0)->outcome == CRUX_CLI_EVENT_INVALID);
    }
}

static void line_ends(void)
{
    /* CR, LF and CR LF each end one line, and LF CR is two line ends. */
    expect_session("echo-args a\r\necho-args b\n\recho-args c\r", "> [a]\n> [b]\n> > [c]\n> ");
    /* A CR LF split across reads is still one line end. */
    begin();
    cli_fake_input(&cli_test_fake, "echo-args a\r");
    cli_fake_input_timeouts(&cli_test_fake, 2);
    cli_fake_input(&cli_test_fake, "\necho-args b\n");
    CHECK(cli_test_session_scripted(false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> [a]\n> [b]\n> ");
    /* An empty line does nothing, and is not an event. */
    expect_session("\n\n", "> > > ");
    CHECK(cli_fake_event_count() == 0);
}

static void editing(void)
{
    /* Backspace at the start echoes nothing. */
    expect_echo("\x08\x7f\n", "> \n> ");
    expect_echo("\x08"
                "a\x7f\n",
                "> a\b \b\n> ");
    /* Backspace and DEL each remove a byte. */
    expect_session("echo-args ab\x08\x7f"
                   "c\n",
                   "> [c]\n> ");
    /* Ctrl-U clears, and the terminal is erased. */
    expect_echo("abc\x15\n", "> abc\b \b\b \b\b \b\n> ");
    expect_session("no-such\x15"
                   "echo-args z\n",
                   "> [z]\n> ");
    /* Ctrl-C clears, runs nothing and prompts again. */
    expect_echo("abc\x03"
                "echo-args z\n",
                "> abc^C\n> echo-args z\n[z]\n> ");
    expect_session("echo-args a\x03\n", "> > > ");
    CHECK(echo_runs == 0);
    CHECK(cli_fake_event_count() == 0);
}

static void controls_and_bytes(void)
{
    /* Other C0 controls, including NUL and TAB, are dropped. */
    for (unsigned byte = 0; byte < 0x20u; ++byte) {
        if (byte == '\r' || byte == '\n' || byte == 0x08u || byte == 0x03u || byte == 0x15u ||
            byte == 0x1Bu) {
            continue;
        }
        char input[] = "echo-args a?b\n";
        input[11] = (char)byte;
        begin();
        cli_fake_input_bytes(&cli_test_fake, input, sizeof(input) - 1u);
        CHECK(cli_test_session_scripted(true, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
        CHECK_TEXT(cli_test_output(), "> echo-args ab\n[ab]\n> ");
    }
    /* Bytes above 0x7F only inside quotes, and passed on unchanged. */
    expect_session("echo-args \xC3\xA9\n", "> invalid line\n> ");
    CHECK(echo_runs == 0);
    expect_session("\xC3\xA9\n", "> invalid line\n> ");
    expect_session("echo-args \"\xC3\xA9\"\n", "> [\xC3\xA9]\n> ");
    expect_session("echo-args \"\xFF\x80\"\n", "> [\xFF\x80]\n> ");
}

static void escape_sequences(void)
{
    /* Arrow keys and other sequences are dropped whole. */
    const char *const dropped[] = {
        "\x1b[A",     "\x1b[1;5C", "\x1bOA",           "\x1b[?25l",          "\x1bx",
        "\x1b\x1b[A", "\x1b(B",    "\x1b]0;title\x07", "\x1b]0;title\x1b\\", "\x1bPdata\x1b\\"};
    for (size_t index = 0; index < sizeof(dropped) / sizeof(dropped[0]); ++index) {
        begin();
        cli_fake_input(&cli_test_fake, "echo-args a");
        cli_fake_input(&cli_test_fake, dropped[index]);
        cli_fake_input(&cli_test_fake, "b\n");
        CHECK(cli_test_session_scripted(true, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
        CHECK_TEXT(cli_test_output(), "> echo-args ab\n[ab]\n> ");
    }
    /* Split across reads. */
    begin();
    cli_fake_input(&cli_test_fake, "echo-args a\x1b");
    cli_fake_input_timeouts(&cli_test_fake, 1);
    cli_fake_input(&cli_test_fake, "[");
    cli_fake_input_timeouts(&cli_test_fake, 1);
    cli_fake_input(&cli_test_fake, "1;5");
    cli_fake_input_timeouts(&cli_test_fake, 1);
    cli_fake_input(&cli_test_fake, "Cb\n");
    CHECK(cli_test_session_scripted(false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> [ab]\n> ");
    /* A line end or an interrupt inside a sequence still acts. */
    expect_session("echo-args a\x1b[\recho-args b\x1b\necho-args c\x1b]0;x\n",
                   "> [a]\n> [b]\n> [c]\n> ");
    expect_session("echo-args a\x1b[\x03\n", "> > > ");
    /* The state machine is bounded: past the bound, bytes are ordinary input again. */
    begin();
    cli_fake_input(&cli_test_fake, "echo-args \x1b[");
    for (unsigned index = 0; index < 40u; ++index) {
        cli_fake_input(&cli_test_fake, "1");
    }
    CHECK(cli_test_session("\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> [111111111]\n> ");
    /* A string sequence cannot swallow the session's input for ever. */
    begin();
    cli_fake_input(&cli_test_fake, "echo-args \x1b]");
    for (unsigned index = 0; index < 40u; ++index) {
        cli_fake_input(&cli_test_fake, "1");
    }
    CHECK(cli_test_session("\n", false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> [111111111]\n> ");
}

static void longest_config_set_fits(void)
{
    /* The proposal's sizing: a 31-byte name and a 128-byte value with every byte escaped. */
    static char line[CRUX_CLI_LINE_MAX + 2];
    size_t length = 0;
    memcpy(line, "config set ", 11);
    length = 11;
    memset(line + length, 'n', 31);
    length += 31;
    memcpy(line + length, " \"", 2);
    length += 2;
    for (unsigned index = 0; index < 128u; ++index) {
        memcpy(line + length, "\\\"", 2);
        length += 2;
    }
    line[length] = '"';
    length += 1;
    CHECK(length == 301);
    CHECK(length <= CRUX_CLI_LINE_MAX);
    line[length] = '\n';
    line[length + 1] = '\0';
    begin();
    last_config_value[0] = '\0';
    CHECK(cli_test_session(line, false, CRUX_ACCESS_CHANNEL_SERIAL) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> > ");
    CHECK(strlen(last_config_value) == 128);
    CHECK(last_config_value[0] == '"' && last_config_value[127] == '"');
}

static void buffer_is_cleared(void)
{
    /* What was typed does not stay in the line buffer after it was handled. */
    expect_session("echo-args remembered\nno-such typed-secret\n",
                   "> [remembered]\n> unknown command\n> ");
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "remembered"));
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "typed-secret"));
    /* That includes a line abandoned when the transport ended. */
    expect_session("half-typed-secret", "> ");
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "half-typed-secret"));
    /* And a line stopped with Ctrl-C. */
    expect_session("stopped-secret\x03", "> > ");
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "stopped-secret"));
    int status = -1;
    begin();
    CHECK(cli_test_exec("echo-args exec-secret", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "exec-secret"));
}

static void exec_form(void)
{
    int status = -1;
    begin();
    CHECK(cli_test_exec("echo-args a", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK_TEXT(cli_test_output(), "[a]\n");
    /* The line need not be terminated. */
    static const char unterminated[] = {'e', 'c', 'h', 'o', '-', 'a', 'r', 'g', 's'};
    begin();
    CHECK(cli_test_exec_bytes(unterminated, sizeof(unterminated), CRUX_ACCESS_CHANNEL_SSH,
                              &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK_TEXT(cli_test_output(), "\n");
    /* Nothing edits an exec line, so a control byte is refused, not dropped. */
    const char *const refused[] = {"echo-args a\n", "echo-args a\r", "echo-args\x7f",
                                   "echo-args \x1b[A", "echo-args\t"};
    for (size_t index = 0; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        begin();
        CHECK(cli_test_exec(refused[index], CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
        CHECK(status == 2);
        CHECK_TEXT(cli_test_output(), "invalid line\n");
        CHECK(echo_runs == 0);
    }
    static const char embedded_nul[] = {'e', 'c', 'h', 'o', '\0', 'x'};
    begin();
    CHECK(cli_test_exec_bytes(embedded_nul, sizeof(embedded_nul), CRUX_ACCESS_CHANNEL_SSH,
                              &status) == CRUX_CLI_OK);
    CHECK(status == 2);
    CHECK_TEXT(cli_test_output(), "invalid line\n");
    /* At and over the bound. */
    static char line[CRUX_CLI_LINE_MAX + 2];
    memset(line, 'x', sizeof(line));
    snprintf(line, 11, "echo-args ");
    line[10] = 'x';
    begin();
    CHECK(cli_test_exec_bytes(line, CRUX_CLI_LINE_MAX, CRUX_ACCESS_CHANNEL_SSH, &status) ==
          CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK(echo_runs == 1);
    begin();
    CHECK(cli_test_exec_bytes(line, CRUX_CLI_LINE_MAX + 1u, CRUX_ACCESS_CHANNEL_SSH, &status) ==
          CRUX_CLI_OK);
    CHECK(status == 2);
    CHECK_TEXT(cli_test_output(), "line too long\n");
    CHECK(echo_runs == 0);
    /* Empty. */
    begin();
    CHECK(cli_test_exec_bytes(NULL, 0, CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK_TEXT(cli_test_output(), "");
    begin();
    CHECK(cli_test_exec_bytes(NULL, 3, CRUX_ACCESS_CHANNEL_SSH, &status) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
}

static void argument_checks(void)
{
    crux_cli_io_t io = cli_fake_io(&cli_test_fake, false);
    crux_cli_io_t no_read = {NULL, cli_fake_write, &cli_test_fake, false};
    crux_cli_io_t no_write = {cli_fake_read, NULL, &cli_test_fake, false};
    crux_access_session_t session = CLI_FAKE_ACCESS_SESSION;
    int status = -1;
    begin();
    CHECK(crux_cli_run(NULL, &io, CRUX_ACCESS_CHANNEL_SERIAL, session) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_run(&cli_test_context, NULL, CRUX_ACCESS_CHANNEL_SERIAL, session) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_run(&cli_test_context, &no_read, CRUX_ACCESS_CHANNEL_SERIAL, session) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_run(&cli_test_context, &no_write, CRUX_ACCESS_CHANNEL_SERIAL, session) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_run(&cli_test_context, &io, CRUX_ACCESS_CHANNEL_WEB, session) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_exec(NULL, &io, CRUX_ACCESS_CHANNEL_SSH, session, "a", 1, &status) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(status == 1);
    CHECK(crux_cli_exec(&cli_test_context, &io, CRUX_ACCESS_CHANNEL_SSH, session, "a", 1, NULL) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_exec(&cli_test_context, &no_write, CRUX_ACCESS_CHANNEL_SSH, session, "a", 1,
                        &status) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_exec(&cli_test_context, &io, CRUX_ACCESS_CHANNEL_WEB, session, "a", 1,
                        &status) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    /* exec never reads, so it needs no read function. */
    CHECK(crux_cli_exec(&cli_test_context, &no_read, CRUX_ACCESS_CHANNEL_SSH, session, "echo-args",
                        9, &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK(cli_test_fake.read_calls == 0);
}

int main(void)
{
    line_limit();
    token_limit();
    quoting();
    line_ends();
    editing();
    controls_and_bytes();
    escape_sequences();
    longest_config_set_fits();
    buffer_is_cleared();
    exec_form();
    argument_checks();
    return cli_test_finish("cli parser");
}
