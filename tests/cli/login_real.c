/* The console and the registry on the real crux_access core, with its fake ports and a fake store:
 * every row of the login and setup tables, login delay, forced password change, permission denial,
 * expiry and a lost lock. Linked with --wrap=crux_mutex_lock. */
#include "access_test.h"
#include "cli_console.h"
#include "fake_events.h"
#include "fake_transport.h"

#define CHECK_TEXT(actual, expected)                                                               \
    do {                                                                                           \
        ++access_test_checks;                                                                      \
        const char *cli_actual = (actual);                                                         \
        const char *cli_expected = (expected);                                                     \
        if (strcmp(cli_actual, cli_expected) != 0) {                                               \
            printf("FAIL %s:%d:\n  got      [%s]\n  expected [%s]\n", __FILE__, __LINE__,          \
                   cli_actual, cli_expected);                                                      \
            ++access_test_failures;                                                                \
        }                                                                                          \
    } while (0)

#define ADMIN ACCESS_TEST_ADMIN
#define USER ACCESS_TEST_USER
#define PASSWORD ACCESS_TEST_PASSWORD
#define OTHER_PASSWORD ACCESS_TEST_OTHER_PASSWORD
#define SETUP_ROLE "administrator"

void crux_cli_test_reset(void);

static cli_fake_t fake;
static crux_cli_context_t context;

static const crux_access_policy_t *policy(void)
{
    return &access_test_profile_reference.policy;
}

static bool contains(const void *memory, size_t size, const char *needle)
{
    const char *bytes = memory;
    size_t length = strlen(needle);
    for (size_t index = 0; index + length <= size; ++index) {
        if (memcmp(bytes + index, needle, length) == 0) {
            return true;
        }
    }
    return false;
}

static void begin_common(void)
{
    crux_cli_test_reset();
    cli_fake_reset(&fake);
    cli_fake_events_clear();
    memset(&context, 0xA5, sizeof(context));
    CHECK(crux_cli_init(NULL, NULL, 0) == CRUX_CLI_OK);
}

/* An empty medium, with provisioning open. */
static void begin_empty(void)
{
    begin_common();
    access_test_boot(policy());
}

/* An administrator exists already. */
static void begin_with_admin(void)
{
    begin_common();
    access_test_boot_with_admin();
}

static crux_cli_result_t serve(void)
{
    crux_cli_io_t io = cli_fake_io(&fake, true);
    return crux_cli_console_serve(&context, &io, SETUP_ROLE);
}

/* Serves this input, and requires this output. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test's own helper.
static void expect_serve(const char *input, const char *expected)
{
    cli_fake_input(&fake, input);
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(fake.output, expected);
    /* Whatever the path, nothing typed stays behind. */
    CHECK(!contains(&context, sizeof(context), PASSWORD));
    CHECK(!contains(&context, sizeof(context), OTHER_PASSWORD));
    /* The transport ended, so every session was ended before serve returned. */
    cli_fake_output_clear(&fake);
    cli_fake_reset(&fake);
}

/* --- A lock that cannot be taken. The linker's --wrap option fixes these names. --- */

static bool fail_locks;
static unsigned fail_lock_countdown;
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __real_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);

// NOLINTNEXTLINE(bugprone-reserved-identifier)
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    if (fail_lock_countdown > 0) {
        fail_lock_countdown -= 1;
        if (fail_lock_countdown == 0) {
            return CRUX_ERR_IO;
        }
    }
    return fail_locks ? CRUX_ERR_IO : __real_crux_mutex_lock(mutex, timeout_ms);
}

/* --- Login table --- */

static void login_denied_rows(void)
{
    /* A wrong password, an unknown name and a disabled account give the same output. */
    begin_with_admin();
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_web);
    access_test_create_user(admin);
    CHECK(crux_access_account_disable(admin, USER) == CRUX_ACCESS_OK);
    expect_serve(ADMIN "\nwrong-password-here\n",
                 "login: " ADMIN "\npassword: \nlogin failed\nlogin: ");
    expect_serve("nobody\n" PASSWORD "\n", "login: nobody\npassword: \nlogin failed\nlogin: ");
    expect_serve(USER "\n" PASSWORD "\n", "login: " USER "\npassword: \nlogin failed\nlogin: ");
    /* A name no account can hold, the same again, with no verification. */
    unsigned derivations = access_fake_derive_count();
    expect_serve("Not!A!Name\n" PASSWORD "\n",
                 "login: Not!A!Name\npassword: \nlogin failed\nlogin: ");
    CHECK(access_fake_derive_count() == derivations);
    /* A password longer than any account can hold, denied by length. */
    static char line[CRUX_ACCESS_PASSWORD_MAX + 8];
    memset(line, 'x', CRUX_ACCESS_PASSWORD_MAX + 1u);
    memcpy(line + CRUX_ACCESS_PASSWORD_MAX + 1u, "\n", 2);
    cli_fake_input(&fake, ADMIN "\n");
    expect_serve(line, "login: " ADMIN "\npassword: \nlogin failed\nlogin: ");
    CHECK(access_fake_derive_count() == derivations);
    /* The serial attempts were tagged as serial, and Access recorded them. */
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGIN_FAILURE) >= 5);
    const crux_access_event_t *last = NULL;
    for (size_t index = 0; index < access_fake_event_count(); ++index) {
        if (access_fake_event(index)->type == CRUX_ACCESS_EVENT_LOGIN_FAILURE) {
            last = access_fake_event(index);
        }
    }
    CHECK(last != NULL && last->origin.channel == CRUX_ACCESS_CHANNEL_SERIAL);
    /* The console itself adds nothing. */
    CHECK(cli_fake_event_count() == 0);
}

static void login_delay_row(void)
{
    /* Past the free attempts, a wrong password moves the bucket into a window. */
    begin_with_admin();
    access_fake_derive_counters_clear();
    unsigned attempts = 0;
    while (attempts < 2u * CRUX_ACCESS_DELAY_FREE_ATTEMPTS) {
        cli_fake_input(&fake, ADMIN "\nwrong-password-here\n");
        attempts += 1;
    }
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(strstr(fake.output, "try again in ") != NULL);
    /* The window's own attempt is not verified or counted, and the console does not sleep on it. */
    CHECK(access_fake_derive_count() < 2u * CRUX_ACCESS_DELAY_FREE_ATTEMPTS);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_DELAY_APPLIED) >= 1);
    cli_fake_reset(&fake);

    /* Inside the window, even the right password gets the delay. */
    cli_fake_input(&fake, ADMIN "\n" PASSWORD "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK(strstr(fake.output, "try again in ") != NULL);
    CHECK(strstr(fake.output, "> ") == NULL);
    cli_fake_reset(&fake);

    /* Once it has passed, the right password works. */
    access_fake_clock_advance(CRUX_ACCESS_DELAY_CAP_MS);
    cli_fake_input(&fake, ADMIN "\n" PASSWORD "\nlogout\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(fake.output, "login: " ADMIN "\npassword: \n> logout\nlogin: ");
    cli_fake_reset(&fake);
    CHECK(!contains(&context, sizeof(context), PASSWORD));
}

/* A second login while a verification runs, from inside the derivation. */
static void nested_login(void *unused)
{
    (void)unused;
    static cli_fake_t nested;
    static crux_cli_context_t nested_context;
    cli_fake_reset(&nested);
    cli_fake_input(&nested, ADMIN "\n" PASSWORD "\n");
    crux_cli_io_t io = cli_fake_io(&nested, true);
    CHECK(crux_cli_console_serve(&nested_context, &io, SETUP_ROLE) == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(nested.output, "login: " ADMIN "\npassword: \nbusy, try again\nlogin: ");
}

static void login_busy_row(void)
{
    begin_with_admin();
    access_fake_derive_hook(nested_login, NULL);
    cli_fake_input(&fake, ADMIN "\n" PASSWORD "\nlogout\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(fake.output, "login: " ADMIN "\npassword: \n> logout\nlogin: ");
    access_fake_derive_hook(NULL, NULL);
}

static void login_no_session_row(void)
{
    /* Fill every slot, including the one kept for serial. */
    begin_with_admin();
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_serial);
    static const char *const others[] = {"user-one", "user-two", "user-three"};
    for (size_t index = 0; index < 3; ++index) {
        CHECK(crux_access_account_create(admin, others[index], "viewer", PASSWORD,
                                         strlen(PASSWORD)) == CRUX_ACCESS_OK);
    }
    size_t filled = 1;
    uint32_t next_source = 100;
    for (size_t index = 0; index < 3 && filled < CRUX_ACCESS_SESSION_MAX; ++index) {
        for (size_t count = 0;
             count < CRUX_ACCESS_SESSIONS_PER_ACCOUNT && filled < CRUX_ACCESS_SESSION_MAX;
             ++count) {
            crux_access_origin_t origin = access_test_web_from(next_source);
            next_source += 1;
            crux_access_session_t session = {0};
            CHECK(access_test_login(others[index], PASSWORD, &origin, &session) == CRUX_ACCESS_OK);
            filled += 1;
        }
    }
    CHECK(filled == CRUX_ACCESS_SESSION_MAX);
    expect_serve(USER "\n" PASSWORD "\n",
                 "login: " USER "\npassword: \nno session available\nlogin: ");
}

static void login_not_ready_rows(void)
{
    /* A store that reads as unusable. */
    begin_common();
    access_fake_reset();
    crux_access_test_reset();
    access_fake_store_state(CRUX_ACCESS_STORE_UNUSABLE);
    CHECK(crux_access_init(policy()) == CRUX_ACCESS_ERR_NOT_READY);
    expect_serve(ADMIN "\n" PASSWORD "\n",
                 "login: " ADMIN "\npassword: \nlogin unavailable\nlogin: ");
    /* Setup says its own word there. */
    expect_serve(
        "!setup\nfirst\n" PASSWORD "\n" PASSWORD "\n",
        "login: !setup\nnew user: first\npassword: \nrepeat: \nsetup not available\nlogin: ");

    /* A crypto provider failure. */
    begin_with_admin();
    access_fake_derive_fail(true);
    expect_serve(ADMIN "\n" PASSWORD "\n",
                 "login: " ADMIN "\npassword: \nlogin unavailable\nlogin: ");
    access_fake_derive_fail(false);
}

/* --- Setup table --- */

static void setup_rows(void)
{
    /* First account, on the serial origin. No session opens, and it can then log in. */
    begin_empty();
    expect_serve("!setup\nfirst\n" PASSWORD "\n" PASSWORD "\n",
                 "login: !setup\nnew user: first\npassword: \nrepeat: \naccount created\nlogin: ");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PROVISIONED) == 1);
    expect_serve("first\n" PASSWORD "\nlogout\n", "login: first\npassword: \n> logout\nlogin: ");

    /* Closed once an account exists. */
    expect_serve(
        "!setup\nsecond\n" PASSWORD "\n" PASSWORD "\n",
        "login: !setup\nnew user: second\npassword: \nrepeat: \nsetup not available\nlogin: ");
    /* Closed on a store that already held an account. */
    begin_with_admin();
    expect_serve(
        "!setup\nsecond\n" PASSWORD "\n" PASSWORD "\n",
        "login: !setup\nnew user: second\npassword: \nrepeat: \nsetup not available\nlogin: ");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PROVISION_REFUSED) >= 1);

    /* The password rule, then the name, then a corrected attempt, all in one dialogue. */
    begin_empty();
    expect_serve("!setup\nfirst\nshort\nshort\nBad!Name\n" PASSWORD "\n" PASSWORD
                 "\nfirst\n" PASSWORD "\n" PASSWORD "\n",
                 "login: !setup\n"
                 "new user: first\npassword: \nrepeat: \npassword not accepted\n"
                 "new user: Bad!Name\npassword: \nrepeat: \ninvalid name\n"
                 "new user: first\npassword: \nrepeat: \naccount created\nlogin: ");

    /* A bad role in the build lands on invalid name too, and needs a corrected build. */
    begin_empty();
    cli_fake_input(&fake, "!setup\nfirst\n" PASSWORD "\n" PASSWORD "\n");
    {
        crux_cli_io_t io = cli_fake_io(&fake, true);
        CHECK(crux_cli_console_serve(&context, &io, "viewer") == CRUX_CLI_ERR_OUTPUT);
    }
    CHECK(strstr(fake.output, "invalid name\nnew user: ") != NULL);
    cli_fake_reset(&fake);

    /* A failed save: the required words, the login prompt, and a retry that can still succeed. */
    begin_empty();
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
    expect_serve("!setup\nfirst\n" PASSWORD "\n" PASSWORD "\n",
                 "login: !setup\nnew user: first\npassword: \nrepeat: \n"
                 "could not save, a later boot may use either\nlogin: ");
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);
    expect_serve("!setup\nfirst\n" PASSWORD "\n" PASSWORD "\n",
                 "login: !setup\nnew user: first\npassword: \nrepeat: \naccount created\nlogin: ");

    /* A crypto provider failure. */
    begin_empty();
    access_fake_derive_fail(true);
    expect_serve(
        "!setup\nfirst\n" PASSWORD "\n" PASSWORD "\n",
        "login: !setup\nnew user: first\npassword: \nrepeat: \nlogin unavailable\nlogin: ");
    access_fake_derive_fail(false);

    /* A wrong entry, with nothing asked of Access at all. */
    begin_empty();
    size_t events_before = access_fake_event_count();
    expect_serve(
        "!setup\nfirst\n" PASSWORD "\n" OTHER_PASSWORD "\n",
        "login: !setup\nnew user: first\npassword: \nrepeat: \npasswords differ\nnew user: ");
    CHECK(access_fake_event_count() == events_before);
}

/* --- Forced password change, permission denial --- */

static void forced_password_change(void)
{
    begin_with_admin();
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_web);
    access_test_create_user(admin);
    CHECK(crux_access_account_reset_password(admin, USER, PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    access_fake_events_clear();

    /* Logged in under a forced change: a notice, and only what can run. The rule also stops a
     * command that passes no permission check. */
    expect_serve(USER "\n" PASSWORD "\naccount list\nhelp\nlogout\n",
                 "login: " USER "\npassword: \n"
                 "password change required, use passwd\n"
                 "> account list\nunknown command\n"
                 "> help\nhelp - list commands, or describe one\n"
                 "logout - end this session\n"
                 "passwd - change your password\n"
                 "> logout\nlogin: ");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 0);

    /* A wrong current password changes nothing, and the session stays under the rule. */
    expect_serve(USER "\n" PASSWORD "\npasswd\nwrong-password-here\n" OTHER_PASSWORD
                      "\n" OTHER_PASSWORD "\nlogout\n",
                 "login: " USER "\npassword: \npassword change required, use passwd\n"
                 "> passwd\ncurrent password: \nnew password: \nrepeat: \n"
                 "password not changed\n> logout\nlogin: ");

    /* The change clears the rule at once in this session, and the new password then works. */
    expect_serve(USER "\n" PASSWORD "\npasswd\n" PASSWORD "\n" OTHER_PASSWORD "\n" OTHER_PASSWORD
                      "\nhelp logout\nlogout\n",
                 "login: " USER "\npassword: \npassword change required, use passwd\n"
                 "> passwd\ncurrent password: \nnew password: \nrepeat: \n"
                 "password changed\n> help logout\nlogout - end this session\n> logout\nlogin: ");
    expect_serve(USER "\n" OTHER_PASSWORD "\nlogout\n",
                 "login: " USER "\npassword: \n> logout\nlogin: ");
    expect_serve(USER "\n" PASSWORD "\n", "login: " USER "\npassword: \nlogin failed\nlogin: ");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_CHANGED) == 1);
}

static void permission_denial(void)
{
    begin_with_admin();
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_web);
    access_test_create_user(admin);
    access_fake_events_clear();

    /* A user without the permission: the same words as an unknown command, and Access's denial. */
    expect_serve(USER "\n" PASSWORD "\naccount list\nno-such\nhelp account list\nlogout\n",
                 "login: " USER "\npassword: \n"
                 "> account list\nunknown command\n"
                 "> no-such\nunknown command\n"
                 "> help account list\nunknown command\n"
                 "> logout\nlogin: ");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 1);
    const crux_access_event_t *denial = NULL;
    for (size_t index = 0; index < access_fake_event_count(); ++index) {
        if (access_fake_event(index)->type == CRUX_ACCESS_EVENT_PERMISSION_DENIED) {
            denial = access_fake_event(index);
        }
    }
    CHECK(denial != NULL);
    if (denial != NULL) {
        CHECK_TEXT(denial->permission, CRUX_ACCESS_PERMISSION_MANAGE);
        CHECK_TEXT(denial->actor, USER);
        CHECK(denial->origin.channel == CRUX_ACCESS_CHANNEL_SERIAL);
    }
    /* The unknown line left the CLI's own event, naming the user and not the line. */
    CHECK(cli_fake_event_count() == 1);
    CHECK_TEXT(cli_fake_event(0)->user, USER);

    /* help left no denial of its own. */
    access_fake_events_clear();
    expect_serve(USER "\n" PASSWORD "\nhelp\nlogout\n",
                 "login: " USER "\npassword: \n> help\nhelp - list commands, or describe one\n"
                 "logout - end this session\npasswd - change your password\n> logout\nlogin: ");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 0);

    /* An administrator holds it, and sees it. */
    expect_serve(ADMIN "\n" PASSWORD "\naccount list\nlogout\n",
                 "login: " ADMIN "\npassword: \n> account list\n"
                 "admin administrator enabled keys=0\nalice operator enabled keys=0\n"
                 "> logout\nlogin: ");

    /* A run over SSH sees the same, through the same registry. */
    crux_access_session_t session = {0};
    CHECK(access_test_login(USER, PASSWORD, &access_test_ssh, &session) == CRUX_ACCESS_OK);
    int status = -1;
    crux_cli_io_t io = cli_fake_io(&fake, false);
    CHECK(crux_cli_exec(&context, &io, CRUX_ACCESS_CHANNEL_SSH, session, "account list", 12,
                        &status) == CRUX_CLI_OK);
    CHECK(status == 127);
    CHECK_TEXT(fake.output, "unknown command\n");
    cli_fake_output_clear(&fake);
    CHECK(crux_cli_exec(&context, &io, CRUX_ACCESS_CHANNEL_SSH, session, "passwd", 6, &status) ==
          CRUX_CLI_OK);
    CHECK(status == 2);
    cli_fake_output_clear(&fake);
    CHECK(crux_cli_exec(&context, &io, CRUX_ACCESS_CHANNEL_SSH, session, "help logout", 11,
                        &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    CHECK_TEXT(fake.output, "logout - end this session\n");
    crux_access_logout(&session);
    cli_fake_reset(&fake);
}

/* --- Expiry --- */

static void advance_past_idle(void *unused)
{
    (void)unused;
    access_fake_clock_advance((uint64_t)policy()->session_idle_limit_s * 1000u);
}

static void session_expiry(void)
{
    /* Found at a read timeout, back to the prompt. */
    begin_with_admin();
    access_fake_events_clear();
    cli_fake_input(&fake, ADMIN "\n" PASSWORD "\n");
    cli_fake_input_timeouts(&fake, 1);
    fake.on_read_timeout = advance_past_idle;
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(fake.output, "login: " ADMIN "\npassword: \n> session ended\nlogin: ");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_SESSION_EXPIRED) == 1);
    cli_fake_reset(&fake);

    /* Found at the next line, if nothing timed out first. */
    begin_with_admin();
    access_fake_events_clear();
    cli_fake_input(&fake, ADMIN "\n" PASSWORD "\nhelp");
    fake.hook_position = fake.input_length;
    fake.on_position = advance_past_idle;
    cli_fake_input(&fake, "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(fake.output, "login: " ADMIN "\npassword: \n> help\nsession ended\nlogin: ");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_SESSION_EXPIRED) == 1);
    cli_fake_reset(&fake);

    /* A typed line counts as activity, and a timed-out read does not. */
    begin_with_admin();
    uint64_t half = (uint64_t)policy()->session_idle_limit_s * 500u;
    crux_access_session_t session = {0};
    CHECK(access_test_login(ADMIN, PASSWORD, &access_test_serial, &session) == CRUX_ACCESS_OK);
    crux_cli_io_t io = cli_fake_io(&fake, false);
    access_fake_clock_advance(half);
    int status = -1;
    CHECK(crux_cli_exec(&context, &io, CRUX_ACCESS_CHANNEL_SSH, session, "help logout", 11,
                        &status) == CRUX_CLI_OK);
    access_fake_clock_advance(half);
    CHECK(crux_cli_exec(&context, &io, CRUX_ACCESS_CHANNEL_SSH, session, "help logout", 11,
                        &status) == CRUX_CLI_OK);
    CHECK(status == 0);
    /* Run forward past the idle limit, and the same exec is refused with the session gone. */
    access_fake_clock_advance(half * 2u);
    CHECK(crux_cli_exec(&context, &io, CRUX_ACCESS_CHANNEL_SSH, session, "help logout", 11,
                        &status) == CRUX_CLI_ERR_DENIED);
    CHECK(status == 1);
    cli_fake_reset(&fake);

    /* A session reference that was never valid is refused at the first line. */
    begin_with_admin();
    cli_fake_input(&fake, "help\n");
    {
        crux_access_session_t none = {0};
        crux_cli_io_t no_session_io = cli_fake_io(&fake, false);
        CHECK(crux_cli_run(&context, &no_session_io, CRUX_ACCESS_CHANNEL_SERIAL, none) ==
              CRUX_CLI_ERR_DENIED);
    }
    CHECK_TEXT(fake.output, "> session ended\n");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_SESSION_INVALID) >= 1);
    cli_fake_reset(&fake);

    /* An expired session through run, against a fresh console prompt afterwards. */
    begin_with_admin();
    crux_access_session_t stale = access_test_session(ADMIN, PASSWORD, &access_test_serial);
    access_fake_clock_advance((uint64_t)policy()->session_idle_limit_s * 1000u);
    cli_fake_input(&fake, "help\n");
    io = cli_fake_io(&fake, false);
    CHECK(crux_cli_run(&context, &io, CRUX_ACCESS_CHANNEL_SERIAL, stale) == CRUX_CLI_ERR_DENIED);
    CHECK_TEXT(fake.output, "> session ended\n");
    cli_fake_reset(&fake);
}

/* --- A lost lock --- */

static void start_failing_locks(void *unused)
{
    (void)unused;
    fail_locks = true;
}

static void fail_second_lock(void *unused)
{
    (void)unused;
    fail_lock_countdown = 2;
}

static void fail_third_lock(void *unused)
{
    (void)unused;
    fail_lock_countdown = 3;
}

static void lost_lock(void)
{
    /* Before login: login is refused, and nothing is created. */
    begin_with_admin();
    fail_locks = true;
    expect_serve(ADMIN "\n" PASSWORD "\n",
                 "login: " ADMIN "\npassword: \nlogin unavailable\nlogin: ");
    fail_locks = false;

    /* First-account setup: refused, and it fails closed. */
    begin_empty();
    fail_locks = true;
    expect_serve(
        "!setup\nfirst\n" PASSWORD "\n" PASSWORD "\n",
        "login: !setup\nnew user: first\npassword: \nrepeat: \nlogin unavailable\nlogin: ");
    fail_locks = false;

    /* In a session, at the touch: the session ends, back to the prompt, and no command runs. */
    begin_with_admin();
    cli_fake_input(&fake, ADMIN "\n" PASSWORD "\nlogout");
    fake.hook_position = fake.input_length;
    fake.on_position = start_failing_locks;
    cli_fake_input(&fake, "\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    fail_locks = false;
    CHECK_TEXT(fake.output, "login: " ADMIN "\npassword: \n> logout\nsession ended\nlogin: ");
    /* A lost lock emits nothing, by the Access contract. The CLI adds no event either. */
    CHECK(cli_fake_event_count() == 0);
    cli_fake_reset(&fake);

    /* At a read timeout in a session. */
    begin_with_admin();
    cli_fake_input(&fake, ADMIN "\n" PASSWORD "\n");
    cli_fake_input_timeouts(&fake, 1);
    fake.on_read_timeout = start_failing_locks;
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    fail_locks = false;
    CHECK_TEXT(fake.output, "login: " ADMIN "\npassword: \n> session ended\nlogin: ");
    cli_fake_reset(&fake);

    /* After the touch: the same words as a denial, then the session ends. The forced-change query
     * takes the second lock of the line, and the permission check the third. */
    const cli_fake_hook_t lost_at[] = {fail_second_lock, fail_third_lock};
    for (size_t index = 0; index < 2; ++index) {
        begin_with_admin();
        cli_fake_input(&fake, ADMIN "\n" PASSWORD "\naccount list");
        fake.hook_position = fake.input_length;
        fake.on_position = lost_at[index];
        cli_fake_input(&fake, "\n");
        CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
        CHECK(fail_lock_countdown == 0);
        CHECK_TEXT(fake.output,
                   "login: " ADMIN
                   "\npassword: \n> account list\nunknown command\nsession ended\nlogin: ");
        cli_fake_reset(&fake);
    }

    /* The core recovers once locks work again. */
    begin_with_admin();
    expect_serve(ADMIN "\n" PASSWORD "\nlogout\n",
                 "login: " ADMIN "\npassword: \n> logout\nlogin: ");
}

/* --- Secrets --- */

static void secrets_stay_out(void)
{
    begin_with_admin();
    access_fake_events_clear();
    cli_fake_input(&fake, ADMIN "\nwrong-hunter2-password\n");
    cli_fake_input(&fake, "hunter2-in-the-name-field\n" PASSWORD "\n");
    cli_fake_input(&fake, ADMIN "\n" PASSWORD "\n");
    cli_fake_input(&fake, "hunter2-typed-as-a-command\n");
    cli_fake_input(&fake, "passwd\nhunter2-current-password\nhunter2-new-password\n"
                          "hunter2-new-password\n");
    cli_fake_input(&fake, "logout\n");
    CHECK(serve() == CRUX_CLI_ERR_OUTPUT);
    /* Passwords are not echoed. What is typed at the name prompt and the command prompt is. */
    CHECK(!contains(fake.output, fake.output_length, "wrong-hunter2-password"));
    CHECK(!contains(fake.output, fake.output_length, "hunter2-current-password"));
    CHECK(!contains(fake.output, fake.output_length, "hunter2-new-password"));
    CHECK(!contains(fake.output, fake.output_length, PASSWORD));
    /* No event of either kind holds anything typed. */
    for (size_t index = 0; index < access_fake_event_count(); ++index) {
        const crux_access_event_t *event = access_fake_event(index);
        CHECK(!contains(event, sizeof(*event), "hunter2"));
        CHECK(!contains(event, sizeof(*event), PASSWORD));
    }
    CHECK(cli_fake_event_count() >= 1);
    for (size_t index = 0; index < cli_fake_event_count(); ++index) {
        const crux_cli_event_t *event = cli_fake_event(index);
        CHECK(!contains(event, sizeof(*event), "hunter2"));
    }
    /* Nothing typed stays in the session's memory. */
    CHECK(!contains(&context, sizeof(context), "hunter2"));
    CHECK(!contains(&context, sizeof(context), PASSWORD));
    cli_fake_reset(&fake);
}

int main(void)
{
    login_denied_rows();
    login_delay_row();
    login_busy_row();
    login_no_session_row();
    login_not_ready_rows();
    setup_rows();
    forced_password_change();
    permission_denial();
    session_expiry();
    lost_lock();
    secrets_stay_out();
    return access_test_finish("cli login on the real core");
}
