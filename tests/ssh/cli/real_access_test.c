/* The shell and exec entry points through the fake provider, on the real crux_access core with its
 * fake ports: permissions, the denial events, a forced change, expiry, and which session a
 * connection speaks for. */
#include "access_test.h"
#include "crux_cli_adapter.h"
#include "fake_events.h"
#include "fake_transport.h"
#include "ssh_cli_harness.h"

#define ADMIN ACCESS_TEST_ADMIN
#define USER ACCESS_TEST_USER
#define PASSWORD ACCESS_TEST_PASSWORD
#define NEW_PASSWORD ACCESS_TEST_OTHER_PASSWORD

#define CHECK_TEXT(actual, expected)                                                               \
    do {                                                                                           \
        ++access_test_checks;                                                                      \
        const char *ssh_actual = (actual);                                                         \
        const char *ssh_expected = (expected);                                                     \
        if (strcmp(ssh_actual, ssh_expected) != 0) {                                               \
            printf("FAIL %s:%d:\n  got      [%s]\n  expected [%s]\n", __FILE__, __LINE__,          \
                   ssh_actual, ssh_expected);                                                      \
            ++access_test_failures;                                                                \
        }                                                                                          \
    } while (0)

void crux_cli_test_reset(void);

static unsigned handler_runs;
static crux_cli_context_t serial_context;

static crux_cli_result_t run_command(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                     void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    handler_runs += 1;
    return crux_cli_write(call, "ran\n", 4);
}

#define BOTH (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)
static const crux_cli_command_t commands[] = {
    CRUX_CLI_COMMAND("any", "any session", CRUX_CLI_SESSION_ONLY, 0, 0, BOTH, run_command, NULL),
    CRUX_CLI_COMMAND("write", "operators and above", CRUX_CLI_PERMISSION("config.write"), 0, 0,
                     BOTH, run_command, NULL),
    CRUX_CLI_COMMAND("manage", "administrators", CRUX_CLI_PERMISSION(CRUX_ACCESS_PERMISSION_MANAGE),
                     0, 0, BOTH, run_command, NULL),
};

static const crux_access_policy_t *policy(void)
{
    return &access_test_profile_reference.policy;
}

static crux_access_session_t begin(void)
{
    static const crux_cli_command_t *const tables[] = {commands};
    static const size_t counts[] = {sizeof(commands) / sizeof(commands[0])};
    crux_cli_test_reset();
    cli_fake_events_clear();
    access_test_boot_with_admin();
    access_fake_events_clear();
    CHECK(crux_cli_init(tables, counts, 1) == CRUX_CLI_OK);
    handler_runs = 0;
    CHECK(ssh_cli_begin());
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_ssh);
    access_test_create_user(admin);
    access_fake_events_clear();
    return admin;
}

static crux_access_session_t user_session(void)
{
    crux_access_session_t session = access_test_session(USER, PASSWORD, &access_test_ssh);
    access_fake_events_clear();
    return session;
}

static ssh_cli_result_t run_exec(crux_access_session_t session, const char *line)
{
    int connection = -1;
    CHECK(ssh_cli_connect(session, &connection));
    CHECK(ssh_cli_exec(connection, line, strlen(line)));
    return ssh_cli_result(connection);
}

static ssh_cli_result_t run_shell(crux_access_session_t session, const char *input, bool terminal)
{
    int connection = -1;
    CHECK(ssh_cli_connect(session, &connection));
    CHECK(ssh_cli_input(connection, input, strlen(input), true));
    CHECK(ssh_cli_shell(connection, terminal));
    return ssh_cli_result(connection);
}

/* The same line under the same session on the serial channel, for comparison. */
static const char *serial_exec(crux_access_session_t session, const char *line, int *status)
{
    static cli_fake_t fake;
    cli_fake_reset(&fake);
    crux_cli_io_t io = cli_fake_io(&fake, false);
    CHECK(crux_cli_exec(&serial_context, &io, CRUX_ACCESS_CHANNEL_SERIAL, session, line,
                        strlen(line), status) == CRUX_CLI_OK);
    return fake.output;
}

static void permissions_as_on_serial(void)
{
    static const char *const lines[] = {"any",  "write",       "manage", "account list",
                                        "help", "help manage", "no-such"};
    begin();
    crux_access_session_t session = user_session();
    for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); ++index) {
        int status = -1;
        static char expected[1024];
        snprintf(expected, sizeof(expected), "%s", serial_exec(session, lines[index], &status));
        ssh_cli_result_t result = run_exec(session, lines[index]);
        CHECK_TEXT(result.text, expected);
        CHECK(result.status == status);
    }
    /* A role decides, not the channel: the operator holds one and lacks the other. */
    handler_runs = 0;
    ssh_cli_result_t result = run_exec(session, "write");
    CHECK_TEXT(result.text, "ran\n");
    CHECK(result.status == 0);
    access_fake_events_clear();
    cli_fake_events_clear();
    result = run_exec(session, "manage");
    CHECK_TEXT(result.text, "unknown command\n");
    CHECK(result.status == 127);
    CHECK(handler_runs == 1);
    /* A typed hidden command leaves Access's denial, and no CLI event on top of it. */
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 1);
    CHECK(cli_fake_event_count() == 0);

    /* help hides it and leaves no denial. */
    access_fake_events_clear();
    result = run_exec(session, "help");
    CHECK(strstr(result.text, "manage") == NULL);
    CHECK(strstr(result.text, "account list") == NULL);
    CHECK(strstr(result.text, "write - operators and above") != NULL);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 0);
    result = run_exec(session, "help manage");
    CHECK_TEXT(result.text, "unknown command\n");
    CHECK(result.status == 127);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 0);
}

static void the_attachment_decides_the_session(void)
{
    /* Two connections at once, each with its own attachment, each speaking for its own user. */
    crux_access_session_t admin = begin();
    crux_access_session_t user = user_session();
    int admin_connection = -1;
    int user_connection = -1;
    CHECK(ssh_cli_connect(admin, &admin_connection));
    CHECK(ssh_cli_connect(user, &user_connection));
    CHECK(ssh_cli_exec(user_connection, "manage", 6));
    CHECK(ssh_cli_exec(admin_connection, "manage", 6));
    CHECK(ssh_cli_result(user_connection).status == 127);
    CHECK(ssh_cli_result(admin_connection).status == 0);
    CHECK(handler_runs == 1);

    /* A rejected line's event names the user of this connection and the SSH channel. */
    cli_fake_events_clear();
    (void)run_exec(user, "no-such");
    CHECK(cli_fake_event_count() == 1);
    CHECK(cli_fake_event(0)->channel == CRUX_ACCESS_CHANNEL_SSH);
    CHECK_TEXT(cli_fake_event(0)->user, USER);
    cli_fake_events_clear();
    (void)run_shell(admin, "no-such\n", false);
    CHECK(cli_fake_event_count() == 1);
    CHECK(cli_fake_event(0)->channel == CRUX_ACCESS_CHANNEL_SSH);
    CHECK_TEXT(cli_fake_event(0)->user, ADMIN);
}

static void logout_stays_with_the_connection(void)
{
    /* The contract gives crux_access_logout to connection_closed, and the double here never calls
     * it, so the session is still good after the CLI's own logout. */
    begin();
    crux_access_session_t session = user_session();
    ssh_cli_result_t result = run_shell(session, "logout\n", false);
    CHECK(result.status == 0);
    char name[CRUX_ACCESS_NAME_SIZE];
    CHECK(crux_access_session_user(session, name, sizeof(name)) == CRUX_ACCESS_OK);
}

static void a_forced_change(void)
{
    crux_access_session_t admin = begin();
    CHECK(crux_access_account_reset_password(admin, USER, PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    crux_access_session_t session = user_session();
    bool required = false;
    CHECK(crux_access_session_must_change_password(session, &required) == CRUX_ACCESS_OK);
    CHECK(required);

    /* Held back, in exec too, whatever the class. */
    ssh_cli_result_t result = run_exec(session, "any");
    CHECK_TEXT(result.text, "unknown command\n");
    CHECK(result.status == 127);
    result = run_exec(session, "write");
    CHECK(result.status == 127);
    CHECK(handler_runs == 0);
    /* passwd needs a prompt, so exec cannot do it. */
    result = run_exec(session, "passwd");
    CHECK_TEXT(result.text, "not available for exec\n");
    CHECK(result.status == 2);

    /* A shell: the notice first, and the change itself. Nothing typed comes back. */
    static char input[256];
    snprintf(input, sizeof(input), "any\rpasswd\r%s\r%s\r%s\rany\rlogout\r", PASSWORD, NEW_PASSWORD,
             NEW_PASSWORD);
    result = run_shell(session, input, true);
    CHECK_TEXT(result.text, "password change required, use passwd\r\n"
                            "> any\r\nunknown command\r\n"
                            "> passwd\r\ncurrent password: \r\nnew password: \r\nrepeat: \r\n"
                            "password changed\r\n"
                            "> any\r\nran\r\n"
                            "> logout\r\n");
    CHECK(result.status == 0);
    CHECK(handler_runs == 1);
    CHECK(strstr(result.text, PASSWORD) == NULL);
    CHECK(strstr(result.text, NEW_PASSWORD) == NULL);
    for (size_t index = 0; index < cli_fake_event_count(); ++index) {
        const crux_cli_event_t *event = cli_fake_event(index);
        CHECK(strstr(event->user, PASSWORD) == NULL);
        CHECK(strstr(event->user, NEW_PASSWORD) == NULL);
    }
    /* The new password is the one that works now. */
    crux_access_session_t again = access_test_session(USER, NEW_PASSWORD, &access_test_ssh);
    CHECK(run_exec(again, "any").status == 0);
}

static void advance_past_idle(void *unused)
{
    (void)unused;
    access_fake_clock_advance((uint64_t)policy()->session_idle_limit_s * 1000u);
}

static void advance_half_idle(void *unused)
{
    (void)unused;
    access_fake_clock_advance((uint64_t)policy()->session_idle_limit_s * 500u);
}

static void limits_end_a_session(void)
{
    /* A client that sends nothing: its wait ends at the idle limit, and only a line refreshes it.
     */
    begin();
    crux_access_session_t session = user_session();
    int connection = -1;
    ssh_cli_shim.on_read_timeout = advance_half_idle;
    CHECK(ssh_cli_connect(session, &connection));
    CHECK(ssh_cli_shell(connection, false));
    ssh_cli_result_t result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "> session ended\n");
    CHECK(result.status == 1);
    CHECK(ssh_cli_shim.read_timeouts_seen == 2);

    /* A client that stops reading: its writes wait until the same limit. */
    begin();
    session = user_session();
    ssh_cli_shim.on_write_timeout = advance_past_idle;
    CHECK(ssh_cli_connect(session, &connection));
    crux_ssh_fake_client()->set_window(connection, 0);
    CHECK(ssh_cli_shell(connection, true));
    result = ssh_cli_result(connection);
    CHECK(result.length == 0);
    CHECK(result.status == 1);
    CHECK(ssh_cli_shim.write_timeouts_seen == 1);
    CHECK(ssh_cli_shim.read_calls == 0);

    /* In the middle of an exec's output. */
    begin();
    session = user_session();
    ssh_cli_shim.on_write_timeout = advance_past_idle;
    CHECK(ssh_cli_connect(session, &connection));
    crux_ssh_fake_client()->set_window(connection, 2);
    CHECK(ssh_cli_exec(connection, "any", 3));
    result = ssh_cli_result(connection);
    CHECK_TEXT(result.text, "ra");
    CHECK(result.status == 1);
    CHECK(handler_runs == 1);

    /* An exec on a session that has already expired runs nothing, and says so. */
    begin();
    session = user_session();
    access_fake_clock_advance((uint64_t)policy()->session_idle_limit_s * 1000u);
    handler_runs = 0;
    result = run_exec(session, "any");
    CHECK_TEXT(result.text, "session ended\n");
    CHECK(result.status == 1);
    CHECK(handler_runs == 0);

    /* After the absolute limit too, and activity does not extend it. */
    begin();
    session = user_session();
    uint64_t step = (uint64_t)policy()->session_idle_limit_s * 500u;
    for (uint64_t passed = 0; passed < (uint64_t)policy()->session_absolute_limit_s * 1000u;
         passed += step) {
        CHECK(crux_access_touch(session) == CRUX_ACCESS_OK);
        access_fake_clock_advance(step);
    }
    result = run_exec(session, "any");
    CHECK_TEXT(result.text, "session ended\n");
    CHECK(result.status == 1);

    /* A session ended by logout elsewhere, such as connection_closed of another attachment. */
    begin();
    session = user_session();
    crux_access_session_t ended = session;
    crux_access_logout(&ended);
    result = run_shell(session, "any\n", false);
    CHECK_TEXT(result.text, "> session ended\n");
    CHECK(result.status == 1);
}

int main(void)
{
    permissions_as_on_serial();
    the_attachment_decides_the_session();
    logout_stays_with_the_connection();
    a_forced_change();
    limits_end_a_session();
    return access_test_finish("cli over the fake SSH provider, on the real access core");
}
