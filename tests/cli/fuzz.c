/* Random and mutated input through the line editor, the tokeniser and the login loop, from a fixed
 * seed so that a failure repeats. Run under sanitizers, this looks for memory errors and for any
 * way past the registry: a hidden command must never run, and no handler may see arguments outside
 * its declared bounds. */
#include "cli_console.h"
#include "cli_session_test.h"

#define BOTH (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)
#define ROUNDS 4000u

static unsigned hidden_runs;

static crux_cli_result_t run_hidden(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                    void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    (void)context;
    hidden_runs += 1;
    return CRUX_CLI_OK;
}

static crux_cli_result_t run_bounded(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                     void *context)
{
    (void)context;
    CHECK(argc >= 1 && argc <= 3);
    for (size_t index = 0; index < argc; ++index) {
        CHECK(argv[index] != NULL);
        size_t length = strlen(argv[index]);
        CHECK(length <= CRUX_CLI_LINE_MAX);
        (void)crux_cli_write(call, argv[index], length);
    }
    return CRUX_CLI_OK;
}

static crux_cli_result_t run_quiet(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                   void *context)
{
    (void)call;
    (void)argc;
    (void)argv;
    (void)context;
    return CRUX_CLI_OK;
}

static const crux_cli_command_t table[] = {
    CRUX_CLI_COMMAND("bounded", "one to three arguments", CRUX_CLI_SESSION_ONLY, 1, 3, BOTH,
                     run_bounded, NULL),
    CRUX_CLI_COMMAND("two word", "two words", CRUX_CLI_SESSION_ONLY, 1, 3, BOTH, run_bounded, NULL),
    CRUX_CLI_COMMAND("quiet", "no output", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, run_quiet, NULL),
    CRUX_CLI_COMMAND("secret-cmd", "a permission nobody holds", CRUX_CLI_PERMISSION("lacked"), 0, 9,
                     BOTH, run_hidden, NULL),
    CRUX_CLI_COMMAND("two secret", "a permission nobody holds", CRUX_CLI_PERMISSION("lacked"), 0, 8,
                     BOTH, run_hidden, NULL),
};

static uint32_t random_state = 0x181C11u;

/* xorshift32, so that a failure repeats on any host. */
static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static const char *const fragments[] = {
    "bounded ", "two word ", "two secret ", "secret-cmd ", "quiet", "help ",  "help",
    "logout",   "passwd",    "\"",          "\\\"",        "\\\\",  "\\n",    " ",
    "  ",       "\r",        "\n",          "\r\n",        "\x08",  "\x7f",   "\x15",
    "\x03",     "\x1b",      "\x1b[",       "\x1b[1;5C",   "\x1bO", "\x1b]0", "\x07",
    "\x1b\\",   "\xc3\xa9",  "\xff",        "!setup",      "a",     "bb",     "ccc",
};

/* Fills the buffer with either bytes of any value, or fragments that reach deeper states. */
static size_t random_input(uint8_t *buffer, size_t capacity)
{
    size_t length = 0;
    size_t target = next_random() % capacity;
    if (next_random() % 3u == 0) {
        while (length < target) {
            buffer[length] = (uint8_t)next_random();
            length += 1;
        }
        return length;
    }
    while (length < target) {
        const char *fragment =
            fragments[next_random() % (sizeof(fragments) / sizeof(fragments[0]))];
        size_t size = strlen(fragment);
        if (size > capacity - length) {
            break;
        }
        for (size_t index = 0; index < size; ++index) {
            buffer[length] = (uint8_t)fragment[index];
            length += 1;
        }
    }
    return length;
}

static uint8_t input[3000];

static void begin_round(void)
{
    cli_fake_reset(&cli_test_fake);
    cli_fake_events_clear();
    /* Keep the scripted answers and the registry. Only the counters may grow. */
    cli_fake_access.check_denials = 0;
}

static void sessions(void)
{
    for (unsigned round = 0; round < ROUNDS; ++round) {
        begin_round();
        size_t length = random_input(input, sizeof(input));
        cli_fake_input_bytes(&cli_test_fake, input, length);
        (void)cli_test_session_scripted((next_random() & 1u) != 0, (next_random() & 1u) != 0
                                                                       ? CRUX_ACCESS_CHANNEL_SERIAL
                                                                       : CRUX_ACCESS_CHANNEL_SSH);
        CHECK(hidden_runs == 0);
    }
}

static void exec_lines(void)
{
    for (unsigned round = 0; round < ROUNDS; ++round) {
        begin_round();
        size_t length = random_input(input, (size_t)2u * CRUX_CLI_LINE_MAX);
        int status = -1;
        crux_cli_result_t result =
            cli_test_exec_bytes((const char *)input, length, CRUX_ACCESS_CHANNEL_SSH, &status);
        CHECK(result == CRUX_CLI_OK || result == CRUX_CLI_ERR_OUTPUT);
        CHECK(status == 0 || status == 1 || status == 2 || status == 127);
        CHECK(hidden_runs == 0);
    }
}

static void every_single_byte_change(void)
{
    /* Each position of a valid line, set to each byte value, with no way past the rules. */
    static const char line[] = "two secret \"a b\" c\\d";
    size_t length = sizeof(line) - 1u;
    for (size_t position = 0; position < length; ++position) {
        for (unsigned value = 0; value < 256u; ++value) {
            begin_round();
            memcpy(input, line, length);
            input[position] = (uint8_t)value;
            int status = -1;
            (void)cli_test_exec_bytes((const char *)input, length, CRUX_ACCESS_CHANNEL_SSH,
                                      &status);
            CHECK(hidden_runs == 0);
            begin_round();
            cli_fake_input_bytes(&cli_test_fake, input, length);
            cli_fake_input(&cli_test_fake, "\n");
            (void)cli_test_session_scripted(false, CRUX_ACCESS_CHANNEL_SERIAL);
            CHECK(hidden_runs == 0);
        }
    }
}

static void login_loop(void)
{
    /* Random input at the login and setup prompts, with every login answered as denied. */
    cli_fake_access.login_result = CRUX_ACCESS_ERR_DENIED;
    cli_fake_access.provision_result = CRUX_ACCESS_ERR_DENIED;
    for (unsigned round = 0; round < ROUNDS; ++round) {
        begin_round();
        size_t length = random_input(input, sizeof(input));
        cli_fake_input_bytes(&cli_test_fake, input, length);
        crux_cli_io_t io = cli_fake_io(&cli_test_fake, true);
        CHECK(crux_cli_console_serve(&cli_test_context, &io, "administrator") ==
              CRUX_CLI_ERR_OUTPUT);
        CHECK(hidden_runs == 0);
    }
    CHECK(cli_fake_access.login_calls > 0);
}

int main(void)
{
    cli_test_begin_table(table, sizeof(table) / sizeof(table[0]));
    sessions();
    exec_lines();
    every_single_byte_change();
    login_loop();
    return cli_test_finish("cli fuzz");
}
