/* The console's login and setup tables, row by row, against the scripted access control. */
#include "cli_console.h"
#include "cli_session_test.h"

#define SETUP_ROLE "administrator"
#define PASSWORD "correct-horse-password"
#define OTHER_PASSWORD "another-long-password"

static bool context_clean_at_verify;

/* Runs inside the access call: the password is in its buffer, and the name in the line. */
static void note_context(void *context)
{
    (void)context;
    context_clean_at_verify =
        !cli_test_contains(cli_test_context.line, sizeof(cli_test_context.line), PASSWORD);
}

static void begin(void)
{
    cli_test_begin();
    CHECK(crux_cli_init(NULL, NULL, 0) == CRUX_CLI_OK);
    cli_fake_access.on_verify = note_context;
    context_clean_at_verify = false;
}

static crux_cli_result_t serve(void)
{
    crux_cli_io_t io = cli_fake_io(&cli_test_fake, true);
    return crux_cli_console_serve(&cli_test_context, &io, SETUP_ROLE);
}

/* Logs in as alice with the scripted result, and requires this output. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test's own helper.
static void expect_login_row(crux_access_result_t result, uint32_t retry_after_ms,
                             const char *expected)
{
    static char full[512];
    begin();
    cli_fake_access.login_result = result;
    cli_fake_access.login_retry_after_ms = retry_after_ms;
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    snprintf(full, sizeof(full), "login: alice\npassword: \n%slogin: ", expected);
    CHECK_TEXT(cli_test_output(), full);
    CHECK(cli_fake_access.login_calls == 1);
    CHECK_TEXT(cli_fake_access.last_user, "alice");
    CHECK_TEXT(cli_fake_access.last_password, PASSWORD);
    CHECK(cli_fake_access.last_length == strlen(PASSWORD));
    /* Not a session, so nothing to end. */
    CHECK(cli_fake_access.logout_calls == 0);
    /* Serial's origin, with no source. */
    CHECK(cli_fake_access.last_origin.channel == CRUX_ACCESS_CHANNEL_SERIAL);
    CHECK(cli_fake_access.last_origin.source_length == 0);
    /* The secret never reached the line buffer, and is cleared from everywhere now. */
    CHECK(context_clean_at_verify);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "alice"));
    CHECK(!cli_test_contains(cli_test_fake.output, cli_test_fake.output_length, PASSWORD));
    /* The console reports only its own failures. Access reports the rest. */
    CHECK(cli_fake_event_count() == 0);
}

static void login_table(void)
{
    expect_login_row(CRUX_ACCESS_ERR_DENIED, 0, "login failed\n");
    expect_login_row(CRUX_ACCESS_ERR_INVALID_ARGUMENT, 0, "login failed\n");
    expect_login_row(CRUX_ACCESS_ERR_RETRY_LATER, 1, "try again in 1 s\n");
    expect_login_row(CRUX_ACCESS_ERR_RETRY_LATER, 1000, "try again in 1 s\n");
    expect_login_row(CRUX_ACCESS_ERR_RETRY_LATER, 1001, "try again in 2 s\n");
    expect_login_row(CRUX_ACCESS_ERR_RETRY_LATER, 60000, "try again in 60 s\n");
    expect_login_row(CRUX_ACCESS_ERR_RETRY_LATER, 4294967295u, "try again in 4294968 s\n");
    expect_login_row(CRUX_ACCESS_ERR_BUSY, 0, "busy, try again\n");
    expect_login_row(CRUX_ACCESS_ERR_NO_RESOURCES, 0, "no session available\n");
    expect_login_row(CRUX_ACCESS_ERR_NOT_READY, 0, "login unavailable\n");
    /* A result the table has no row for gives away nothing. */
    expect_login_row(CRUX_ACCESS_ERR_STORE, 0, "login failed\n");

    /* Success: a session runs, and the console ends it afterwards. */
    begin();
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD "\nlogout\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: alice\npassword: \n> logout\nlogin: ");
    CHECK(cli_fake_access.logout_calls == 1);
    CHECK(cli_fake_access.last_logout.value == CLI_FAKE_ACCESS_SESSION.value);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));

    /* Success with a must-change notice. */
    begin();
    cli_fake_access.must_change = true;
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD "\nlogout\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: alice\npassword: \n"
                                  "password change required, use passwd\n> logout\nlogin: ");

    /* A session the access control ends: back to the prompt, and the reference is cleared. */
    begin();
    cli_fake_access.touch_result = CRUX_ACCESS_ERR_SESSION_INVALID;
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD "\nhelp\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: alice\npassword: \n> help\nsession ended\nlogin: ");
    CHECK(cli_fake_access.logout_calls == 1);

    /* The transport fails during a session: the session is ended before serve returns. */
    begin();
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.logout_calls == 1);
    CHECK(cli_fake_access.last_logout.value == CLI_FAKE_ACCESS_SESSION.value);
}

static void login_details(void)
{
    /* Nothing about the device appears before login. */
    begin();
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: ");
    CHECK(cli_fake_access.login_calls == 0);

    /* A password is not echoed, and neither are its edits. */
    begin();
    cli_fake_input(&cli_test_fake, "alice\nxx\x7f\x15" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: alice\npassword: \n> ");
    CHECK_TEXT(cli_fake_access.last_password, PASSWORD);

    /* A terminal that sends CR LF gets no extra prompt, across a password and into the session. */
    begin();
    cli_fake_input(&cli_test_fake, "alice\r\n" PASSWORD "\r\nlogout\r\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: alice\npassword: \n> logout\nlogin: ");

    /* An empty name asks again, with no password prompt and no attempt. */
    begin();
    cli_fake_input(&cli_test_fake, "\n\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: \nlogin: \nlogin: ");
    CHECK(cli_fake_access.login_calls == 0);

    /* A name that fails the alphabet still reaches the password prompt. */
    begin();
    cli_fake_access.login_result = CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    cli_fake_input(&cli_test_fake, "Not A Name!\n" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: Not A Name!\npassword: \nlogin failed\nlogin: ");
    CHECK_TEXT(cli_fake_access.last_user, "Not A Name!");

    /* An overlong name does too, and reaches access control as a name no account can hold. */
    begin();
    cli_fake_access.login_result = CRUX_ACCESS_ERR_INVALID_ARGUMENT;
    for (unsigned index = 0; index < CRUX_CLI_LINE_MAX + 10u; ++index) {
        cli_fake_input(&cli_test_fake, "n");
    }
    cli_fake_input(&cli_test_fake, "\n" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.login_calls == 1);
    CHECK_TEXT(cli_fake_access.last_user, "");
    CHECK(strstr(cli_test_output(), "login failed\n") != NULL);

    /* An overlong password is denied by length, with its bytes already gone. */
    begin();
    for (unsigned index = 0; index < 6u; ++index) {
        cli_fake_input(&cli_test_fake, "x");
    }
    cli_fake_input(&cli_test_fake, "\n");
    for (unsigned index = 0; index < CRUX_ACCESS_PASSWORD_MAX + 40u; ++index) {
        cli_fake_input(&cli_test_fake, "P");
    }
    cli_fake_access.login_result = CRUX_ACCESS_ERR_DENIED;
    cli_fake_input(&cli_test_fake, "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.login_calls == 1);
    CHECK(cli_fake_access.last_length == CRUX_ACCESS_PASSWORD_MAX + 1u);
    CHECK(cli_fake_access.last_password[0] == '\0');
    CHECK(strstr(cli_test_output(), "login failed\n") != NULL);
    /* At the limit it is an ordinary attempt. */
    begin();
    cli_fake_input(&cli_test_fake, "alice\n");
    for (unsigned index = 0; index < CRUX_ACCESS_PASSWORD_MAX; ++index) {
        cli_fake_input(&cli_test_fake, "P");
    }
    cli_fake_access.login_result = CRUX_ACCESS_ERR_DENIED;
    cli_fake_input(&cli_test_fake, "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.last_length == CRUX_ACCESS_PASSWORD_MAX);
    CHECK(cli_fake_access.last_password[0] == 'P');

    /* Ctrl-C stops either prompt with no attempt. */
    begin();
    cli_fake_input(&cli_test_fake, "ali\x03");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: ali^C\nlogin: ");
    begin();
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD "\x03");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: alice\npassword: ^C\nlogin: ");
    CHECK(cli_fake_access.login_calls == 0);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));

    /* An abandoned password prompt gives up, and what was typed goes with it. */
    begin();
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD);
    cli_fake_input_timeouts(&cli_test_fake, CRUX_CLI_SECRET_IDLE_POLLS);
    cli_fake_input(&cli_test_fake, "tail\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.login_calls == 0);
    CHECK(cli_test_fake.read_timeouts_seen == CRUX_CLI_SECRET_IDLE_POLLS);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));
    CHECK(strstr(cli_test_output(), "login: alice\npassword: \nlogin: tail\npassword: ") != NULL);

    /* Waiting at the name prompt has no limit. */
    begin();
    cli_fake_input_timeouts(&cli_test_fake, 5u * CRUX_CLI_SECRET_IDLE_POLLS);
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_test_fake.read_timeouts_seen == 5u * CRUX_CLI_SECRET_IDLE_POLLS);
    CHECK_TEXT(cli_test_output(), "login: ");

    /* The transport ends at the password prompt, and the typed bytes are cleared. */
    begin();
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD);
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.login_calls == 0);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));

    /* A registry that is not ready: no attempt. */
    cli_test_begin();
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: alice\npassword: \nlogin unavailable\nlogin: ");
    CHECK(cli_fake_access.login_calls == 0);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));

    /* A stuck client at the prompt ends the cycle after a bounded number of attempts. */
    begin();
    cli_test_fake.write_stuck = true;
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_test_fake.write_timeouts_seen == CRUX_CLI_WRITE_ATTEMPTS_NO_SESSION);

    /* A broken write at each step ends it too. */
    const size_t failures[] = {0, 3, 7, 16};
    for (size_t index = 0; index < 4; ++index) {
        begin();
        cli_test_fake.write_fail_after = failures[index];
        cli_fake_input(&cli_test_fake, "alice\n" PASSWORD "\n");
        CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    }

    /* Arguments. */
    crux_cli_io_t io = cli_fake_io(&cli_test_fake, true);
    CHECK(crux_cli_console_serve(NULL, &io, SETUP_ROLE) == CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_console_serve(&cli_test_context, NULL, SETUP_ROLE) ==
          CRUX_CLI_ERR_INVALID_ARGUMENT);
    CHECK(crux_cli_console_serve(&cli_test_context, &io, NULL) == CRUX_CLI_ERR_INVALID_ARGUMENT);
}

/* Runs the setup dialogue with the scripted result, and requires this output after it. */
static void expect_setup_row(crux_access_result_t result, const char *tail, bool again)
{
    static char full[512];
    begin();
    cli_fake_access.provision_result = result;
    cli_fake_input(&cli_test_fake, "!setup\nfirst\n" PASSWORD "\n" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    snprintf(full, sizeof(full), "login: !setup\nnew user: first\npassword: \nrepeat: \n%s%s", tail,
             again ? "new user: " : "login: ");
    CHECK_TEXT(cli_test_output(), full);
    CHECK(cli_fake_access.provision_calls == 1);
    CHECK_TEXT(cli_fake_access.last_user, "first");
    CHECK_TEXT(cli_fake_access.last_password, PASSWORD);
    CHECK(cli_fake_access.last_length == strlen(PASSWORD));
    CHECK_TEXT(cli_fake_access.last_role, SETUP_ROLE);
    CHECK(cli_fake_access.last_origin.channel == CRUX_ACCESS_CHANNEL_SERIAL);
    CHECK(cli_fake_access.last_origin.source_length == 0);
    /* Setup opens no session. */
    CHECK(cli_fake_access.login_calls == 0);
    CHECK(cli_fake_access.touch_calls == 0);
    CHECK(context_clean_at_verify);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "first"));
    CHECK(!cli_test_contains(cli_test_fake.output, cli_test_fake.output_length, PASSWORD));
    CHECK(cli_fake_event_count() == 0);
}

static void setup_table(void)
{
    expect_setup_row(CRUX_ACCESS_OK, "account created\n", false);
    expect_setup_row(CRUX_ACCESS_ERR_DENIED, "setup not available\n", false);
    expect_setup_row(CRUX_ACCESS_ERR_PASSWORD_REJECTED, "password not accepted\n", true);
    expect_setup_row(CRUX_ACCESS_ERR_INVALID_ARGUMENT, "invalid name\n", true);
    expect_setup_row(CRUX_ACCESS_ERR_STORE, "could not save, a later boot may use either\n", false);
    expect_setup_row(CRUX_ACCESS_ERR_NOT_READY, "login unavailable\n", false);
    /* A result the table has no row for gives away nothing. */
    expect_setup_row(CRUX_ACCESS_ERR_BUSY, "setup not available\n", false);

    /* The rule's text follows the fixed words when one is configured. */
    begin();
    CHECK(crux_cli_set_password_rule("use 12 to 128 bytes") == CRUX_CLI_OK);
    cli_fake_access.provision_result = CRUX_ACCESS_ERR_PASSWORD_REJECTED;
    cli_fake_input(&cli_test_fake, "!setup\nfirst\n" PASSWORD "\n" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(strstr(cli_test_output(), "password not accepted: use 12 to 128 bytes\nnew user: ") !=
          NULL);

    /* Two entries that differ make no call, and ask again. */
    begin();
    cli_fake_input(&cli_test_fake, "!setup\nfirst\n" PASSWORD "\n" OTHER_PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(
        cli_test_output(),
        "login: !setup\nnew user: first\npassword: \nrepeat: \npasswords differ\nnew user: ");
    CHECK(cli_fake_access.provision_calls == 0);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), OTHER_PASSWORD));

    /* Corrected, it goes through. */
    begin();
    cli_fake_input(&cli_test_fake, "!setup\nfirst\n" PASSWORD "\n" OTHER_PASSWORD "\n");
    cli_fake_input(&cli_test_fake, "first\n" PASSWORD "\n" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.provision_calls == 1);
    CHECK(strstr(cli_test_output(), "account created\nlogin: ") != NULL);

    /* An overlong entry is refused with no call. */
    begin();
    cli_fake_input(&cli_test_fake, "!setup\nfirst\n");
    for (unsigned index = 0; index < CRUX_ACCESS_PASSWORD_MAX + 1u; ++index) {
        cli_fake_input(&cli_test_fake, "P");
    }
    cli_fake_input(&cli_test_fake, "\n\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.provision_calls == 0);
    CHECK(strstr(cli_test_output(), "password not accepted\nnew user: ") != NULL);
    /* An empty name too, with no password prompts. */
    begin();
    cli_fake_input(&cli_test_fake, "!setup\n\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "login: !setup\nnew user: \ninvalid name\nnew user: ");
    /* An overlong name reaches the same answer as a bad one, and asks again. */
    begin();
    cli_fake_input(&cli_test_fake, "!setup\n");
    for (unsigned index = 0; index < CRUX_CLI_LINE_MAX + 1u; ++index) {
        cli_fake_input(&cli_test_fake, "n");
    }
    cli_fake_input(&cli_test_fake, "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.provision_calls == 0);
    CHECK(strstr(cli_test_output(), "invalid name\nnew user: ") != NULL);

    /* Ctrl-C at any step goes back to the login prompt with no call. */
    const char *const stops[] = {"!setup\nfir\x03", "!setup\nfirst\npass\x03",
                                 "!setup\nfirst\npass\npa\x03"};
    for (size_t index = 0; index < 3; ++index) {
        begin();
        cli_fake_input(&cli_test_fake, stops[index]);
        CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
        CHECK(cli_fake_access.provision_calls == 0);
        /* The final prompt is the login prompt. */
        size_t length = cli_test_fake.output_length;
        CHECK(length >= 7 && strcmp(cli_test_fake.output + length - 7, "login: ") == 0);
        CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), "pass"));
    }

    /* A transport that ends in the dialogue ends the cycle with everything cleared. */
    const char *const ends[] = {"!setup\n", "!setup\nfirst\n", "!setup\nfirst\n" PASSWORD "\n",
                                "!setup\nfirst\n" PASSWORD "\n" PASSWORD};
    for (size_t index = 0; index < 4; ++index) {
        begin();
        cli_fake_input(&cli_test_fake, ends[index]);
        CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
        CHECK(cli_fake_access.provision_calls == 0);
        CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));
    }

    /* Abandoned at a password step, the dialogue goes back to the login prompt. */
    begin();
    cli_fake_input(&cli_test_fake, "!setup\nfirst\n" PASSWORD);
    cli_fake_input_timeouts(&cli_test_fake, CRUX_CLI_SECRET_IDLE_POLLS);
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(cli_fake_access.provision_calls == 0);
    CHECK(!cli_test_contains(&cli_test_context, sizeof(cli_test_context), PASSWORD));
    size_t length = cli_test_fake.output_length;
    CHECK(strcmp(cli_test_fake.output + length - 7, "login: ") == 0);

    /* The prompt announces nothing. */
    begin();
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(strstr(cli_test_output(), "setup") == NULL);

    /* Only the exact word enters setup. It is not an account, and not a login attempt. */
    const char *const not_setup[] = {"!setup \n", " !setup\n", "!SETUP\n", "!setupx\n", "!\n"};
    for (size_t index = 0; index < sizeof(not_setup) / sizeof(not_setup[0]); ++index) {
        begin();
        cli_fake_input(&cli_test_fake, not_setup[index]);
        CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
        CHECK(strstr(cli_test_output(), "password: ") != NULL);
        CHECK(strstr(cli_test_output(), "new user") == NULL);
    }
    /* Inside a session it means nothing, and setup cannot be reached from there. */
    begin();
    cli_fake_input(&cli_test_fake, "alice\n" PASSWORD "\n!setup\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(strstr(cli_test_output(), "> !setup\nunknown command\n> ") != NULL);
    CHECK(strstr(cli_test_output(), "new user") == NULL);
    CHECK(cli_fake_access.provision_calls == 0);
}

int main(void)
{
    login_table();
    login_details();
    setup_table();
    return cli_test_finish("cli login");
}
