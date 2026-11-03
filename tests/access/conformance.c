/* Contract conformance, run against two different product policies. */
#include "access_test.h"

static const char too_long_name[] = "name-longer-than-thirty-one-chars";

static crux_access_result_t provision(const char *user, const char *password, const char *role,
                                      const crux_access_origin_t *origin)
{
    return crux_access_provision_first_account(user, password, strlen(password), role, origin);
}

static void not_ready_cases(void)
{
    access_fake_reset();
    crux_access_test_reset();
    crux_access_session_t session = {0x01000001u};
    char name[CRUX_ACCESS_NAME_SIZE] = "x";
    bool required = false;
    uint32_t retry_after_ms = 0;
    size_t count = 0;
    const uint8_t key[] = {1, 2, 3};
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_session_user(session, name, sizeof(name)) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_session_role(session, name, sizeof(name)) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_session_must_change_password(session, &required) ==
          CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_login_password("admin", "password", 8, &access_test_web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_key_is_authorised("admin", key, sizeof(key), false, &access_test_ssh,
                                        &retry_after_ms) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_login_verified("admin", key, sizeof(key), &access_test_ssh, &session) ==
          CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_provision_first_account("admin", "correct horse battery", 21, "administrator",
                                              &access_test_serial) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_touch(session) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_list(session, NULL, 0, &count) == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_create(session, "bob", "viewer", "correct horse battery", 21) ==
          CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_remove(session, "bob") == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_disable(session, "bob") == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_enable(session, "bob") == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_set_role(session, "bob", "viewer") == CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_reset_password(session, "bob", "correct horse battery", 21) ==
          CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_change_password(session, "a", 1, "b", 1, &retry_after_ms) ==
          CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_add_key(session, "bob", key, sizeof(key)) ==
          CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_account_remove_key(session, "bob", key, sizeof(key)) ==
          CRUX_ACCESS_ERR_NOT_READY);
    crux_access_report_key_failure("admin", &access_test_ssh);
    crux_access_logout(&session);
    CHECK(session.value == 0);
    CHECK(access_fake_derive_count() == 0);
    CHECK(access_fake_event_count() == 0);
}

static void expect_policy_rejected(const crux_access_policy_t *policy)
{
    access_fake_reset();
    crux_access_test_reset();
    CHECK(crux_access_init(policy) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    crux_access_session_t session = {0};
    CHECK(access_test_login("admin", ACCESS_TEST_PASSWORD, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_NOT_READY);
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_ERR_NOT_READY);
}

static void invalid_policy_cases(void)
{
    static const char *const manage[] = {CRUX_ACCESS_PERMISSION_MANAGE};
    static const char *const no_manage[] = {"status.read"};
    static const char *const duplicate[] = {CRUX_ACCESS_PERMISSION_MANAGE,
                                            CRUX_ACCESS_PERMISSION_MANAGE};
    static const char *const empty_permission[] = {CRUX_ACCESS_PERMISSION_MANAGE, ""};
    static const char *const null_permission[] = {CRUX_ACCESS_PERMISSION_MANAGE, NULL};
    static const char *const long_permission[] = {
        CRUX_ACCESS_PERMISSION_MANAGE,
        "permission.name.that.is.far.longer.than.the.sixty.three.bytes.allowed"};
    crux_access_role_t role = {"admin", manage, 1};
    crux_access_policy_t policy = {&role, 1, 60, 600, NULL};

    expect_policy_rejected(NULL);
    policy.roles = NULL;
    expect_policy_rejected(&policy);
    policy.roles = &role;
    policy.role_count = 0;
    expect_policy_rejected(&policy);
    policy.role_count = 1;
    policy.session_idle_limit_s = 0;
    expect_policy_rejected(&policy);
    policy.session_idle_limit_s = 60;
    policy.session_absolute_limit_s = 0;
    expect_policy_rejected(&policy);
    policy.session_absolute_limit_s = 59;
    expect_policy_rejected(&policy);
    policy.session_absolute_limit_s = 600;

    role.name = NULL;
    expect_policy_rejected(&policy);
    role.name = "";
    expect_policy_rejected(&policy);
    role.name = too_long_name;
    expect_policy_rejected(&policy);
    role.name = "admin";

    role.permissions = no_manage;
    expect_policy_rejected(&policy);
    role.permissions = NULL;
    expect_policy_rejected(&policy);
    role.permission_count = 2;
    role.permissions = duplicate;
    expect_policy_rejected(&policy);
    role.permissions = empty_permission;
    expect_policy_rejected(&policy);
    role.permissions = null_permission;
    expect_policy_rejected(&policy);
    role.permissions = long_permission;
    expect_policy_rejected(&policy);
    role.permission_count = 1;
    role.permissions = manage;

    crux_access_role_t two[] = {role, role};
    policy.roles = two;
    policy.role_count = 2;
    expect_policy_rejected(&policy);

    crux_access_role_t valid = {"admin", manage, 1};
    crux_access_policy_t accepted = {&valid, 1, 60, 60, NULL};
    access_test_boot(&accepted);
    /* A role with no permissions is allowed, because it still holds access.self. */
    crux_access_role_t with_empty[] = {{"admin", manage, 1}, {"guest", NULL, 0}};
    crux_access_policy_t empty_role = {with_empty, 2, 60, 600, NULL};
    access_test_boot(&empty_role);
}

static void init_cases(const access_test_profile_t *profile)
{
    access_fake_reset();
    crux_access_test_reset();
    access_fake_random_fail(true);
    CHECK(crux_access_init(&profile->policy) == CRUX_ACCESS_ERR_NOT_READY);
    crux_access_session_t session = {0};
    CHECK(access_test_login("admin", profile->password, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_NOT_READY);
    access_fake_random_fail(false);
    CHECK(crux_access_init(&profile->policy) == CRUX_ACCESS_OK);
    CHECK(crux_access_init(&profile->policy) == CRUX_ACCESS_ERR_ALREADY_EXISTS);
}

static void provisioning_cases(const access_test_profile_t *profile)
{
    access_test_boot(&profile->policy);
    crux_access_session_t session = {0};

    /* Until an account exists, every login is denied the same way. */
    CHECK(access_test_login("admin", profile->password, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(access_test_login("admin", profile->password, &access_test_serial, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_store_saves() == 0);

    CHECK(provision("admin", profile->password, profile->administrator_role, NULL) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(provision("admin", profile->password, profile->administrator_role, &access_test_web) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(provision("admin", profile->password, profile->administrator_role, &access_test_ssh) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(provision("admin", profile->password, profile->limited_role, &access_test_serial) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(provision("admin", profile->password, "no-such-role", &access_test_serial) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(provision("Admin", profile->password, profile->administrator_role, &access_test_serial) ==
          CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(provision(too_long_name, profile->password, profile->administrator_role,
                    &access_test_serial) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(provision("admin", profile->rejected_password, profile->administrator_role,
                    &access_test_serial) == CRUX_ACCESS_ERR_PASSWORD_REJECTED);
    CHECK(access_fake_store_saves() == 0);

    access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
    CHECK(provision("admin", profile->password, profile->administrator_role, &access_test_serial) ==
          CRUX_ACCESS_ERR_STORE);
    CHECK(access_test_login("admin", profile->password, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);

    access_fake_events_clear();
    CHECK(provision("admin", profile->password, profile->administrator_role, &access_test_serial) ==
          CRUX_ACCESS_OK);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PROVISIONED) == 1);
    CHECK(provision("second", profile->password, profile->administrator_role,
                    &access_test_serial) == CRUX_ACCESS_ERR_DENIED);

    /* A later boot from the saved table stays closed. */
    access_test_power_cycle(&profile->policy);
    CHECK(provision("second", profile->password, profile->administrator_role,
                    &access_test_serial) == CRUX_ACCESS_ERR_DENIED);
    session = access_test_session("admin", profile->password, &access_test_web);
    CHECK(crux_access_check(session, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
}

static const access_test_profile_t *nested_profile;
static crux_access_result_t nested_result;

static void hook_nested_provision(void *context)
{
    (void)context;
    nested_result = provision("first", nested_profile->password, nested_profile->administrator_role,
                              &access_test_serial);
}

/* Two provisioning attempts that both saw it open: the second to finish is refused. */
static void concurrent_provisioning_cases(const access_test_profile_t *profile)
{
    access_test_boot(&profile->policy);
    nested_profile = profile;
    nested_result = CRUX_ACCESS_ERR_NOT_READY;
    access_fake_derive_hook(hook_nested_provision, NULL);
    CHECK(provision("second", profile->password, profile->administrator_role,
                    &access_test_serial) == CRUX_ACCESS_ERR_DENIED);
    CHECK(nested_result == CRUX_ACCESS_OK);
    crux_access_session_t session = {0};
    CHECK(access_test_login("second", profile->password, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    (void)access_test_session("first", profile->password, &access_test_web);
}

static void permission_cases(const access_test_profile_t *profile)
{
    access_test_boot(&profile->policy);
    CHECK(provision("admin", profile->password, profile->administrator_role, &access_test_serial) ==
          CRUX_ACCESS_OK);
    crux_access_session_t admin = access_test_session("admin", profile->password, &access_test_web);
    CHECK(crux_access_account_create(admin, "user", profile->limited_role, profile->password,
                                     strlen(profile->password)) == CRUX_ACCESS_OK);
    crux_access_session_t user = access_test_session("user", profile->password, &access_test_web);

    CHECK(crux_access_check(admin, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    CHECK(crux_access_check(user, profile->granted_to_limited) == CRUX_ACCESS_OK);
    CHECK(crux_access_check(user, CRUX_ACCESS_PERMISSION_SELF) == CRUX_ACCESS_OK);
    access_fake_events_clear();
    CHECK(crux_access_check(user, profile->not_granted_to_limited) == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 1);
    CHECK(strcmp(access_fake_event(0)->account, "user") == 0);
    CHECK(crux_access_check(user, profile->unknown_permission) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_check(admin, profile->unknown_permission) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_check(user, "") == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_check(user, NULL) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    /* Exact comparison: no prefix, suffix, case or wildcard match. */
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%s.more", profile->granted_to_limited);
    CHECK(crux_access_check(user, prefix) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_check(user, "*") == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_check(user, "access.") == CRUX_ACCESS_ERR_DENIED);

    char name[CRUX_ACCESS_NAME_SIZE];
    CHECK(crux_access_session_user(user, name, sizeof(name)) == CRUX_ACCESS_OK);
    CHECK(strcmp(name, "user") == 0);
    CHECK(crux_access_session_role(user, name, sizeof(name)) == CRUX_ACCESS_OK);
    CHECK(strcmp(name, profile->limited_role) == 0);
    bool required = true;
    CHECK(crux_access_session_must_change_password(user, &required) == CRUX_ACCESS_OK);
    CHECK(!required);
    char small[4] = "abc";
    CHECK(crux_access_session_user(user, small, sizeof(small)) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(strcmp(small, "abc") == 0);
    CHECK(crux_access_session_user(user, NULL, 0) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_session_role(user, NULL, 0) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_session_must_change_password(user, NULL) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
}

static void unauthenticated_and_stale_cases(const access_test_profile_t *profile)
{
    access_test_boot(&profile->policy);
    CHECK(provision("admin", profile->password, profile->administrator_role, &access_test_serial) ==
          CRUX_ACCESS_OK);
    crux_access_session_t none = {0};
    crux_access_session_t forged = {0xFF000001u};
    crux_access_session_t out_of_table = {((uint32_t)CRUX_ACCESS_SESSION_MAX) << 24 | 1u};
    char name[CRUX_ACCESS_NAME_SIZE] = "kept";
    bool required = false;
    const crux_access_session_t invalid[] = {none, forged, out_of_table};
    for (size_t index = 0; index < 3; ++index) {
        CHECK(crux_access_check(invalid[index], profile->granted_to_limited) ==
              CRUX_ACCESS_ERR_SESSION_INVALID);
        CHECK(crux_access_session_user(invalid[index], name, sizeof(name)) ==
              CRUX_ACCESS_ERR_SESSION_INVALID);
        CHECK(crux_access_session_role(invalid[index], name, sizeof(name)) ==
              CRUX_ACCESS_ERR_SESSION_INVALID);
        CHECK(crux_access_session_must_change_password(invalid[index], &required) ==
              CRUX_ACCESS_ERR_SESSION_INVALID);
        CHECK(crux_access_touch(invalid[index]) == CRUX_ACCESS_ERR_SESSION_INVALID);
        CHECK(crux_access_account_disable(invalid[index], "admin") ==
              CRUX_ACCESS_ERR_SESSION_INVALID);
    }
    CHECK(strcmp(name, "kept") == 0);

    /* Logging out ends the session, clears the reference, and a copy goes stale. */
    crux_access_session_t session =
        access_test_session("admin", profile->password, &access_test_web);
    crux_access_session_t copy = session;
    access_fake_events_clear();
    crux_access_logout(&session);
    CHECK(session.value == 0);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGOUT) == 1);
    CHECK(crux_access_check(copy, CRUX_ACCESS_PERMISSION_MANAGE) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    crux_access_logout(&copy);
    CHECK(copy.value == 0);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGOUT) == 1);
    crux_access_logout(NULL);

    /* A reused slot takes a new generation, so the old handle never reaches the new session. */
    crux_access_session_t first = access_test_session("admin", profile->password, &access_test_web);
    crux_access_session_t stale = first;
    crux_access_logout(&first);
    crux_access_session_t second =
        access_test_session("admin", profile->password, &access_test_web);
    CHECK(second.value >> 24 == stale.value >> 24);
    CHECK(second.value != stale.value);
    CHECK(crux_access_check(stale, CRUX_ACCESS_PERMISSION_MANAGE) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_access_check(second, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);

    /* A reset ends every session, and a handle from before it does not match by accident. */
    access_test_power_cycle(&profile->policy);
    CHECK(crux_access_check(second, CRUX_ACCESS_PERMISSION_MANAGE) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
}

static void expiry_cases(const access_test_profile_t *profile)
{
    const uint64_t idle_ms = (uint64_t)profile->policy.session_idle_limit_s * 1000u;
    const uint64_t absolute_ms = (uint64_t)profile->policy.session_absolute_limit_s * 1000u;
    access_test_boot(&profile->policy);
    CHECK(provision("admin", profile->password, profile->administrator_role, &access_test_serial) ==
          CRUX_ACCESS_OK);

    /* Idle: a check does not refresh, and touch does. */
    crux_access_session_t session =
        access_test_session("admin", profile->password, &access_test_web);
    access_fake_clock_advance(idle_ms - 1);
    CHECK(crux_access_check(session, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    access_fake_events_clear();
    access_fake_clock_advance(1);
    CHECK(crux_access_check(session, CRUX_ACCESS_PERMISSION_MANAGE) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_SESSION_EXPIRED) == 1);
    CHECK(strcmp(access_fake_event(0)->account, "admin") == 0);
    CHECK(crux_access_touch(session) == CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_SESSION_EXPIRED) == 1);

    /* Touching keeps it alive up to the absolute limit, and no further. */
    session = access_test_session("admin", profile->password, &access_test_web);
    uint64_t elapsed_ms = 0;
    const uint64_t step_ms = idle_ms - 1;
    while (elapsed_ms + step_ms < absolute_ms) {
        access_fake_clock_advance(step_ms);
        elapsed_ms += step_ms;
        CHECK(crux_access_touch(session) == CRUX_ACCESS_OK);
    }
    access_fake_clock_advance(absolute_ms - elapsed_ms);
    CHECK(crux_access_touch(session) == CRUX_ACCESS_ERR_SESSION_INVALID);
    CHECK(crux_access_check(session, CRUX_ACCESS_PERMISSION_MANAGE) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
}

static void generation_cases(const access_test_profile_t *profile)
{
    access_test_boot(&profile->policy);
    CHECK(provision("admin", profile->password, profile->administrator_role, &access_test_serial) ==
          CRUX_ACCESS_OK);

    /* A wrap on the reserved serial slot must skip the value that makes the handle zero. */
    crux_access_test_set_generation(0, ACCESS_GENERATION_MASK);
    crux_access_session_t serial =
        access_test_session("admin", profile->password, &access_test_serial);
    CHECK(serial.value == ACCESS_GENERATION_MASK);
    crux_access_logout(&serial);
    serial = access_test_session("admin", profile->password, &access_test_serial);
    CHECK(serial.value == 1);
    CHECK(crux_access_check(serial, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    crux_access_session_t zero = {0};
    CHECK(crux_access_check(zero, CRUX_ACCESS_PERMISSION_MANAGE) ==
          CRUX_ACCESS_ERR_SESSION_INVALID);
    crux_access_logout(&serial);

    /* Other slots may hold generation zero, because the slot keeps the handle nonzero. */
    crux_access_test_set_generation(1, ACCESS_GENERATION_MASK);
    crux_access_session_t web = access_test_session("admin", profile->password, &access_test_web);
    CHECK(web.value == (1u << 24 | ACCESS_GENERATION_MASK));
    crux_access_logout(&web);
    web = access_test_session("admin", profile->password, &access_test_web);
    CHECK(web.value == 1u << 24);
    CHECK(crux_access_check(web, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
}

static void argument_cases(const access_test_profile_t *profile)
{
    access_test_boot(&profile->policy);
    CHECK(provision("admin", profile->password, profile->administrator_role, &access_test_serial) ==
          CRUX_ACCESS_OK);
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    crux_access_origin_t bad_channel = access_test_web;
    /* Deliberately out of range, to test that the contract rejects it. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    bad_channel.channel = (crux_access_channel_t)7;
    crux_access_origin_t bad_length = access_test_web;
    bad_length.source_length = 17;
    access_fake_derive_counters_clear();

    const size_t length = strlen(profile->password);
    CHECK(crux_access_login_password(NULL, profile->password, length, &access_test_web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_password("admin", NULL, length, &access_test_web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_password("admin", profile->password, length, NULL, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_password("admin", profile->password, length, &access_test_web, NULL,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_password("admin", profile->password, length, &access_test_web, &session,
                                     NULL) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_password("admin", profile->password, length, &bad_channel, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    CHECK(crux_access_login_password("admin", profile->password, length, &bad_length, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    /* A name no account can hold reveals nothing, and is refused without any work. */
    const char *const malformed[] = {"", "Admin", "ad min", "ad/min", too_long_name};
    for (size_t index = 0; index < sizeof(malformed) / sizeof(malformed[0]); ++index) {
        CHECK(crux_access_login_password(malformed[index], profile->password, length,
                                         &access_test_web, &session,
                                         &retry_after_ms) == CRUX_ACCESS_ERR_INVALID_ARGUMENT);
    }
    /* Passwords over the limit are denied without verification. */
    char long_password[CRUX_ACCESS_PASSWORD_MAX + 2];
    memset(long_password, 'x', sizeof(long_password));
    CHECK(crux_access_login_password("admin", long_password, sizeof(long_password) - 1,
                                     &access_test_web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_derive_count() == 0);
}

static void suite(const access_test_profile_t *profile)
{
    init_cases(profile);
    provisioning_cases(profile);
    concurrent_provisioning_cases(profile);
    permission_cases(profile);
    unauthenticated_and_stale_cases(profile);
    expiry_cases(profile);
    generation_cases(profile);
    argument_cases(profile);
}

int main(void)
{
    not_ready_cases();
    invalid_policy_cases();
    suite(&access_test_profile_reference);
    suite(&access_test_profile_product);
    return access_test_finish("Access conformance");
}
