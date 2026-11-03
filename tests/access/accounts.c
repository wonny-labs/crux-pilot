/* Account operations: success, unauthenticated, unpermitted and invalid input for each, then the
 * rules on sessions, last administrator, forced change, persistence and concurrent change. */
#include "access_test.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#define ADMIN ACCESS_TEST_ADMIN
#define USER ACCESS_TEST_USER
#define PASSWORD ACCESS_TEST_PASSWORD
#define OTHER_PASSWORD ACCESS_TEST_OTHER_PASSWORD

static const uint8_t key_one[] = {0x11, 0x22, 0x33, 0x44};
static const uint8_t key_two[] = {0x55, 0x66};
static const uint8_t key_three[] = {0x77};

static const crux_access_policy_t *reference(void)
{
    return &access_test_profile_reference.policy;
}

static crux_access_session_t admin_session(void)
{
    return access_test_session(ADMIN, PASSWORD, &access_test_web);
}

static bool account_info(crux_access_session_t admin, const char *user,
                         crux_access_account_info_t *info)
{
    crux_access_account_info_t accounts[CRUX_ACCESS_ACCOUNT_MAX];
    size_t count = 0;
    memset(info, 0, sizeof(*info));
    CHECK(crux_access_account_list(admin, accounts, CRUX_ACCESS_ACCOUNT_MAX, &count) ==
          CRUX_ACCESS_OK);
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(accounts[index].user, user) == 0) {
            *info = accounts[index];
            return true;
        }
    }
    return false;
}

/* Every operation refuses an invalid session and a session without the permission. */
static void refusal_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = admin_session();
    access_test_create_user(admin);
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web);
    crux_access_session_t none = {0};
    crux_access_account_info_t info;
    size_t count = 0;
    const size_t length = strlen(PASSWORD);

    const crux_access_session_t subjects[] = {none, user};
    const crux_access_result_t expected[] = {CRUX_ACCESS_ERR_SESSION_INVALID,
                                             CRUX_ACCESS_ERR_DENIED};
    for (size_t index = 0; index < 2; ++index) {
        crux_access_session_t subject = subjects[index];
        crux_access_result_t refused = expected[index];
        unsigned saves = access_fake_store_saves();
        access_fake_derive_counters_clear();
        CHECK(crux_access_account_list(subject, &info, 1, &count) == refused);
        CHECK(crux_access_account_create(subject, "bob", "viewer", PASSWORD, length) == refused);
        CHECK(crux_access_account_remove(subject, ADMIN) == refused);
        CHECK(crux_access_account_disable(subject, ADMIN) == refused);
        CHECK(crux_access_account_enable(subject, ADMIN) == refused);
        CHECK(crux_access_account_set_role(subject, USER, "administrator") == refused);
        CHECK(crux_access_account_reset_password(subject, ADMIN, PASSWORD, length) == refused);
        CHECK(crux_access_account_add_key(subject, ADMIN, key_one, sizeof(key_one)) == refused);
        CHECK(crux_access_account_remove_key(subject, ADMIN, key_one, sizeof(key_one)) == refused);
        CHECK(access_fake_store_saves() == saves);
        CHECK(access_fake_derive_count() == 0);
    }
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) >= 8);
    /* Only the own-password operation refuses an invalid session this way; it has no permission. */
    uint32_t retry_after_ms = 0;
    CHECK(crux_access_account_change_password(none, PASSWORD, length, OTHER_PASSWORD,
                                              strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_ERR_SESSION_INVALID);

    /* Invalid input. */
    CHECK(crux_access_account_list(admin, NULL, 1, &count) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_list(admin, &info, 1, NULL) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_create(admin, "Bob", "viewer", PASSWORD, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_create(admin, NULL, "viewer", PASSWORD, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_create(admin, "bob", "no-role", PASSWORD, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_create(admin, "bob", NULL, PASSWORD, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_create(admin, "bob", "viewer", NULL, length) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_create(admin, "bob", "viewer", "short", 5) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    char longest[CRUX_ACCESS_PASSWORD_MAX + 1];
    memset(longest, 'x', sizeof(longest));
    CHECK(crux_access_account_create(admin, "bob", "viewer", longest, sizeof(longest)) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    CHECK(crux_access_account_remove(admin, "") == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_disable(admin, NULL) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_enable(admin, "A") == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_set_role(admin, USER, "no-role") == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_reset_password(admin, USER, "short", 5) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    CHECK(crux_access_account_add_key(admin, USER, NULL, 4) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_add_key(admin, USER, key_one, 0) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_add_key(admin, USER, key_one, CRUX_ACCESS_KEY_SIZE_MAX + 1u) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_remove_key(admin, USER, NULL, 4) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_change_password(admin, NULL, length, OTHER_PASSWORD,
                                              strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_change_password(admin, PASSWORD, length, NULL, 21, &retry_after_ms) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_account_change_password(admin, PASSWORD, length, OTHER_PASSWORD,
                                              strlen(OTHER_PASSWORD),
                                              NULL) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
}

static void success_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = admin_session();
    crux_access_account_info_t info;
    const size_t length = strlen(PASSWORD);

    /* Create, list and log in. */
    access_fake_events_clear();
    CHECK(crux_access_account_create(admin, USER, "operator", PASSWORD, length) == CRUX_ACCESS_OK);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_ACCOUNT_CREATED) == 1);
    CHECK(crux_access_account_create(admin, USER, "viewer", PASSWORD, length) ==
          CRUX_ACCESS_ERR_ALREADY_EXISTS);
    CHECK(account_info(admin, USER, &info));
    CHECK(strcmp(info.role, "operator") == 0);
    CHECK(info.enabled);
    CHECK(!info.must_change_password);
    CHECK(info.key_count == 0);

    size_t count = 0;
    CHECK(crux_access_account_list(admin, NULL, 0, &count) == CRUX_ACCESS_OK);
    CHECK(count == 2);
    crux_access_account_info_t one;
    count = 0;
    CHECK(crux_access_account_list(admin, &one, 1, &count) == CRUX_ACCESS_OK);
    CHECK(count == 2);
    CHECK(strcmp(one.user, ADMIN) == 0);

    /* Not found. */
    CHECK(crux_access_account_remove(admin, "ghost") == CRUX_ACCESS_ERR_NOT_FOUND);
    CHECK(crux_access_account_disable(admin, "ghost") == CRUX_ACCESS_ERR_NOT_FOUND);
    CHECK(crux_access_account_enable(admin, "ghost") == CRUX_ACCESS_ERR_NOT_FOUND);
    CHECK(crux_access_account_set_role(admin, "ghost", "viewer") == CRUX_ACCESS_ERR_NOT_FOUND);
    CHECK(crux_access_account_reset_password(admin, "ghost", PASSWORD, length) ==
          CRUX_ACCESS_ERR_NOT_FOUND);
    CHECK(crux_access_account_add_key(admin, "ghost", key_one, sizeof(key_one)) ==
          CRUX_ACCESS_ERR_NOT_FOUND);
    CHECK(crux_access_account_remove_key(admin, USER, key_one, sizeof(key_one)) ==
          CRUX_ACCESS_ERR_NOT_FOUND);

    /* Disabling and enabling. Repeating either changes nothing. */
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web);
    CHECK(crux_access_account_disable(admin, USER) == CRUX_ACCESS_OK);
    CHECK(crux_access_check(user, "config.write") == CRUX_ACCESS_ERR_SESSION_INVALID);
    unsigned saves = access_fake_store_saves();
    CHECK(crux_access_account_disable(admin, USER) == CRUX_ACCESS_OK);
    CHECK(access_fake_store_saves() == saves);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &user) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_account_enable(admin, USER) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_enable(admin, USER) == CRUX_ACCESS_OK);
    user = access_test_session(USER, PASSWORD, &access_test_web);

    /* A role change ends sessions and takes effect at the next login. */
    CHECK(crux_access_check(user, "config.write") == CRUX_ACCESS_OK);
    saves = access_fake_store_saves();
    CHECK(crux_access_account_set_role(admin, USER, "operator") == CRUX_ACCESS_OK);
    CHECK(access_fake_store_saves() == saves);
    CHECK(crux_access_check(user, "config.write") == CRUX_ACCESS_OK);
    CHECK(crux_access_account_set_role(admin, USER, "viewer") == CRUX_ACCESS_OK);
    CHECK(crux_access_check(user, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    user = access_test_session(USER, PASSWORD, &access_test_web);
    CHECK(crux_access_check(user, "status.read") == CRUX_ACCESS_OK);
    CHECK(crux_access_check(user, "config.write") == CRUX_ACCESS_ERR_DENIED);

    /* Removal ends sessions, and the name can be used again. */
    CHECK(crux_access_account_remove(admin, USER) == CRUX_ACCESS_OK);
    CHECK(crux_access_check(user, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &user) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_account_create(admin, USER, "viewer", OTHER_PASSWORD,
                                     strlen(OTHER_PASSWORD)) == CRUX_ACCESS_OK);
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &user) == CRUX_ACCESS_ERR_DENIED);
    user = access_test_session(USER, OTHER_PASSWORD, &access_test_web);

    /* The account table is bounded, and a full table is refused before any derivation. */
    CHECK(crux_access_account_remove(admin, USER) == CRUX_ACCESS_OK);
    char name[16];
    for (size_t index = 1; index < CRUX_ACCESS_ACCOUNT_MAX; ++index) {
        snprintf(name, sizeof(name), "user-%zu", index);
        CHECK(crux_access_account_create(admin, name, "viewer", PASSWORD, length) ==
              CRUX_ACCESS_OK);
    }
    access_fake_derive_counters_clear();
    CHECK(crux_access_account_create(admin, "full", "viewer", PASSWORD, length) ==
          CRUX_ACCESS_ERR_NO_RESOURCES);
    CHECK(access_fake_derive_count() == 0);
}

static void key_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = admin_session();
    access_test_create_user(admin);
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web);
    crux_access_account_info_t info;

    /* An account manages its own keys with access.self, and adding keeps sessions. */
    access_fake_events_clear();
    CHECK(crux_access_account_add_key(user, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_ACCOUNT_KEY_ADDED) == 1);
    CHECK(crux_access_check(user, "config.write") == CRUX_ACCESS_OK);
    CHECK(crux_access_account_add_key(user, USER, key_one, sizeof(key_one)) ==
          CRUX_ACCESS_ERR_ALREADY_EXISTS);
    CHECK(crux_access_account_add_key(user, ADMIN, key_one, sizeof(key_one)) ==
          CRUX_ACCESS_ERR_DENIED);
    /* An administrator manages anyone's keys. */
    CHECK(crux_access_account_add_key(admin, USER, key_two, sizeof(key_two)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_add_key(admin, USER, key_three, sizeof(key_three)) ==
          CRUX_ACCESS_ERR_NO_RESOURCES);
    CHECK(account_info(admin, USER, &info));
    CHECK(info.key_count == 2);

    /* A prefix of a listed key is a different key. */
    CHECK(crux_access_account_remove_key(user, USER, key_one, sizeof(key_one) - 1u) ==
          CRUX_ACCESS_ERR_NOT_FOUND);
    /* Removing a key ends every session of the account, including the one that removed it. */
    crux_access_session_t second = access_test_session(USER, PASSWORD, &access_test_web);
    CHECK(crux_access_account_remove_key(user, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    CHECK(crux_access_check(user, "config.write") == CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_access_check(second, "config.write") == CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_access_check(admin, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    CHECK(account_info(admin, USER, &info));
    CHECK(info.key_count == 1);
}

static void own_password_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = admin_session();
    access_test_create_user(admin);
    crux_access_session_t maker = access_test_session(USER, PASSWORD, &access_test_web);
    crux_access_session_t other = access_test_session(USER, PASSWORD, &access_test_web_other);
    const size_t length = strlen(PASSWORD);
    const size_t other_length = strlen(OTHER_PASSWORD);
    uint32_t retry_after_ms = 7;

    /* The password rule is checked first, so that result never confirms the current password. */
    access_fake_derive_counters_clear();
    CHECK(crux_access_account_change_password(maker, "wrong", 5, "short", 5, &retry_after_ms) ==
          CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    CHECK(access_fake_derive_count() == 0);

    CHECK(crux_access_account_change_password(maker, "wrong-current", 13, OTHER_PASSWORD,
                                              other_length,
                                              &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(retry_after_ms == 0);
    CHECK(crux_access_check(other, "config.write") == CRUX_ACCESS_OK);

    access_fake_events_clear();
    CHECK(crux_access_account_change_password(maker, PASSWORD, length, OTHER_PASSWORD, other_length,
                                              &retry_after_ms) == CRUX_ACCESS_OK);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_CHANGED) == 1);
    CHECK(crux_access_check(maker, "config.write") == CRUX_ACCESS_OK);
    CHECK(crux_access_check(other, "config.write") == CRUX_ACCESS_ERR_SESSION_INVALID);
    crux_access_session_t session = {0};
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &session) == CRUX_ACCESS_ERR_DENIED);

    /* Wrong current passwords follow the login delay, for the session's own source. */
    const crux_access_origin_t fresh_source = access_test_web_from(99);
    session = access_test_session(USER, OTHER_PASSWORD, &fresh_source);
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        CHECK(crux_access_account_change_password(session, "wrong-current", 13, PASSWORD, length,
                                                  &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
        access_fake_clock_advance(10u);
    }
    access_fake_derive_counters_clear();
    CHECK(crux_access_account_change_password(session, OTHER_PASSWORD, other_length, PASSWORD,
                                              length,
                                              &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms > 0 && retry_after_ms <= CRUX_ACCESS_DELAY_BASE_MS);
    CHECK(access_fake_derive_count() == 0);

    /* Over-long current password: denied without verification. */
    char longest[CRUX_ACCESS_PASSWORD_MAX + 1];
    memset(longest, 'x', sizeof(longest));
    access_fake_clock_advance(CRUX_ACCESS_DELAY_QUIET_MS);
    access_fake_derive_counters_clear();
    CHECK(crux_access_account_change_password(maker, longest, sizeof(longest), PASSWORD, length,
                                              &retry_after_ms) == CRUX_ACCESS_ERR_SESSION_INVALID);
    session = access_test_session(USER, OTHER_PASSWORD, &access_test_web);
    access_fake_derive_counters_clear();
    CHECK(crux_access_account_change_password(session, longest, sizeof(longest), PASSWORD, length,
                                              &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_derive_count() == 0);
}

static void forced_change_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = admin_session();
    access_test_create_user(admin);
    CHECK(crux_access_account_add_key(admin, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    crux_access_session_t existing = access_test_session(USER, PASSWORD, &access_test_web);
    const size_t length = strlen(PASSWORD);
    const size_t other_length = strlen(OTHER_PASSWORD);
    uint32_t retry_after_ms = 0;

    access_fake_events_clear();
    CHECK(crux_access_account_reset_password(admin, USER, OTHER_PASSWORD, other_length) ==
          CRUX_ACCESS_OK);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_RESET) == 1);
    CHECK(crux_access_check(existing, "config.write") == CRUX_ACCESS_ERR_SESSION_INVALID);

    crux_access_account_info_t info;
    CHECK(account_info(admin, USER, &info));
    CHECK(info.must_change_password);
    CHECK(info.key_count == 1);

    /* Key login is refused until the change, so a key added by an attacker does not bypass it. */
    crux_access_session_t by_key = {0};
    CHECK(crux_access_key_is_authorised(USER, key_one, sizeof(key_one), false, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_login_verified(USER, key_one, sizeof(key_one), &access_test_ssh, &by_key) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(by_key.value == 0);

    /* The old password no longer works, and the new one gives a restricted session. */
    crux_access_session_t session = {0};
    CHECK(access_test_login(USER, PASSWORD, &access_test_web, &session) == CRUX_ACCESS_ERR_DENIED);
    session = access_test_session(USER, OTHER_PASSWORD, &access_test_web);
    bool required = false;
    CHECK(crux_access_session_must_change_password(session, &required) == CRUX_ACCESS_OK);
    CHECK(required);
    CHECK(crux_access_check(session, "config.write") == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_check(session, CRUX_ACCESS_PERMISSION_SELF) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_account_add_key(session, USER, key_two, sizeof(key_two)) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_account_remove_key(session, USER, key_one, sizeof(key_one)) ==
          CRUX_ACCESS_ERR_DENIED);
    size_t count = 0;
    CHECK(crux_access_account_list(session, NULL, 0, &count) == CRUX_ACCESS_ERR_DENIED);

    /* It survives a reset. */
    access_test_power_cycle(reference());
    session = access_test_session(USER, OTHER_PASSWORD, &access_test_web);
    CHECK(crux_access_session_must_change_password(session, &required) == CRUX_ACCESS_OK);
    CHECK(required);

    /* Changing the password lifts the restriction for the session that made the change. */
    CHECK(crux_access_account_change_password(session, OTHER_PASSWORD, other_length, PASSWORD,
                                              length, &retry_after_ms) == CRUX_ACCESS_OK);
    CHECK(crux_access_session_must_change_password(session, &required) == CRUX_ACCESS_OK);
    CHECK(!required);
    CHECK(crux_access_check(session, "config.write") == CRUX_ACCESS_OK);
    CHECK(crux_access_key_is_authorised(USER, key_one, sizeof(key_one), false, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_OK);
    CHECK(crux_access_login_verified(USER, key_one, sizeof(key_one), &access_test_ssh, &by_key) ==
          CRUX_ACCESS_OK);

    /* An administrator can reset their own password, which ends their own session. */
    admin = admin_session();
    CHECK(crux_access_account_reset_password(admin, ADMIN, OTHER_PASSWORD, other_length) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_check(admin, CRUX_ACCESS_PERMISSION_MANAGE) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
}

static void last_administrator_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = admin_session();
    access_test_create_user(admin);
    unsigned saves = access_fake_store_saves();

    CHECK(crux_access_account_disable(admin, ADMIN) == CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    CHECK(crux_access_account_remove(admin, ADMIN) == CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    CHECK(crux_access_account_set_role(admin, ADMIN, "viewer") ==
          CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    CHECK(access_fake_store_saves() == saves);
    CHECK(crux_access_check(admin, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);

    /* A non-administrator is not protected. */
    CHECK(crux_access_account_disable(admin, USER) == CRUX_ACCESS_OK);
    /* A disabled administrator does not count. */
    CHECK(crux_access_account_set_role(admin, USER, "administrator") == CRUX_ACCESS_OK);
    CHECK(crux_access_account_disable(admin, ADMIN) == CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    CHECK(crux_access_account_remove(admin, ADMIN) == CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    /* An enabled second administrator allows the change. */
    CHECK(crux_access_account_enable(admin, USER) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_set_role(admin, ADMIN, "viewer") == CRUX_ACCESS_OK);
    CHECK(crux_access_check(admin, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web);
    CHECK(crux_access_check(user, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_set_role(user, ADMIN, "administrator") == CRUX_ACCESS_OK);
    CHECK(crux_access_account_remove(user, USER) == CRUX_ACCESS_OK);
    CHECK(crux_access_check(user, CRUX_ACCESS_PERMISSION_MANAGE) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);

    /* The last administrator cannot be moved to another role, even one holding access.manage. */
    static const char *const manage[] = {CRUX_ACCESS_PERMISSION_MANAGE};
    static const crux_access_role_t two_roles[] = {{"chief", manage, 1}, {"deputy", manage, 1}};
    static const crux_access_policy_t two_administrator_roles = {two_roles, 2, 900, 28800, NULL};
    access_test_boot(&two_administrator_roles);
    CHECK(crux_access_provision_first_account(ADMIN, PASSWORD, strlen(PASSWORD), "chief",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    admin = admin_session();
    saves = access_fake_store_saves();
    CHECK(crux_access_account_set_role(admin, ADMIN, "deputy") ==
          CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
    CHECK(access_fake_store_saves() == saves);
    /* With a second administrator it can be moved, to either kind of role. */
    CHECK(crux_access_account_create(admin, USER, "deputy", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_set_role(admin, ADMIN, "deputy") == CRUX_ACCESS_OK);
}

typedef struct {
    size_t count;
    crux_access_account_info_t accounts[CRUX_ACCESS_ACCOUNT_MAX];
} snapshot_t;

static snapshot_t snapshot(crux_access_session_t admin)
{
    snapshot_t result;
    memset(&result, 0, sizeof(result));
    CHECK(crux_access_account_list(admin, result.accounts, CRUX_ACCESS_ACCOUNT_MAX,
                                   &result.count) == CRUX_ACCESS_OK);
    return result;
}

static bool same_snapshot(const snapshot_t *left, const snapshot_t *right)
{
    if (left->count != right->count) {
        return false;
    }
    for (size_t index = 0; index < left->count; ++index) {
        const crux_access_account_info_t *one = &left->accounts[index];
        const crux_access_account_info_t *other = &right->accounts[index];
        if (strcmp(one->user, other->user) != 0 || strcmp(one->role, other->role) != 0 ||
            one->enabled != other->enabled ||
            one->must_change_password != other->must_change_password ||
            one->key_count != other->key_count) {
            return false;
        }
    }
    return true;
}

static void expect_same(const snapshot_t *left, const snapshot_t *right)
{
    CHECK(same_snapshot(left, right));
}

/* Applies a change. Each must leave state and medium unchanged if the save fails. */
typedef crux_access_result_t (*change_t)(crux_access_session_t admin);

static crux_access_result_t change_create(crux_access_session_t admin)
{
    return crux_access_account_create(admin, "new", "viewer", PASSWORD, strlen(PASSWORD));
}

static crux_access_result_t change_remove(crux_access_session_t admin)
{
    return crux_access_account_remove(admin, USER);
}

static crux_access_result_t change_disable(crux_access_session_t admin)
{
    return crux_access_account_disable(admin, USER);
}

static crux_access_result_t change_role(crux_access_session_t admin)
{
    return crux_access_account_set_role(admin, USER, "viewer");
}

static crux_access_result_t change_reset(crux_access_session_t admin)
{
    return crux_access_account_reset_password(admin, USER, OTHER_PASSWORD, strlen(OTHER_PASSWORD));
}

static crux_access_result_t change_add_key(crux_access_session_t admin)
{
    return crux_access_account_add_key(admin, USER, key_two, sizeof(key_two));
}

static crux_access_result_t change_remove_key(crux_access_session_t admin)
{
    return crux_access_account_remove_key(admin, USER, key_one, sizeof(key_one));
}

static crux_access_result_t change_enable_disabled(crux_access_session_t admin)
{
    return crux_access_account_enable(admin, "disabled");
}

static const change_t changes[] = {change_create,     change_remove,         change_disable,
                                   change_role,       change_reset,          change_add_key,
                                   change_remove_key, change_enable_disabled};
#define CHANGE_COUNT (sizeof(changes) / sizeof(changes[0]))

typedef struct {
    crux_access_session_t admin;
    crux_access_session_t user;
} store_case_t;

static store_case_t prepare_store_case(void)
{
    store_case_t sessions;
    access_test_boot_with_admin();
    sessions.admin = admin_session();
    access_test_create_user(sessions.admin);
    CHECK(crux_access_account_add_key(sessions.admin, USER, key_one, sizeof(key_one)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(sessions.admin, "disabled", "viewer", PASSWORD,
                                     strlen(PASSWORD)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_disable(sessions.admin, "disabled") == CRUX_ACCESS_OK);
    sessions.user = access_test_session(USER, PASSWORD, &access_test_web);
    return sessions;
}

static void save_failure_cases(void)
{
    for (size_t index = 0; index < CHANGE_COUNT; ++index) {
        const store_case_t sessions = prepare_store_case();
        crux_access_session_t admin = sessions.admin;
        const crux_access_session_t user = sessions.user;
        snapshot_t before = snapshot(admin);
        uint8_t medium[CRUX_ACCESS_RECORD_SIZE_MAX];
        size_t medium_length = 0;
        const uint8_t *stored = access_fake_store_record(&medium_length);
        memcpy(medium, stored, medium_length);

        access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
        access_fake_events_clear();
        CHECK(changes[index](admin) == CRUX_ACCESS_ERR_STORE);
        CHECK(access_fake_event_count() >= 1);
        CHECK(access_fake_event(access_fake_event_count() - 1u)->result == CRUX_ACCESS_ERR_STORE);
        snapshot_t after = snapshot(admin);
        expect_same(&before, &after);
        size_t length_after = 0;
        stored = access_fake_store_record(&length_after);
        CHECK(length_after == medium_length && memcmp(stored, medium, medium_length) == 0);
        /* Sessions continue, and the old credential still works. */
        CHECK(crux_access_check(user, "config.write") == CRUX_ACCESS_OK);
        (void)access_test_session(USER, PASSWORD, &access_test_web);

        /* Retrying once the store recovers succeeds. */
        access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);
        CHECK(changes[index](admin) == CRUX_ACCESS_OK);
    }

    /* A change of one's own password that cannot be saved keeps the old one, and the restriction.
     */
    const crux_access_session_t user = prepare_store_case().user;
    uint32_t retry_after_ms = 0;
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
    CHECK(crux_access_account_change_password(user, PASSWORD, strlen(PASSWORD), OTHER_PASSWORD,
                                              strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_ERR_STORE);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);
    (void)access_test_session(USER, PASSWORD, &access_test_web);
}

/* A power loss during a save leaves the old or the new table readable, and each works. */
static void power_loss_cases(void)
{
    for (size_t index = 0; index < CHANGE_COUNT; ++index) {
        const store_case_t sessions = prepare_store_case();
        crux_access_session_t admin = sessions.admin;
        snapshot_t before = snapshot(admin);

        access_fake_store_save_mode(ACCESS_FAKE_SAVE_POWER_LOSS_BEFORE);
        CHECK(changes[index](admin) == CRUX_ACCESS_ERR_STORE);
        access_test_power_cycle(reference());
        CHECK(crux_access_check(admin, CRUX_ACCESS_PERMISSION_MANAGE) ==
              CRUX_ACCESS_ERR_SESSION_INVALID);
        access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);
        admin = admin_session();
        snapshot_t old_table = snapshot(admin);
        expect_same(&before, &old_table);

        access_fake_store_save_mode(ACCESS_FAKE_SAVE_POWER_LOSS_AFTER);
        CHECK(changes[index](admin) == CRUX_ACCESS_ERR_STORE);
        access_test_power_cycle(reference());
        access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);
        admin = admin_session();
        snapshot_t new_table = snapshot(admin);
        CHECK(!same_snapshot(&before, &new_table));
    }
}

static void persistence_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = admin_session();
    access_test_create_user(admin);
    CHECK(crux_access_account_add_key(admin, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_add_key(admin, USER, key_two, sizeof(key_two)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(admin, "disabled", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_disable(admin, "disabled") == CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(admin, "forced", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_reset_password(admin, "forced", OTHER_PASSWORD,
                                             strlen(OTHER_PASSWORD)) == CRUX_ACCESS_OK);
    snapshot_t before = snapshot(admin);

    access_test_power_cycle(reference());
    admin = admin_session();
    snapshot_t after = snapshot(admin);
    expect_same(&before, &after);
    crux_access_session_t session = access_test_session(USER, PASSWORD, &access_test_web);
    CHECK(crux_access_check(session, "config.write") == CRUX_ACCESS_OK);
    crux_access_session_t none = {0};
    CHECK(access_test_login("disabled", PASSWORD, &access_test_web, &none) ==
          CRUX_ACCESS_ERR_DENIED);
    uint32_t retry_after_ms = 0;
    CHECK(crux_access_key_is_authorised(USER, key_two, sizeof(key_two), false, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_OK);
}

/* --- Changes meeting logins, checks and each other --- */

typedef struct {
    crux_access_session_t session;
    crux_access_result_t result;
} login_during_t;

static void hook_login(void *context)
{
    login_during_t *during = context;
    during->result = access_test_login(USER, PASSWORD, &access_test_web_other, &during->session);
}

static void login_during_save_cases(void)
{
    /* A login completing during a save sees the old record, and applying the change ends it. */
    typedef crux_access_result_t (*user_change_t)(crux_access_session_t);
    const user_change_t apply[] = {change_disable, change_role, change_reset, change_remove};
    for (size_t index = 0; index < sizeof(apply) / sizeof(apply[0]); ++index) {
        access_test_boot_with_admin();
        crux_access_session_t admin = admin_session();
        access_test_create_user(admin);
        login_during_t during = {{0}, CRUX_ACCESS_ERR_NOT_READY};
        access_fake_store_save_hook(hook_login, &during);
        CHECK(apply[index](admin) == CRUX_ACCESS_OK);
        CHECK(during.result == CRUX_ACCESS_OK);
        CHECK(during.session.value != 0);
        CHECK(crux_access_check(during.session, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
    }
}

typedef struct {
    crux_access_session_t session;
    crux_access_result_t result;
} check_thread_t;

static void *check_thread(void *context)
{
    check_thread_t *check = context;
    check->result = crux_access_check(check->session, CRUX_ACCESS_PERMISSION_MANAGE);
    return NULL;
}

static void hook_check_from_other_thread(void *context)
{
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, check_thread, context) == 0);
    /* Joined here, inside the save: if the state lock were held, this would never return and
     * the CTest timeout would report it. */
    CHECK(pthread_join(thread, NULL) == 0);
}

static void check_does_not_wait_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = admin_session();
    access_test_create_user(admin);

    /* During a save. */
    check_thread_t check = {admin, CRUX_ACCESS_ERR_NOT_READY};
    access_fake_store_save_hook(hook_check_from_other_thread, &check);
    CHECK(crux_access_account_disable(admin, USER) == CRUX_ACCESS_OK);
    CHECK(check.result == CRUX_ACCESS_OK);

    /* During the derivation of a new verifier. */
    check.result = CRUX_ACCESS_ERR_NOT_READY;
    access_fake_derive_hook(hook_check_from_other_thread, &check);
    CHECK(crux_access_account_reset_password(admin, USER, OTHER_PASSWORD, strlen(OTHER_PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(check.result == CRUX_ACCESS_OK);

    /* During the derivation of a login. */
    check.result = CRUX_ACCESS_ERR_NOT_READY;
    access_fake_derive_hook(hook_check_from_other_thread, &check);
    (void)access_test_session(ADMIN, PASSWORD, &access_test_web);
    CHECK(check.result == CRUX_ACCESS_OK);
}

/* Derivation runs before the change lock, so these changes arrive while it runs. */
static void hook_remove_user(void *context)
{
    crux_access_session_t *admin = context;
    CHECK(crux_access_account_remove(*admin, USER) == CRUX_ACCESS_OK);
}

static void hook_create_user(void *context)
{
    crux_access_session_t *admin = context;
    access_test_create_user(*admin);
}

static void hook_reset_user(void *context)
{
    crux_access_session_t *admin = context;
    CHECK(crux_access_account_reset_password(*admin, USER, OTHER_PASSWORD,
                                             strlen(OTHER_PASSWORD)) == CRUX_ACCESS_OK);
}

static void hook_add_key_to_user(void *context)
{
    crux_access_session_t *admin = context;
    CHECK(crux_access_account_add_key(*admin, USER, key_one, sizeof(key_one)) == CRUX_ACCESS_OK);
}

/* A session that logs out during its own save must not lift another account's restriction when the
 * slot is reused before the change is applied. */
typedef struct {
    crux_access_session_t maker;
    crux_access_session_t reused;
} slot_reuse_t;

static void hook_logout_and_reuse_slot(void *context)
{
    slot_reuse_t *reuse = context;
    crux_access_session_t ended = reuse->maker;
    crux_access_logout(&ended);
    reuse->reused = access_test_session("forced", OTHER_PASSWORD, &access_test_web);
}

static void kept_session_reuse_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_serial);
    access_test_create_user(admin);
    CHECK(crux_access_account_create(admin, "forced", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_reset_password(admin, "forced", OTHER_PASSWORD,
                                             strlen(OTHER_PASSWORD)) == CRUX_ACCESS_OK);
    slot_reuse_t reuse = {access_test_session(USER, PASSWORD, &access_test_web), {0}};
    uint32_t retry_after_ms = 0;
    access_fake_store_save_hook(hook_logout_and_reuse_slot, &reuse);
    CHECK(crux_access_account_change_password(reuse.maker, PASSWORD, strlen(PASSWORD),
                                              OTHER_PASSWORD, strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_OK);
    CHECK(reuse.reused.value >> 24 == reuse.maker.value >> 24);
    bool required = false;
    CHECK(crux_access_session_must_change_password(reuse.reused, &required) == CRUX_ACCESS_OK);
    CHECK(required);
    CHECK(crux_access_check(reuse.reused, "status.read") == CRUX_ACCESS_ERR_DENIED);
}

/* Role names are policy, and an invalid session must not learn which ones exist. */
static void role_disclosure_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t none = {0};
    const size_t length = strlen(PASSWORD);
    CHECK(crux_access_account_create(none, "bob", "no-role", PASSWORD, length) ==
          crux_access_account_create(none, "bob", "viewer", PASSWORD, length));
    CHECK(crux_access_account_create(none, "bob", "no-role", PASSWORD, length) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_access_account_set_role(none, ADMIN, "no-role") == CRUX_ACCESS_ERR_SESSION_INVALID);
}

static void change_during_derivation_cases(void)
{
    uint32_t retry_after_ms = 0;
    access_test_boot_with_admin();
    crux_access_session_t admin = admin_session();
    const size_t length = strlen(OTHER_PASSWORD);

    /* Create meets a create of the same name. */
    access_fake_derive_hook(hook_create_user, &admin);
    CHECK(crux_access_account_create(admin, USER, "viewer", OTHER_PASSWORD, length) ==
          CRUX_ACCESS_ERR_ALREADY_EXISTS);

    /* Reset meets a removal. */
    access_fake_derive_hook(hook_remove_user, &admin);
    CHECK(crux_access_account_reset_password(admin, USER, OTHER_PASSWORD, length) ==
          CRUX_ACCESS_ERR_NOT_FOUND);

    /* The caller's own session ends during the derivation of a new verifier. */
    CHECK(crux_access_account_create(admin, USER, "administrator", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    crux_access_session_t user = access_test_session(USER, PASSWORD, &access_test_web);
    access_fake_derive_hook(hook_remove_user, &admin);
    CHECK(crux_access_account_create(user, "x", "viewer", OTHER_PASSWORD, length) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_access_account_list(admin, NULL, 0, &(size_t){0}) == CRUX_ACCESS_OK);
    CHECK(access_test_login("x", OTHER_PASSWORD, &access_test_web, &user) ==
          CRUX_ACCESS_ERR_DENIED);

    /* Changing one's own password meets a key change. Sessions continue, but the account's change
     * number moved, so the password verified earlier no longer applies. */
    access_test_create_user(admin);
    user = access_test_session(USER, PASSWORD, &access_test_web);
    access_fake_derive_hook(hook_add_key_to_user, &admin);
    CHECK(crux_access_account_change_password(user, PASSWORD, strlen(PASSWORD), "third password!",
                                              15, &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_check(user, "status.read") == CRUX_ACCESS_OK);

    /* It meets an administrator's reset, which ends the session. Nothing the user set survives. */
    access_fake_derive_hook(hook_reset_user, &admin);
    CHECK(crux_access_account_change_password(user, PASSWORD, strlen(PASSWORD), "third password!",
                                              15,
                                              &retry_after_ms) == CRUX_ACCESS_ERR_SESSION_INVALID);
    crux_access_session_t after = {0};
    CHECK(access_test_login(USER, "third password!", &access_test_web, &after) ==
          CRUX_ACCESS_ERR_DENIED);
    after = access_test_session(USER, OTHER_PASSWORD, &access_test_web);
    bool required = false;
    CHECK(crux_access_session_must_change_password(after, &required) == CRUX_ACCESS_OK);
    CHECK(required);
}

typedef struct {
    crux_access_session_t session;
    const char *target;
    int action;
    pthread_barrier_t *barrier;
    crux_access_result_t result;
} rival_t;

static void *rival_thread(void *context)
{
    rival_t *rival = context;
    if (rival->barrier != NULL) {
        pthread_barrier_wait(rival->barrier);
    }
    switch (rival->action) {
    case 0:
        rival->result = crux_access_account_disable(rival->session, rival->target);
        break;
    case 1:
        rival->result = crux_access_account_remove(rival->session, rival->target);
        break;
    default:
        rival->result = crux_access_account_set_role(rival->session, rival->target, "viewer");
        break;
    }
    return NULL;
}

static void concurrent_rivals(int action)
{
    access_test_boot_with_admin();
    crux_access_session_t first = admin_session();
    CHECK(crux_access_account_create(first, "second", "administrator", PASSWORD,
                                     strlen(PASSWORD)) == CRUX_ACCESS_OK);
    crux_access_session_t second = access_test_session("second", PASSWORD, &access_test_web);
    pthread_barrier_t barrier;
    CHECK(pthread_barrier_init(&barrier, NULL, 2) == 0);
    rival_t left = {first, "second", action, &barrier, CRUX_ACCESS_ERR_NOT_READY};
    rival_t right = {second, ADMIN, action, &barrier, CRUX_ACCESS_ERR_NOT_READY};
    pthread_t threads[2];
    CHECK(pthread_create(&threads[0], NULL, rival_thread, &left) == 0);
    CHECK(pthread_create(&threads[1], NULL, rival_thread, &right) == 0);
    CHECK(pthread_join(threads[0], NULL) == 0);
    CHECK(pthread_join(threads[1], NULL) == 0);
    pthread_barrier_destroy(&barrier);

    /* The changes run in order: one succeeds, and the other finds its session ended. */
    bool left_won = left.result == CRUX_ACCESS_OK;
    bool right_won = right.result == CRUX_ACCESS_OK;
    CHECK(left_won != right_won);
    CHECK((left_won ? right.result : left.result) == CRUX_ACCESS_ERR_SESSION_INVALID);
    crux_access_session_t survivor = left_won ? first : second;
    CHECK(crux_access_check(survivor, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
}

static rival_t ordered_rival;
static pthread_t ordered_thread;
static atomic_bool ordered_finished;

static void *ordered_rival_thread(void *context)
{
    (void)rival_thread(context);
    atomic_store(&ordered_finished, true);
    return NULL;
}

/* The rival gets ample time to finish during the save. Only the change lock can stop it, and if it
 * finishes here, both administrators could disable each other and leave none. */
static void hook_start_rival(void *context)
{
    (void)context;
    atomic_store(&ordered_finished, false);
    CHECK(pthread_create(&ordered_thread, NULL, ordered_rival_thread, &ordered_rival) == 0);
    const struct timespec wait = {.tv_sec = 0, .tv_nsec = 100000000};
    (void)nanosleep(&wait, NULL);
    CHECK(!atomic_load(&ordered_finished));
}

static void last_administrator_concurrency_cases(void)
{
    for (int round = 0; round < 60; ++round) {
        concurrent_rivals(round % 3);
    }

    /* The rival starts while the first change holds the change lock in its save, and must wait. */
    access_test_boot_with_admin();
    crux_access_session_t first = admin_session();
    CHECK(crux_access_account_create(first, "second", "administrator", PASSWORD,
                                     strlen(PASSWORD)) == CRUX_ACCESS_OK);
    crux_access_session_t second = access_test_session("second", PASSWORD, &access_test_web);
    rival_t rival = {second, ADMIN, 0, NULL, CRUX_ACCESS_ERR_NOT_READY};
    ordered_rival = rival;
    access_fake_store_save_hook(hook_start_rival, NULL);
    CHECK(crux_access_account_disable(first, "second") == CRUX_ACCESS_OK);
    CHECK(pthread_join(ordered_thread, NULL) == 0);
    CHECK(ordered_rival.result == CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_access_check(first, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
}

int main(void)
{
    refusal_cases();
    success_cases();
    key_cases();
    own_password_cases();
    forced_change_cases();
    last_administrator_cases();
    save_failure_cases();
    power_loss_cases();
    persistence_cases();
    login_during_save_cases();
    check_does_not_wait_cases();
    change_during_derivation_cases();
    last_administrator_concurrency_cases();
    kept_session_reuse_cases();
    role_disclosure_cases();
    return access_test_finish("Access accounts");
}
