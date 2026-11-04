/* Each rejection path produces its event, with the fields that the events contract reads. Every
 * event in this suite also proves that it runs with no lock held. */
#include "access_test.h"

#define ADMIN ACCESS_TEST_ADMIN
#define USER ACCESS_TEST_USER
#define PASSWORD ACCESS_TEST_PASSWORD
#define OTHER_PASSWORD ACCESS_TEST_OTHER_PASSWORD
#define WRONG "wrong-password-here"
#define TYPED_NAME "typed-password-as-name"

static const uint8_t key_one[] = ACCESS_TEST_ED25519_KEY(0x11);
static const uint8_t key_two[] = ACCESS_TEST_ED25519_KEY(0x22);
static const uint8_t key_three[] = ACCESS_TEST_ED25519_KEY(0x33);

static void hook_expect_no_lock_held(void *context)
{
    (void)context;
    if (!crux_access_state.locks_created) {
        return;
    }
    const crux_mutex_t locks[] = {crux_access_state.state_lock, crux_access_state.change_lock};
    for (size_t index = 0; index < 2; ++index) {
        crux_status_t status = crux_mutex_lock(locks[index], 0);
        CHECK(status == CRUX_OK);
        if (status == CRUX_OK) {
            (void)crux_mutex_unlock(locks[index]);
        }
    }
}

static const crux_access_policy_t *policy(void)
{
    return &access_test_profile_reference.policy;
}

static crux_access_session_t boot_with_user(void)
{
    access_test_boot_with_admin();
    access_fake_event_hook(hook_expect_no_lock_held, NULL);
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_web);
    access_test_create_user(admin);
    access_fake_events_clear();
    return admin;
}

/* Exactly one event since the last clear: this actor, on this channel, acting on this account. NULL
 * means the field is empty. */
static void expect_event(crux_access_event_type_t type, crux_access_result_t result,
                         const char *actor, crux_access_channel_t channel, const char *account)
{
    CHECK(access_fake_event_count() == 1);
    const crux_access_event_t *event = access_fake_event(0);
    if (event == NULL) {
        return;
    }
    CHECK(event->type == type);
    CHECK(event->result == result);
    CHECK((int)event->origin.channel == (int)channel);
    CHECK(strcmp(event->actor, actor == NULL ? "" : actor) == 0);
    CHECK(strcmp(event->account, account == NULL ? "" : account) == 0);
    access_fake_events_clear();
}

static const crux_access_channel_t no_channel = (crux_access_channel_t)0;

static void login_rejections(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;

    CHECK(crux_access_account_create(admin, "disabled", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_disable(admin, "disabled") == CRUX_ACCESS_OK);
    access_fake_events_clear();

    crux_access_origin_t origin = access_test_web_from(1);
    CHECK(access_test_login(USER, WRONG, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, USER);
    CHECK(access_test_login("no-such-user", WRONG, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(access_test_login("disabled", PASSWORD, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, "disabled");

    /* A bad name form and an over-long password. Neither names anyone. */
    CHECK(access_test_login("Not A Name!", WRONG, &origin, &session) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_INVALID_ARGUMENT, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_login_password(USER, WRONG, CRUX_ACCESS_PASSWORD_MAX + 1u, &origin, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);

    /* A delay window. */
    crux_access_origin_t delayed = access_test_web_from(2);
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        (void)access_test_login(USER, WRONG, &delayed, &session);
    }
    access_fake_events_clear();
    CHECK(access_test_login(USER, WRONG, &delayed, &session) == CRUX_ACCESS_ERR_RETRY_LATER);
    expect_event(CRUX_ACCESS_EVENT_DELAY_APPLIED, CRUX_ACCESS_ERR_RETRY_LATER, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
}

static void hook_login_while_verifying(void *context)
{
    crux_access_session_t session = {0};
    crux_access_origin_t origin = access_test_web_from(3);
    CHECK(access_test_login(USER, PASSWORD, &origin, &session) == CRUX_ACCESS_ERR_BUSY);
    *(size_t *)context = access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGIN_BUSY);
}

static void busy_and_full_tables(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t session = {0};

    size_t busy_events = 0;
    access_fake_derive_hook(hook_login_while_verifying, &busy_events);
    (void)access_test_session(USER, PASSWORD, &access_test_web_other);
    CHECK(busy_events == 1);
    access_fake_events_clear();

    /* Fill the session table, which the administrator's session already occupies. */
    CHECK(crux_access_account_add_key(admin, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    char name[16];
    size_t network_slots = CRUX_ACCESS_SESSION_MAX - 1u;
    size_t used = 2;
    for (unsigned account = 0; used < network_slots; ++account) {
        snprintf(name, sizeof(name), "filler-%u", account);
        CHECK(crux_access_account_create(admin, name, "viewer", PASSWORD, strlen(PASSWORD)) ==
              CRUX_ACCESS_OK);
        for (unsigned count = 0; count < CRUX_ACCESS_SESSIONS_PER_ACCOUNT && used < network_slots;
             ++count, ++used) {
            crux_access_origin_t origin = access_test_web_from(100u + used);
            (void)access_test_session(name, PASSWORD, &origin);
        }
    }
    access_fake_events_clear();
    crux_access_origin_t origin = access_test_web_from(99);
    CHECK(access_test_login(USER, PASSWORD, &origin, &session) == CRUX_ACCESS_ERR_NO_RESOURCES);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_NO_SESSION, CRUX_ACCESS_ERR_NO_RESOURCES, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_login_verified(USER, key_one, sizeof(key_one), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_NO_RESOURCES);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_NO_SESSION, CRUX_ACCESS_ERR_NO_RESOURCES, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, NULL);
}

static void key_rejections(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    CHECK(crux_access_account_add_key(admin, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    access_fake_events_clear();

    /* An unsigned probe records nothing, and a signed request with a bad key does. */
    CHECK(crux_access_key_is_authorised(USER, key_two, sizeof(key_two), false, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event_count() == 0);
    CHECK(crux_access_key_is_authorised(USER, key_two, sizeof(key_two), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_KEY_FAILURE, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, USER);
    CHECK(crux_access_key_is_authorised("Not A Name!", key_two, sizeof(key_two), false,
                                        &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(access_fake_event_count() == 0);
    CHECK(crux_access_key_is_authorised("Not A Name!", key_two, sizeof(key_two), true,
                                        &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_FAILURE, CRUX_ACCESS_ERR_INVALID_ARGUMENT, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, NULL);

    /* A failed signature, naming a known account only. */
    crux_access_report_key_failure(USER, &access_test_ssh);
    expect_event(CRUX_ACCESS_EVENT_KEY_FAILURE, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, USER);
    crux_access_report_key_failure(TYPED_NAME, &access_test_ssh);
    expect_event(CRUX_ACCESS_EVENT_KEY_FAILURE, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, NULL);

    /* A refused verified login. */
    CHECK(crux_access_login_verified(USER, key_two, sizeof(key_two), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, USER);
    CHECK(crux_access_login_verified("no-such-user", key_two, sizeof(key_two), &access_test_ssh,
                                     &session) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, NULL);

    /* A forced change refuses key login at both steps, with the client's results unchanged. */
    CHECK(crux_access_account_reset_password(admin, USER, OTHER_PASSWORD, strlen(OTHER_PASSWORD)) ==
          CRUX_ACCESS_OK);
    access_fake_events_clear();
    CHECK(crux_access_key_is_authorised(USER, key_one, sizeof(key_one), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, USER);
    CHECK(crux_access_login_verified(USER, key_one, sizeof(key_one), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, USER);
}

static void removed_role(void)
{
    crux_access_session_t admin = boot_with_user();
    CHECK(crux_access_account_add_key(admin, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    static const char *const manage[] = {CRUX_ACCESS_PERMISSION_MANAGE};
    static const crux_access_role_t reduced_roles[] = {{"administrator", manage, 1}};
    static const crux_access_policy_t reduced = {reduced_roles, 1, 900, 28800, NULL};
    access_test_power_cycle(&reduced);
    access_fake_events_clear();
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;

    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event(0)->role_missing);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, USER);
    CHECK(crux_access_key_is_authorised(USER, key_one, sizeof(key_one), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event(0)->role_missing);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, USER);
    CHECK(crux_access_login_verified(USER, key_one, sizeof(key_one), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event(0)->role_missing);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, USER);
}

static void session_rejections(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web_other);
    access_fake_events_clear();
    char name[CRUX_ACCESS_NAME_SIZE];
    bool required = false;

    /* A permission denial has a subject and an actor. */
    CHECK(crux_access_check(user, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_ERR_DENIED);
    CHECK(strcmp(access_fake_event(0)->permission, CRUX_ACCESS_PERMISSION_MANAGE) == 0);
    CHECK(access_fake_event(0)->origin.source[3] == access_test_web_other.source[3]);
    expect_event(CRUX_ACCESS_EVENT_PERMISSION_DENIED, CRUX_ACCESS_ERR_DENIED, USER,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_check(user, "firmware.upgrade") == CRUX_ACCESS_ERR_DENIED);
    CHECK(strcmp(access_fake_event(0)->permission, "firmware.upgrade") == 0);
    expect_event(CRUX_ACCESS_EVENT_PERMISSION_DENIED, CRUX_ACCESS_ERR_DENIED, USER,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);

    /* An account operation's denial names the permission it needed. */
    CHECK(crux_access_account_disable(user, ADMIN) == CRUX_ACCESS_ERR_DENIED);
    CHECK(strcmp(access_fake_event(0)->permission, CRUX_ACCESS_PERMISSION_MANAGE) == 0);
    expect_event(CRUX_ACCESS_EVENT_PERMISSION_DENIED, CRUX_ACCESS_ERR_DENIED, USER,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);

    /* A stale session has no channel to report, on every operation that takes a session. */
    crux_access_session_t stale = user;
    crux_access_logout(&user);
    expect_event(CRUX_ACCESS_EVENT_LOGOUT, CRUX_ACCESS_OK, USER, CRUX_ACCESS_CHANNEL_WEB, NULL);
    size_t count = 0;
    CHECK(crux_access_check(stale, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_INVALID, CRUX_ACCESS_ERR_SESSION_INVALID, NULL,
                 no_channel, NULL);
    CHECK(crux_access_touch(stale) == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_INVALID, CRUX_ACCESS_ERR_SESSION_INVALID, NULL,
                 no_channel, NULL);
    CHECK(crux_access_session_user(stale, name, sizeof(name)) == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_INVALID, CRUX_ACCESS_ERR_SESSION_INVALID, NULL,
                 no_channel, NULL);
    CHECK(crux_access_session_role(stale, name, sizeof(name)) == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_INVALID, CRUX_ACCESS_ERR_SESSION_INVALID, NULL,
                 no_channel, NULL);
    CHECK(crux_access_session_must_change_password(stale, &required) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_INVALID, CRUX_ACCESS_ERR_SESSION_INVALID, NULL,
                 no_channel, NULL);
    CHECK(crux_access_account_list(stale, NULL, 0, &count) == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_INVALID, CRUX_ACCESS_ERR_SESSION_INVALID, NULL,
                 no_channel, NULL);
    CHECK(crux_access_account_disable(stale, ADMIN) == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_INVALID, CRUX_ACCESS_ERR_SESSION_INVALID, NULL,
                 no_channel, NULL);
    /* Logging out a reference that already ended is not a rejection. */
    crux_access_session_t ended = stale;
    crux_access_logout(&ended);
    CHECK(access_fake_event_count() == 0);

    /* An expired session is reported as expired once, and then as stale. */
    access_fake_clock_advance((uint64_t)policy()->session_idle_limit_s * 1000u);
    CHECK(crux_access_check(admin, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_EXPIRED, CRUX_ACCESS_ERR_SESSION_INVALID, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_check(admin, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_INVALID, CRUX_ACCESS_ERR_SESSION_INVALID, NULL,
                 no_channel, NULL);
}

static void change_rejections(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web_other);
    uint32_t retry_after_ms = 0;
    const size_t length = strlen(PASSWORD);
    access_fake_events_clear();

    /* Last administrator. */
    CHECK(crux_access_account_disable(admin, ADMIN) == CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_LAST_ADMINISTRATOR, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, ADMIN);
    CHECK(crux_access_account_remove(admin, ADMIN) == CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_LAST_ADMINISTRATOR, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, ADMIN);
    CHECK(crux_access_account_set_role(admin, ADMIN, "viewer") ==
          CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_LAST_ADMINISTRATOR, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, ADMIN);

    /* A password the rule refuses: a new name is not a known account, and a reset's target is. */
    CHECK(crux_access_account_create(admin, "bob", "viewer", "short", 5) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_PASSWORD_REJECTED, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_reset_password(admin, USER, "short", 5) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_PASSWORD_REJECTED, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, USER);
    CHECK(
        crux_access_account_change_password(user, PASSWORD, length, "short", 5, &retry_after_ms) ==
        CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_PASSWORD_REJECTED, USER,
                 CRUX_ACCESS_CHANNEL_WEB, USER);

    /* A name that exists, and a target that does not. Unverified input is never a field. */
    CHECK(crux_access_account_create(admin, USER, "viewer", PASSWORD, length) ==
          CRUX_ACCESS_ERR_ALREADY_EXISTS);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_ALREADY_EXISTS, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, USER);
    CHECK(crux_access_account_remove(admin, TYPED_NAME) == CRUX_ACCESS_ERR_NOT_FOUND);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_NOT_FOUND, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_disable(admin, TYPED_NAME) == CRUX_ACCESS_ERR_NOT_FOUND);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_NOT_FOUND, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_enable(admin, TYPED_NAME) == CRUX_ACCESS_ERR_NOT_FOUND);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_NOT_FOUND, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_set_role(admin, TYPED_NAME, "viewer") == CRUX_ACCESS_ERR_NOT_FOUND);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_NOT_FOUND, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_reset_password(admin, TYPED_NAME, PASSWORD, length) ==
          CRUX_ACCESS_ERR_NOT_FOUND);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_NOT_FOUND, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_add_key(admin, TYPED_NAME, key_one, sizeof(key_one)) ==
          CRUX_ACCESS_ERR_NOT_FOUND);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_NOT_FOUND, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_remove_key(admin, TYPED_NAME, key_one, sizeof(key_one)) ==
          CRUX_ACCESS_ERR_NOT_FOUND);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_NOT_FOUND, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);

    /* Invalid arguments never name a target, and name the actor only for a live session. */
    CHECK(crux_access_account_remove(admin, "Not A Name!") == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_INVALID_ARGUMENT, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_create(admin, "bob", "no-such-role", PASSWORD, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_INVALID_ARGUMENT, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_set_role(admin, USER, "no-such-role") ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_INVALID_ARGUMENT, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    const uint8_t not_a_blob[] = {1, 2, 3};
    CHECK(crux_access_account_add_key(admin, USER, not_a_blob, sizeof(not_a_blob)) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_INVALID_ARGUMENT, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    const crux_access_session_t never = {0};
    CHECK(crux_access_account_remove(never, "Not A Name!") == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_INVALID_ARGUMENT, NULL,
                 no_channel, NULL);

    /* A full key table names its target, and a repeated key does too. */
    CHECK(crux_access_account_add_key(admin, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_add_key(admin, USER, key_two, sizeof(key_two)) == CRUX_ACCESS_OK);
    access_fake_events_clear();
    CHECK(crux_access_account_add_key(admin, USER, key_two, sizeof(key_two)) ==
          CRUX_ACCESS_ERR_ALREADY_EXISTS);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_ALREADY_EXISTS, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, USER);
    CHECK(crux_access_account_add_key(admin, USER, key_three, sizeof(key_three)) ==
          CRUX_ACCESS_ERR_NO_RESOURCES);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_NO_RESOURCES, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, USER);

    /* A wrong current password, and an over-long one. */
    CHECK(crux_access_account_change_password(user, WRONG, strlen(WRONG), OTHER_PASSWORD,
                                              strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_DENIED, USER,
                 CRUX_ACCESS_CHANNEL_WEB, USER);
    CHECK(crux_access_account_change_password(user, WRONG, CRUX_ACCESS_PASSWORD_MAX + 1u,
                                              OTHER_PASSWORD, strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_DENIED, USER,
                 CRUX_ACCESS_CHANNEL_WEB, USER);

    /* A full account table names nobody, because the new name is not an account. */
    char name[16];
    for (size_t index = 2; index < CRUX_ACCESS_ACCOUNT_MAX; ++index) {
        snprintf(name, sizeof(name), "user-%zu", index);
        CHECK(crux_access_account_create(admin, name, "viewer", PASSWORD, length) ==
              CRUX_ACCESS_OK);
    }
    access_fake_events_clear();
    CHECK(crux_access_account_create(admin, "one-too-many", "viewer", PASSWORD, length) ==
          CRUX_ACCESS_ERR_NO_RESOURCES);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_REJECTED, CRUX_ACCESS_ERR_NO_RESOURCES, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
}

static void hook_change_password_while_verifying(void *context)
{
    const crux_access_session_t *session = context;
    uint32_t retry_after_ms = 0;
    access_fake_events_clear();
    CHECK(crux_access_account_change_password(*session, PASSWORD, strlen(PASSWORD), OTHER_PASSWORD,
                                              strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_ERR_BUSY);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_BUSY, CRUX_ACCESS_ERR_BUSY, NULL, CRUX_ACCESS_CHANNEL_WEB,
                 NULL);
}

/* An own password change follows the login delay and verification slot, with the same events. */
static void own_change_refusals(void)
{
    (void)boot_with_user();
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web_other);
    uint32_t retry_after_ms = 0;
    const size_t other_length = strlen(OTHER_PASSWORD);

    access_fake_derive_hook(hook_change_password_while_verifying, &user);
    CHECK(crux_access_account_change_password(user, PASSWORD, strlen(PASSWORD), OTHER_PASSWORD,
                                              other_length, &retry_after_ms) == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_CHANGED, CRUX_ACCESS_OK, USER,
                 CRUX_ACCESS_CHANNEL_WEB, USER);

    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        CHECK(crux_access_account_change_password(user, WRONG, strlen(WRONG), OTHER_PASSWORD,
                                                  other_length,
                                                  &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    }
    access_fake_events_clear();
    CHECK(crux_access_account_change_password(user, WRONG, strlen(WRONG), OTHER_PASSWORD,
                                              other_length,
                                              &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms > 0);
    expect_event(CRUX_ACCESS_EVENT_DELAY_APPLIED, CRUX_ACCESS_ERR_RETRY_LATER, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
}

/* What a forced change refuses names the permission it would have needed. */
static void forced_change_denial(void)
{
    crux_access_session_t admin = boot_with_user();
    CHECK(crux_access_account_reset_password(admin, USER, OTHER_PASSWORD, strlen(OTHER_PASSWORD)) ==
          CRUX_ACCESS_OK);
    crux_access_session_t forced =
        access_test_session(USER, OTHER_PASSWORD, &access_test_web_other);
    access_fake_events_clear();
    CHECK(crux_access_account_add_key(forced, USER, key_one, sizeof(key_one)) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(strcmp(access_fake_event(0)->permission, CRUX_ACCESS_PERMISSION_SELF) == 0);
    expect_event(CRUX_ACCESS_EVENT_PERMISSION_DENIED, CRUX_ACCESS_ERR_DENIED, USER,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_check(forced, CRUX_ACCESS_PERMISSION_SELF) == CRUX_ACCESS_ERR_DENIED);
    CHECK(strcmp(access_fake_event(0)->permission, CRUX_ACCESS_PERMISSION_SELF) == 0);
    expect_event(CRUX_ACCESS_EVENT_PERMISSION_DENIED, CRUX_ACCESS_ERR_DENIED, USER,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
}

static void changes_name_actor_and_target(void)
{
    crux_access_session_t admin = boot_with_user();
    const size_t length = strlen(PASSWORD);
    const size_t other_length = strlen(OTHER_PASSWORD);
    uint32_t retry_after_ms = 0;

    CHECK(crux_access_account_create(admin, "bob", "viewer", PASSWORD, length) == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_CREATED, CRUX_ACCESS_OK, ADMIN, CRUX_ACCESS_CHANNEL_WEB,
                 "bob");
    CHECK(crux_access_account_disable(admin, "bob") == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_DISABLED, CRUX_ACCESS_OK, ADMIN, CRUX_ACCESS_CHANNEL_WEB,
                 "bob");
    CHECK(crux_access_account_enable(admin, "bob") == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_ENABLED, CRUX_ACCESS_OK, ADMIN, CRUX_ACCESS_CHANNEL_WEB,
                 "bob");
    CHECK(crux_access_account_set_role(admin, "bob", "operator") == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_ROLE_CHANGED, CRUX_ACCESS_OK, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, "bob");
    CHECK(crux_access_account_reset_password(admin, "bob", OTHER_PASSWORD, other_length) ==
          CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_RESET, CRUX_ACCESS_OK, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, "bob");
    CHECK(crux_access_account_add_key(admin, "bob", key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_KEY_ADDED, CRUX_ACCESS_OK, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, "bob");
    CHECK(crux_access_account_remove_key(admin, "bob", key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_KEY_REMOVED, CRUX_ACCESS_OK, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, "bob");
    CHECK(crux_access_account_remove(admin, "bob") == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_REMOVED, CRUX_ACCESS_OK, ADMIN, CRUX_ACCESS_CHANNEL_WEB,
                 "bob");

    /* An account changing its own password is both actor and target. */
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web_other);
    access_fake_events_clear();
    CHECK(crux_access_account_change_password(user, PASSWORD, length, OTHER_PASSWORD, other_length,
                                              &retry_after_ms) == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_CHANGED, CRUX_ACCESS_OK, USER,
                 CRUX_ACCESS_CHANNEL_WEB, USER);

    /* A login names its account, and has no actor because no session acted. */
    (void)access_test_session(USER, OTHER_PASSWORD, &access_test_web_other);
    expect_event(CRUX_ACCESS_EVENT_LOGIN_SUCCESS, CRUX_ACCESS_OK, NULL, CRUX_ACCESS_CHANNEL_WEB,
                 USER);
}

static void change_failures(void)
{
    crux_access_session_t admin = boot_with_user();
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
    CHECK(crux_access_account_disable(admin, USER) == CRUX_ACCESS_ERR_STORE);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_FAILED, CRUX_ACCESS_ERR_STORE, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, USER);
    /* A failed create leaves no such account to name. */
    CHECK(crux_access_account_create(admin, "bob", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_ERR_STORE);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_FAILED, CRUX_ACCESS_ERR_STORE, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);
}

static void provisioning_rejections(void)
{
    const size_t length = strlen(PASSWORD);
    access_test_boot(policy());
    access_fake_event_hook(hook_expect_no_lock_held, NULL);

    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, length, "administrator",
                                              &access_test_web) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_PROVISION_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_provision_first_account("Not A Name!", PASSWORD, length, "administrator",
                                              &access_test_serial) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_event(CRUX_ACCESS_EVENT_PROVISION_REFUSED, CRUX_ACCESS_ERR_INVALID_ARGUMENT, NULL,
                 CRUX_ACCESS_CHANNEL_SERIAL, NULL);
    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, length, "viewer",
                                              &access_test_serial) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    expect_event(CRUX_ACCESS_EVENT_PROVISION_REFUSED, CRUX_ACCESS_ERR_INVALID_ARGUMENT, NULL,
                 CRUX_ACCESS_CHANNEL_SERIAL, NULL);
    CHECK(crux_access_provision_first_account(ADMIN, "short", 5, "administrator",
                                              &access_test_serial) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    expect_event(CRUX_ACCESS_EVENT_PROVISION_REFUSED, CRUX_ACCESS_ERR_PASSWORD_REJECTED, NULL,
                 CRUX_ACCESS_CHANNEL_SERIAL, NULL);

    /* A failed save names nobody, because no such account exists. */
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, length, "administrator",
                                              &access_test_serial) == CRUX_ACCESS_ERR_STORE);
    expect_event(CRUX_ACCESS_EVENT_CHANGE_FAILED, CRUX_ACCESS_ERR_STORE, NULL,
                 CRUX_ACCESS_CHANNEL_SERIAL, NULL);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);

    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, length, "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    expect_event(CRUX_ACCESS_EVENT_PROVISIONED, CRUX_ACCESS_OK, NULL, CRUX_ACCESS_CHANNEL_SERIAL,
                 ADMIN);

    /* Provisioning is closed once an account exists. */
    CHECK(crux_access_provision_first_account(USER, PASSWORD, length, "administrator",
                                              &access_test_serial) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_PROVISION_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SERIAL, NULL);
}

static void not_ready_rejections(void)
{
    crux_access_session_t session = {0x01000001u};
    char name[CRUX_ACCESS_NAME_SIZE];
    bool required = false;
    uint32_t retry_after_ms = 0;
    size_t count = 0;

    /* Not initialised. */
    access_fake_reset();
    crux_access_test_reset();
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL, no_channel, NULL);
    CHECK(crux_access_touch(session) == CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL, no_channel, NULL);
    CHECK(crux_access_session_user(session, name, sizeof(name)) == CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL, no_channel, NULL);
    CHECK(crux_access_session_must_change_password(session, &required) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL, no_channel, NULL);
    CHECK(crux_access_account_list(session, NULL, 0, &count) == CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL, no_channel, NULL);
    CHECK(crux_access_account_remove(session, USER) == CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL, no_channel, NULL);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_key_is_authorised(USER, key_one, sizeof(key_one), true, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, NULL);
    CHECK(crux_access_login_verified(USER, key_one, sizeof(key_one), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, NULL);
    crux_access_report_key_failure(USER, &access_test_ssh);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL,
                 CRUX_ACCESS_CHANNEL_SSH, NULL);
    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, strlen(PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL,
                 CRUX_ACCESS_CHANNEL_SERIAL, NULL);

    /* An unusable store, at init and afterwards. Provisioning reports its own refusal. */
    access_fake_reset();
    crux_access_test_reset();
    access_fake_store_state(CRUX_ACCESS_STORE_UNUSABLE);
    CHECK(crux_access_init(policy()) == CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL, no_channel, NULL);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, strlen(PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_ERR_DENIED);
    expect_event(CRUX_ACCESS_EVENT_PROVISION_REFUSED, CRUX_ACCESS_ERR_DENIED, NULL,
                 CRUX_ACCESS_CHANNEL_SERIAL, NULL);

    /* A provider failure, which names nobody and counts as no attempt. */
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t none = {0};
    access_fake_derive_fail(true);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web_other, &none) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    CHECK(crux_access_account_create(admin, "bob", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    access_fake_derive_fail(false);
    access_fake_hmac_fail(true);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web_other, &none) ==
          CRUX_ACCESS_ERR_NOT_READY);
    expect_event(CRUX_ACCESS_EVENT_NOT_READY, CRUX_ACCESS_ERR_NOT_READY, NULL,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);
    access_fake_hmac_fail(false);
}

/* The two queries emit nothing and change nothing, whatever the session's state. */
static void queries_stay_silent(void)
{
    crux_access_session_t admin = boot_with_user();
    crux_access_session_t stale = access_test_session(USER, PASSWORD, &access_test_web_other);
    crux_access_session_t ended = stale;
    crux_access_logout(&ended);
    access_fake_events_clear();
    bool permitted = false;
    uint8_t channel = 0, source[16] = {0}, source_length = 0;

    CHECK(crux_access_permitted(admin, CRUX_ACCESS_PERMISSION_MANAGE, &permitted) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_permitted(admin, "firmware.upgrade", &permitted) == CRUX_ACCESS_OK);
    CHECK(crux_access_permitted(stale, "status.read", &permitted) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_access_session_origin(admin, &channel, source, &source_length) == CRUX_ACCESS_OK);
    CHECK(crux_access_session_origin(stale, &channel, source, &source_length) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(access_fake_event_count() == 0);

    access_fake_clock_advance((uint64_t)policy()->session_idle_limit_s * 1000u);
    CHECK(crux_access_permitted(admin, "status.read", &permitted) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_access_session_origin(admin, &channel, source, &source_length) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(access_fake_event_count() == 0);
    /* The expiry was left for the next operation to report. */
    CHECK(crux_access_check(admin, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    expect_event(CRUX_ACCESS_EVENT_SESSION_EXPIRED, CRUX_ACCESS_ERR_SESSION_INVALID, ADMIN,
                 CRUX_ACCESS_CHANNEL_WEB, NULL);

    access_fake_reset();
    crux_access_test_reset();
    CHECK(crux_access_permitted(admin, "status.read", &permitted) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_session_origin(admin, &channel, source, &source_length) ==
          CRUX_ACCESS_ERR_NOT_READY);
    CHECK(access_fake_event_count() == 0);
}

int main(void)
{
    login_rejections();
    busy_and_full_tables();
    key_rejections();
    removed_role();
    session_rejections();
    change_rejections();
    own_change_refusals();
    forced_change_denial();
    changes_name_actor_and_target();
    change_failures();
    provisioning_rejections();
    not_ready_rejections();
    queries_stay_silent();
    return access_test_finish("Access events");
}
