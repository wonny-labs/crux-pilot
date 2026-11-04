/* The acceptance item of Issue #182, shared with #177: each rejection path of access control leaves
 * its record, through the real provider and the real core, in the ring and in the log line. It also
 * covers each row of the contract's result table and each port event. */
#include "joined.h"

#define ADMIN ACCESS_TEST_ADMIN
#define USER ACCESS_TEST_USER
#define PASSWORD ACCESS_TEST_PASSWORD
#define OTHER_PASSWORD ACCESS_TEST_OTHER_PASSWORD
#define WRONG "wrong-password-here"
#define TYPED_NAME "typed-password-as-name"

static const uint8_t key_one[] = ACCESS_TEST_ED25519_KEY(0x11);
static const uint8_t key_two[] = ACCESS_TEST_ED25519_KEY(0x22);

static uint32_t cursor;

/* What a record must hold. A NULL means that the field is empty. */
typedef struct {
    crux_security_event_id_t id;
    const char *name;
    crux_security_event_result_t result;
    const char *result_text;
    const crux_access_origin_t *origin;
    const char *user;
    const char *subject;
} expected_t;

/* Drains what came before, so that the next expect sees only what follows. */
static void start(void)
{
    event_test_drain_all();
    event_test_lines_clear();
    cursor = crux_security_event_state.next_sequence;
}

static const char *channel_text(uint8_t channel)
{
    switch (channel) {
    case 1:
        return "serial";
    case 2:
        return "web";
    case 3:
        return "ssh";
    default:
        return "";
    }
}

/* Exactly one new record, holding these fields, and exactly one new line, saying the same. */
static void expect(const expected_t *row)
{
    crux_security_event_record_t record;
    crux_security_event_read_result_t result = crux_security_event_test_read(&cursor, &record);
    CHECK(result == CRUX_SECURITY_READ_OK);
    if (result != CRUX_SECURITY_READ_OK) {
        printf("  no record for %s\n", row->name);
        return;
    }
    uint32_t next = cursor;
    crux_security_event_record_t extra;
    CHECK(crux_security_event_test_read(&next, &extra) == CRUX_SECURITY_READ_END);

    CHECK(record.id == row->id);
    CHECK(record.result == row->result);
    CHECK(strcmp(record.user, row->user == NULL ? "" : row->user) == 0);
    CHECK(strcmp(record.subject, row->subject == NULL ? "" : row->subject) == 0);
    uint8_t channel = row->origin == NULL ? 0u : (uint8_t)row->origin->channel;
    uint8_t source_length = row->origin == NULL ? 0u : row->origin->source_length;
    CHECK(record.channel == channel);
    CHECK(record.source_length == source_length);
    if (row->origin != NULL) {
        CHECK(memcmp(record.source, row->origin->source, source_length) == 0);
    }

    CHECK(event_test_drain_all() == 1);
    const char *line = event_test_last_line();
    char expected_start[64];
    snprintf(expected_start, sizeof(expected_start), "evt=%s seq=%u ", row->name,
             (unsigned)record.sequence);
    CHECK(strncmp(line, expected_start, strlen(expected_start)) == 0);
    char field[96];
    snprintf(field, sizeof(field), " res=%s", row->result_text);
    CHECK(strstr(line, field) != NULL);
    if (channel != 0) {
        snprintf(field, sizeof(field), " chan=%s", channel_text(channel));
        CHECK(strstr(line, field) != NULL);
    } else {
        CHECK(strstr(line, " chan=") == NULL);
    }
    if (source_length == 4) {
        snprintf(field, sizeof(field), " src=%u.%u.%u.%u ", row->origin->source[0],
                 row->origin->source[1], row->origin->source[2], row->origin->source[3]);
        CHECK(strstr(line, field) != NULL);
    } else {
        CHECK(strstr(line, " src=") == NULL);
    }
    if (row->user != NULL) {
        snprintf(field, sizeof(field), " user=%s ", row->user);
        CHECK(strstr(line, field) != NULL);
    } else {
        CHECK(strstr(line, " user=") == NULL);
    }
    if (row->subject != NULL) {
        snprintf(field, sizeof(field), " subj=%s ", row->subject);
        CHECK(strstr(line, field) != NULL);
    } else {
        CHECK(strstr(line, " subj=") == NULL);
    }
    /* What a client typed is never in the record or the line. */
    CHECK(strstr(line, WRONG) == NULL);
    CHECK(strstr(line, TYPED_NAME) == NULL);
    event_test_lines_clear();
}

#define ID(name) CRUX_SECURITY_EVENT_ID_##name

static void expect_row(crux_security_event_id_t id, const char *name,
                       crux_security_event_result_t result, const char *result_text,
                       const crux_access_origin_t *origin, const char *user, const char *subject)
{
    const expected_t row = {id, name, result, result_text, origin, user, subject};
    expect(&row);
}

#define DENIED CRUX_SECURITY_EVENT_DENIED, "denied"
#define INVALID CRUX_SECURITY_EVENT_INVALID, "invalid"
#define FAILED CRUX_SECURITY_EVENT_FAILED, "failed"
#define DELAYED CRUX_SECURITY_EVENT_DELAYED, "delayed"
#define BUSY CRUX_SECURITY_EVENT_BUSY, "busy"
#define LIMIT CRUX_SECURITY_EVENT_LIMIT, "limit"
#define OK CRUX_SECURITY_EVENT_OK, "ok"

#define LOGIN_FAILED ID(ACCESS_LOGIN_FAILED), "access.login_failed"
#define KEY_FAILED ID(ACCESS_KEY_FAILED), "access.key_failed"
#define LOGIN_REFUSED ID(ACCESS_LOGIN_REFUSED), "access.login_refused"
#define NOT_READY ID(ACCESS_NOT_READY), "access.not_ready"
#define CHANGE_REJECTED ID(ACCESS_CHANGE_REJECTED), "access.change_rejected"
#define PROVISION_REFUSED ID(ACCESS_PROVISION_REFUSED), "access.provision_refused"
#define PERMISSION_DENIED ID(ACCESS_PERMISSION_DENIED), "access.permission_denied"

static crux_access_session_t boot_with_user(void)
{
    crux_access_session_t admin = joined_boot_admin();
    access_test_create_user(admin);
    start();
    return admin;
}

static void login_failures(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t session = {0};
    CHECK(crux_access_account_create(admin, "disabled", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_disable(admin, "disabled") == CRUX_ACCESS_OK);
    start();
    uint32_t retry_after_ms = 0;
    crux_access_origin_t origin = access_test_web_from(1);

    /* Bad password. */
    CHECK(access_test_login(USER, WRONG, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_FAILED, DENIED, &origin, USER, NULL);
    /* Unknown name, which is never named. */
    CHECK(access_test_login(TYPED_NAME, WRONG, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_FAILED, DENIED, &origin, NULL, NULL);
    /* Disabled account. */
    CHECK(access_test_login("disabled", PASSWORD, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_FAILED, DENIED, &origin, "disabled", NULL);
    /* Bad name form. */
    CHECK(access_test_login("Not A Name!", WRONG, &origin, &session) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_row(LOGIN_FAILED, INVALID, &origin, NULL, NULL);
    /* Over-long password. */
    CHECK(crux_access_login_password(USER, WRONG, CRUX_ACCESS_PASSWORD_MAX + 1u, &origin, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_FAILED, DENIED, &origin, NULL, NULL);
}

static void key_failures(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    CHECK(crux_access_account_add_key(admin, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    start();

    /* An unsigned probe records nothing, as the access contract says. */
    CHECK(crux_access_key_is_authorised(USER, key_two, sizeof(key_two), false, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(event_test_drain_all() == 0);

    /* Bad key. */
    CHECK(crux_access_key_is_authorised(USER, key_two, sizeof(key_two), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_row(KEY_FAILED, DENIED, &access_test_ssh, USER, NULL);
    /* Failed signature, naming a known account only. */
    crux_access_report_key_failure(USER, &access_test_ssh);
    expect_row(KEY_FAILED, DENIED, &access_test_ssh, USER, NULL);
    crux_access_report_key_failure(TYPED_NAME, &access_test_ssh);
    expect_row(KEY_FAILED, DENIED, &access_test_ssh, NULL, NULL);
    /* A bad name form on a signed key attempt. */
    CHECK(crux_access_key_is_authorised("Not A Name!", key_two, sizeof(key_two), true,
                                        &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_row(LOGIN_FAILED, INVALID, &access_test_ssh, NULL, NULL);
    /* A refused verified login. */
    CHECK(crux_access_login_verified(USER, key_two, sizeof(key_two), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_REFUSED, DENIED, &access_test_ssh, USER, NULL);

    /* Forced change: key login is refused at both steps, with the client's results unchanged. */
    CHECK(crux_access_account_reset_password(admin, USER, OTHER_PASSWORD, strlen(OTHER_PASSWORD)) ==
          CRUX_ACCESS_OK);
    start();
    CHECK(crux_access_key_is_authorised(USER, key_one, sizeof(key_one), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_REFUSED, DENIED, &access_test_ssh, USER, NULL);
    CHECK(crux_access_login_verified(USER, key_one, sizeof(key_one), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_REFUSED, DENIED, &access_test_ssh, USER, NULL);

    /* A forced change also denies everything else, naming the permission. */
    crux_access_session_t forced =
        access_test_session(USER, OTHER_PASSWORD, &access_test_web_other);
    start();
    CHECK(crux_access_check(forced, "status.read") == CRUX_ACCESS_ERR_DENIED);
    expect_row(PERMISSION_DENIED, DENIED, &access_test_web_other, USER, "status.read");
}

static void delay_window(void)
{
    (void)boot_with_user();
    crux_access_session_t session = {0};
    crux_access_origin_t origin = access_test_web_from(2);
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        (void)access_test_login(USER, WRONG, &origin, &session);
    }
    start();
    CHECK(access_test_login(USER, WRONG, &origin, &session) == CRUX_ACCESS_ERR_RETRY_LATER);
    expect_row(ID(ACCESS_DELAYED), "access.delayed", DELAYED, &origin, NULL, NULL);
}

static void hook_login_while_verifying(void *context)
{
    (void)context;
    crux_access_session_t session = {0};
    crux_access_origin_t origin = access_test_web_from(3);
    CHECK(access_test_login(USER, PASSWORD, &origin, &session) == CRUX_ACCESS_ERR_BUSY);
}

static void busy_slot(void)
{
    (void)boot_with_user();
    access_fake_derive_hook(hook_login_while_verifying, NULL);
    crux_access_session_t session = access_test_session(USER, PASSWORD, &access_test_web_other);
    CHECK(session.value != 0);
    /* The nested attempt's record, then the login that held the slot. */
    crux_access_origin_t origin = access_test_web_from(3);
    crux_security_event_record_t record;
    CHECK(crux_security_event_test_read(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == ID(ACCESS_LOGIN_BUSY));
    CHECK(record.result == CRUX_SECURITY_EVENT_BUSY);
    CHECK(record.channel == (uint8_t)origin.channel);
    CHECK(memcmp(record.source, origin.source, 4) == 0);
    CHECK(record.user[0] == '\0');
    CHECK(crux_security_event_test_read(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == ID(ACCESS_LOGIN_OK));
}

static void hook_change_password_while_verifying(void *context)
{
    crux_access_session_t session = *(crux_access_session_t *)context;
    uint32_t retry_after_ms = 0;
    CHECK(crux_access_account_change_password(session, PASSWORD, strlen(PASSWORD), OTHER_PASSWORD,
                                              strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_ERR_BUSY);
}

/* The result table's other BUSY row: changing one's own password also needs the slot. */
static void busy_slot_on_own_password(void)
{
    (void)boot_with_user();
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web_other);
    start();
    access_fake_derive_hook(hook_change_password_while_verifying, &user);
    crux_access_session_t other = access_test_session(ADMIN, PASSWORD, &access_test_web);
    CHECK(other.value != 0);

    crux_security_event_record_t record;
    CHECK(crux_security_event_test_read(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == ID(ACCESS_LOGIN_BUSY));
    CHECK(record.result == CRUX_SECURITY_EVENT_BUSY);
    CHECK(record.channel == (uint8_t)access_test_web_other.channel);
    CHECK(memcmp(record.source, access_test_web_other.source, 4) == 0);
    CHECK(record.user[0] == '\0');
    CHECK(crux_security_event_test_read(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == ID(ACCESS_LOGIN_OK));
}

static void full_tables(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t session = {0};
    /* Fill the session table, which the administrator's session already occupies. */
    char name[16];
    size_t network_slots = CRUX_ACCESS_SESSION_MAX - 1u;
    size_t used = 1;
    for (unsigned account = 0; used < network_slots; ++account) {
        snprintf(name, sizeof(name), "filler-%u", account);
        CHECK(crux_access_account_create(admin, name, "viewer", PASSWORD, strlen(PASSWORD)) ==
              CRUX_ACCESS_OK);
        for (unsigned count = 0; count < CRUX_ACCESS_SESSIONS_PER_ACCOUNT && used < network_slots;
             ++count, ++used) {
            crux_access_origin_t origin = access_test_web_from(100u + used);
            (void)access_test_session(name, PASSWORD, &origin);
            event_test_drain_all();
        }
    }
    start();
    crux_access_origin_t origin = access_test_web_from(99);
    CHECK(access_test_login(USER, PASSWORD, &origin, &session) == CRUX_ACCESS_ERR_NO_RESOURCES);
    expect_row(ID(ACCESS_LOGIN_NO_SESSION), "access.login_no_session", LIMIT, &origin, NULL, NULL);

    /* The account table: nobody is named, because the new name is not an account. */
    for (size_t index = 0; index < CRUX_ACCESS_ACCOUNT_MAX; ++index) {
        snprintf(name, sizeof(name), "user-%zu", index);
        (void)crux_access_account_create(admin, name, "viewer", PASSWORD, strlen(PASSWORD));
        event_test_drain_all();
    }
    start();
    CHECK(crux_access_account_create(admin, "one-too-many", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_ERR_NO_RESOURCES);
    expect_row(CHANGE_REJECTED, LIMIT, &access_test_web, ADMIN, NULL);
}

static void removed_role(void)
{
    crux_access_session_t admin = boot_with_user();
    CHECK(crux_access_account_add_key(admin, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    static const char *const manage[] = {CRUX_ACCESS_PERMISSION_MANAGE};
    static const crux_access_role_t reduced_roles[] = {{"administrator", manage, 1}};
    static const crux_access_policy_t reduced = {reduced_roles, 1, 900, 28800, NULL};
    access_test_power_cycle(&reduced);
    start();
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &session) == CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_REFUSED, DENIED, &access_test_web, USER, NULL);
    CHECK(crux_access_key_is_authorised(USER, key_one, sizeof(key_one), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_REFUSED, DENIED, &access_test_ssh, USER, NULL);
    CHECK(crux_access_login_verified(USER, key_one, sizeof(key_one), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    expect_row(LOGIN_REFUSED, DENIED, &access_test_ssh, USER, NULL);
}

static void session_rejections(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web_other);
    start();

    /* Unpermitted check, with the permission as the subject. */
    CHECK(crux_access_check(user, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_ERR_DENIED);
    expect_row(PERMISSION_DENIED, DENIED, &access_test_web_other, USER, "access.manage");
    /* An account operation names the permission it needed. */
    CHECK(crux_access_account_disable(user, ADMIN) == CRUX_ACCESS_ERR_DENIED);
    expect_row(PERMISSION_DENIED, DENIED, &access_test_web_other, USER, "access.manage");

    /* Stale session: no channel to report. */
    crux_access_session_t stale = user;
    crux_access_logout(&user);
    expect_row(ID(ACCESS_LOGOUT), "access.logout", OK, &access_test_web_other, USER, NULL);
    CHECK(crux_access_check(stale, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_row(ID(ACCESS_SESSION_INVALID), "access.session_invalid", DENIED, NULL, NULL, NULL);
    /* Never valid. */
    const crux_access_session_t never = {0};
    CHECK(crux_access_check(never, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_row(ID(ACCESS_SESSION_INVALID), "access.session_invalid", DENIED, NULL, NULL, NULL);

    /* Expired session: named once, then stale. */
    access_fake_clock_advance((uint64_t)access_test_profile_reference.policy.session_idle_limit_s *
                              1000u);
    CHECK(crux_access_check(admin, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_row(ID(ACCESS_SESSION_EXPIRED), "access.session_expired", DENIED, &access_test_web,
               ADMIN, NULL);
    CHECK(crux_access_check(admin, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_row(ID(ACCESS_SESSION_INVALID), "access.session_invalid", DENIED, NULL, NULL, NULL);
}

static void change_rejections(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web_other);
    uint32_t retry_after_ms = 0;
    const size_t length = strlen(PASSWORD);
    start();

    /* Last administrator. */
    CHECK(crux_access_account_disable(admin, ADMIN) == CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    expect_row(CHANGE_REJECTED, DENIED, &access_test_web, ADMIN, ADMIN);
    CHECK(crux_access_account_remove(admin, ADMIN) == CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    expect_row(CHANGE_REJECTED, DENIED, &access_test_web, ADMIN, ADMIN);
    CHECK(crux_access_account_set_role(admin, ADMIN, "viewer") ==
          CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    expect_row(CHANGE_REJECTED, DENIED, &access_test_web, ADMIN, ADMIN);

    /* Password rejected: a new name is not a known account, and a reset's target is. */
    CHECK(crux_access_account_create(admin, "bob", "viewer", "short", 5) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    expect_row(CHANGE_REJECTED, INVALID, &access_test_web, ADMIN, NULL);
    CHECK(crux_access_account_reset_password(admin, USER, "short", 5) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    expect_row(CHANGE_REJECTED, INVALID, &access_test_web, ADMIN, USER);

    /* Name exists. */
    CHECK(crux_access_account_create(admin, USER, "viewer", PASSWORD, length) ==
          CRUX_ACCESS_ERR_ALREADY_EXISTS);
    expect_row(CHANGE_REJECTED, INVALID, &access_test_web, ADMIN, USER);

    /* Target not found: unverified client input is never a field. */
    CHECK(crux_access_account_remove(admin, TYPED_NAME) == CRUX_ACCESS_ERR_NOT_FOUND);
    expect_row(CHANGE_REJECTED, INVALID, &access_test_web, ADMIN, NULL);
    CHECK(crux_access_account_add_key(admin, TYPED_NAME, key_one, sizeof(key_one)) ==
          CRUX_ACCESS_ERR_NOT_FOUND);
    expect_row(CHANGE_REJECTED, INVALID, &access_test_web, ADMIN, NULL);

    /* A malformed argument. */
    CHECK(crux_access_account_remove(admin, "Not A Name!") == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_row(CHANGE_REJECTED, INVALID, &access_test_web, ADMIN, NULL);
    const crux_access_session_t never = {0};
    CHECK(crux_access_account_remove(never, "Not A Name!") == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_row(CHANGE_REJECTED, INVALID, NULL, NULL, NULL);

    /* Wrong current password. */
    CHECK(crux_access_account_change_password(user, WRONG, strlen(WRONG), OTHER_PASSWORD,
                                              strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_row(CHANGE_REJECTED, DENIED, &access_test_web_other, USER, USER);
    /* A new password that fails the rule, on one's own account. */
    CHECK(
        crux_access_account_change_password(user, PASSWORD, length, "short", 5, &retry_after_ms) ==
        CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    expect_row(CHANGE_REJECTED, INVALID, &access_test_web_other, USER, USER);
}

static void change_failures(void)
{
    crux_access_session_t admin = boot_with_user();
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
    CHECK(crux_access_account_disable(admin, USER) == CRUX_ACCESS_ERR_STORE);
    expect_row(ID(ACCESS_CHANGE_FAILED), "access.change_failed", FAILED, &access_test_web, ADMIN,
               USER);
    /* A failed create leaves no such account to name. */
    CHECK(crux_access_account_create(admin, "bob", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_ERR_STORE);
    expect_row(ID(ACCESS_CHANGE_FAILED), "access.change_failed", FAILED, &access_test_web, ADMIN,
               NULL);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);
}

static void provisioning(void)
{
    const size_t length = strlen(PASSWORD);
    event_test_boot();
    access_test_boot(&access_test_profile_reference.policy);
    start();

    /* Refused provisioning: not serial, a bad name, a role without the permission, a bad password,
     * and a failed save. */
    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, length, "administrator",
                                              &access_test_web) == CRUX_ACCESS_ERR_DENIED);
    expect_row(PROVISION_REFUSED, DENIED, &access_test_web, NULL, NULL);
    CHECK(crux_access_provision_first_account("Not A Name!", PASSWORD, length, "administrator",
                                              &access_test_serial) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_row(PROVISION_REFUSED, INVALID, &access_test_serial, NULL, NULL);
    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, length, "viewer",
                                              &access_test_serial) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_row(PROVISION_REFUSED, INVALID, &access_test_serial, NULL, NULL);
    CHECK(crux_access_provision_first_account(ADMIN, "short", 5, "administrator",
                                              &access_test_serial) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    expect_row(PROVISION_REFUSED, INVALID, &access_test_serial, NULL, NULL);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, length, "administrator",
                                              &access_test_serial) == CRUX_ACCESS_ERR_STORE);
    expect_row(ID(ACCESS_CHANGE_FAILED), "access.change_failed", FAILED, &access_test_serial, NULL,
               NULL);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);

    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, length, "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_PROVISIONED), "access.provisioned", OK, &access_test_serial, ADMIN, NULL);

    /* Closed, once an account exists. */
    CHECK(crux_access_provision_first_account(USER, PASSWORD, length, "administrator",
                                              &access_test_serial) == CRUX_ACCESS_ERR_DENIED);
    expect_row(PROVISION_REFUSED, DENIED, &access_test_serial, NULL, NULL);
}

static void not_ready(void)
{
    crux_access_session_t session = {0x01000001u};
    event_test_boot();
    access_fake_reset();
    crux_access_test_reset();
    start();

    /* Not initialised. */
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_ERR_NOT_READY);
    expect_row(NOT_READY, FAILED, NULL, NULL, NULL);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_row(NOT_READY, FAILED, &access_test_web, NULL, NULL);

    /* Unusable store, at init and afterwards. Provisioning reports its own refusal. */
    event_test_boot();
    access_fake_reset();
    crux_access_test_reset();
    start();
    access_fake_store_state(CRUX_ACCESS_STORE_UNUSABLE);
    CHECK(crux_access_init(&access_test_profile_reference.policy) == CRUX_ACCESS_ERR_NOT_READY);
    expect_row(NOT_READY, FAILED, NULL, NULL, NULL);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_row(NOT_READY, FAILED, &access_test_web, NULL, NULL);
    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, strlen(PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_ERR_DENIED);
    expect_row(PROVISION_REFUSED, DENIED, &access_test_serial, NULL, NULL);

    /* A provider failure counts as no attempt, and names nobody. */
    (void)boot_with_user();
    crux_access_session_t none = {0};
    access_fake_derive_fail(true);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web_other, &none) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_row(NOT_READY, FAILED, &access_test_web_other, NULL, NULL);
    access_fake_derive_fail(false);
}

static void successes(void)
{
    crux_access_session_t admin = boot_with_user();
    const size_t length = strlen(PASSWORD);
    const size_t other_length = strlen(OTHER_PASSWORD);
    uint32_t retry_after_ms = 0;

    /* Each change names its actor as the user, and its target as the subject. */
    CHECK(crux_access_account_create(admin, "bob", "viewer", PASSWORD, length) == CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_ACCOUNT_CREATED), "access.account_created", OK, &access_test_web, ADMIN,
               "bob");
    CHECK(crux_access_account_disable(admin, "bob") == CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_ACCOUNT_DISABLED), "access.account_disabled", OK, &access_test_web, ADMIN,
               "bob");
    CHECK(crux_access_account_enable(admin, "bob") == CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_ACCOUNT_ENABLED), "access.account_enabled", OK, &access_test_web, ADMIN,
               "bob");
    CHECK(crux_access_account_set_role(admin, "bob", "operator") == CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_ROLE_CHANGED), "access.role_changed", OK, &access_test_web, ADMIN, "bob");
    CHECK(crux_access_account_reset_password(admin, "bob", OTHER_PASSWORD, other_length) ==
          CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_PASSWORD_RESET), "access.password_reset", OK, &access_test_web, ADMIN,
               "bob");
    CHECK(crux_access_account_add_key(admin, "bob", key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_KEY_ADDED), "access.key_added", OK, &access_test_web, ADMIN, "bob");
    CHECK(crux_access_account_remove_key(admin, "bob", key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_KEY_REMOVED), "access.key_removed", OK, &access_test_web, ADMIN, "bob");
    CHECK(crux_access_account_remove(admin, "bob") == CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_ACCOUNT_REMOVED), "access.account_removed", OK, &access_test_web, ADMIN,
               "bob");

    /* A login names its account, and an own password change is a user with no subject. */
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web_other);
    expect_row(ID(ACCESS_LOGIN_OK), "access.login_ok", OK, &access_test_web_other, USER, NULL);
    CHECK(crux_access_account_change_password(user, PASSWORD, length, OTHER_PASSWORD, other_length,
                                              &retry_after_ms) == CRUX_ACCESS_OK);
    expect_row(ID(ACCESS_PASSWORD_CHANGED), "access.password_changed", OK, &access_test_web_other,
               USER, NULL);
}

/* Each port event by itself, so that a new port event cannot go unmapped. */
static void provider_maps_each_event(void)
{
    const struct {
        crux_access_event_type_t type;
        crux_security_event_id_t id;
        const char *user;
        const char *subject;
    } rows[] = {
        {CRUX_ACCESS_EVENT_LOGIN_SUCCESS, ID(ACCESS_LOGIN_OK), "target", NULL},
        {CRUX_ACCESS_EVENT_LOGIN_FAILURE, ID(ACCESS_LOGIN_FAILED), "target", NULL},
        {CRUX_ACCESS_EVENT_KEY_FAILURE, ID(ACCESS_KEY_FAILED), "target", NULL},
        {CRUX_ACCESS_EVENT_DELAY_APPLIED, ID(ACCESS_DELAYED), NULL, NULL},
        {CRUX_ACCESS_EVENT_PERMISSION_DENIED, ID(ACCESS_PERMISSION_DENIED), "actor", "the.perm"},
        {CRUX_ACCESS_EVENT_LOGOUT, ID(ACCESS_LOGOUT), "actor", NULL},
        {CRUX_ACCESS_EVENT_SESSION_EXPIRED, ID(ACCESS_SESSION_EXPIRED), "actor", NULL},
        {CRUX_ACCESS_EVENT_PROVISIONED, ID(ACCESS_PROVISIONED), "target", NULL},
        {CRUX_ACCESS_EVENT_ACCOUNT_CREATED, ID(ACCESS_ACCOUNT_CREATED), "actor", "target"},
        {CRUX_ACCESS_EVENT_ACCOUNT_REMOVED, ID(ACCESS_ACCOUNT_REMOVED), "actor", "target"},
        {CRUX_ACCESS_EVENT_ACCOUNT_DISABLED, ID(ACCESS_ACCOUNT_DISABLED), "actor", "target"},
        {CRUX_ACCESS_EVENT_ACCOUNT_ENABLED, ID(ACCESS_ACCOUNT_ENABLED), "actor", "target"},
        {CRUX_ACCESS_EVENT_ACCOUNT_ROLE_CHANGED, ID(ACCESS_ROLE_CHANGED), "actor", "target"},
        {CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_RESET, ID(ACCESS_PASSWORD_RESET), "actor", "target"},
        {CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_CHANGED, ID(ACCESS_PASSWORD_CHANGED), "actor", NULL},
        {CRUX_ACCESS_EVENT_ACCOUNT_KEY_ADDED, ID(ACCESS_KEY_ADDED), "actor", "target"},
        {CRUX_ACCESS_EVENT_ACCOUNT_KEY_REMOVED, ID(ACCESS_KEY_REMOVED), "actor", "target"},
        {CRUX_ACCESS_EVENT_LOGIN_BUSY, ID(ACCESS_LOGIN_BUSY), NULL, NULL},
        {CRUX_ACCESS_EVENT_LOGIN_NO_SESSION, ID(ACCESS_LOGIN_NO_SESSION), NULL, NULL},
        {CRUX_ACCESS_EVENT_LOGIN_REFUSED, ID(ACCESS_LOGIN_REFUSED), "target", NULL},
        {CRUX_ACCESS_EVENT_NOT_READY, ID(ACCESS_NOT_READY), NULL, NULL},
        {CRUX_ACCESS_EVENT_SESSION_INVALID, ID(ACCESS_SESSION_INVALID), NULL, NULL},
        {CRUX_ACCESS_EVENT_PROVISION_REFUSED, ID(ACCESS_PROVISION_REFUSED), NULL, NULL},
        {CRUX_ACCESS_EVENT_CHANGE_FAILED, ID(ACCESS_CHANGE_FAILED), "actor", "target"},
        {CRUX_ACCESS_EVENT_CHANGE_REJECTED, ID(ACCESS_CHANGE_REJECTED), "actor", "target"},
    };
    CHECK(sizeof(rows) / sizeof(rows[0]) == CRUX_ACCESS_EVENT_CHANGE_REJECTED + 1u);
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        event_test_boot();
        crux_access_event_t event = {.type = rows[index].type,
                                     .result = CRUX_ACCESS_OK,
                                     .origin = access_test_web,
                                     .actor = "actor",
                                     .account = "target",
                                     .permission = "the.perm"};
        __real_crux_access_port_event(&event);
        uint32_t read_cursor = 0;
        crux_security_event_record_t record;
        CHECK(crux_security_event_test_read(&read_cursor, &record) == CRUX_SECURITY_READ_OK);
        CHECK(record.id == rows[index].id);
        CHECK(strcmp(record.user, rows[index].user == NULL ? "" : rows[index].user) == 0);
        CHECK(strcmp(record.subject, rows[index].subject == NULL ? "" : rows[index].subject) == 0);
        CHECK(record.channel == 2);
        CHECK(record.source_length == 4);
        CHECK(memcmp(record.source, access_test_web.source, 4) == 0);
    }
}

/* Each access result maps to one event result. */
static void provider_maps_each_result(void)
{
    const struct {
        crux_access_result_t from;
        crux_security_event_result_t to;
    } rows[] = {
        {CRUX_ACCESS_OK, CRUX_SECURITY_EVENT_OK},
        {CRUX_ACCESS_ERR_INVALID_ARGUMENT, CRUX_SECURITY_EVENT_INVALID},
        {CRUX_ACCESS_ERR_NOT_READY, CRUX_SECURITY_EVENT_FAILED},
        {CRUX_ACCESS_ERR_DENIED, CRUX_SECURITY_EVENT_DENIED},
        {CRUX_ACCESS_ERR_SESSION_INVALID, CRUX_SECURITY_EVENT_DENIED},
        {CRUX_ACCESS_ERR_RETRY_LATER, CRUX_SECURITY_EVENT_DELAYED},
        {CRUX_ACCESS_ERR_BUSY, CRUX_SECURITY_EVENT_BUSY},
        {CRUX_ACCESS_ERR_NO_RESOURCES, CRUX_SECURITY_EVENT_LIMIT},
        {CRUX_ACCESS_ERR_NOT_FOUND, CRUX_SECURITY_EVENT_INVALID},
        {CRUX_ACCESS_ERR_ALREADY_EXISTS, CRUX_SECURITY_EVENT_INVALID},
        {CRUX_ACCESS_ERR_LAST_ADMINISTRATOR, CRUX_SECURITY_EVENT_DENIED},
        {CRUX_ACCESS_ERR_PASSWORD_REJECTED, CRUX_SECURITY_EVENT_INVALID},
        {CRUX_ACCESS_ERR_STORE, CRUX_SECURITY_EVENT_FAILED},
    };
    CHECK(sizeof(rows) / sizeof(rows[0]) == 13u);
    for (size_t index = 0; index < sizeof(rows) / sizeof(rows[0]); ++index) {
        event_test_boot();
        crux_access_event_t event = {.type = CRUX_ACCESS_EVENT_CHANGE_REJECTED,
                                     .result = rows[index].from};
        __real_crux_access_port_event(&event);
        uint32_t read_cursor = 0;
        crux_security_event_record_t record;
        CHECK(crux_security_event_test_read(&read_cursor, &record) == CRUX_SECURITY_READ_OK);
        CHECK(record.result == rows[index].to);
        /* An empty field in the port's event is no field, and no failed rule. */
        CHECK(record.user[0] == '\0' && record.subject[0] == '\0');
        CHECK(record.channel == 0 && record.source_length == 0);
        CHECK(event_test_stats().invalid_fields == 0);
    }

    /* A value this table does not know is a failure with no meaning claimed. */
    event_test_boot();
    crux_access_event_t event = {.type = CRUX_ACCESS_EVENT_CHANGE_REJECTED,
                                 .result = (crux_access_result_t)-99};
    __real_crux_access_port_event(&event);
    uint32_t read_cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_test_read(&read_cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.result == CRUX_SECURITY_EVENT_FAILED);

    /* A port event with a type this table does not know is recorded as unknown, and counted. */
    event_test_boot();
    crux_access_event_t odd = {.type = (crux_access_event_type_t)999, .result = CRUX_ACCESS_OK};
    __real_crux_access_port_event(&odd);
    read_cursor = 0;
    CHECK(crux_security_event_test_read(&read_cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN);
    CHECK(event_test_stats().invalid_fields == 1);
}

int main(void)
{
    login_failures();
    key_failures();
    delay_window();
    busy_slot();
    busy_slot_on_own_password();
    full_tables();
    removed_role();
    session_rejections();
    change_rejections();
    change_failures();
    provisioning();
    not_ready();
    successes();
    provider_maps_each_event();
    provider_maps_each_result();
    int access_status = access_test_finish("Security event rejection paths");
    int event_status = event_test_finish("Security event rejection paths, support");
    return access_status != 0 || event_status != 0 ? 1 : 0;
}
