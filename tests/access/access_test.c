#include "access_test.h"

unsigned access_test_checks, access_test_failures;

int access_test_finish(const char *suite)
{
    printf("%s: %u checks, %u failures\n", suite, access_test_checks, access_test_failures);
    return access_test_failures == 0 ? 0 : 1;
}

static const char *const reference_viewer[] = {"status.read"};
static const char *const reference_operator[] = {"status.read", "config.write"};
static const char *const reference_administrator[] = {"status.read", "config.write",
                                                      CRUX_ACCESS_PERMISSION_MANAGE};
static const crux_access_role_t reference_roles[] = {
    {"viewer", reference_viewer, 1},
    {"operator", reference_operator, 2},
    {"administrator", reference_administrator, 3},
};

const access_test_profile_t access_test_profile_reference = {
    .policy = {reference_roles, 3, 15u * 60u, 8u * 3600u, NULL},
    .administrator_role = "administrator",
    .limited_role = "operator",
    .other_limited_role = "viewer",
    .granted_to_limited = "config.write",
    .not_granted_to_limited = CRUX_ACCESS_PERMISSION_MANAGE,
    .unknown_permission = "firmware.upgrade",
    .password = ACCESS_TEST_PASSWORD,
    .other_password = ACCESS_TEST_OTHER_PASSWORD,
    .rejected_password = "too short",
};

/* A different product: its own names and limits, and a rule that replaces the reference rule. */
static bool product_password_rule(const char *password, size_t length)
{
    return length >= 4 && password[0] != '!';
}

static const char *const product_technician[] = {"app.pump.read", "file.read"};
static const char *const product_reader[] = {"app.pump.read"};
static const char *const product_root[] = {CRUX_ACCESS_PERMISSION_MANAGE, "app.pump.write",
                                           "app.pump.read", "file.read"};
static const crux_access_role_t product_roles[] = {
    {"root", product_root, 4},
    {"technician", product_technician, 2},
    {"reader", product_reader, 1},
};

const access_test_profile_t access_test_profile_product = {
    .policy = {product_roles, 3, 60u, 600u, product_password_rule},
    .administrator_role = "root",
    .limited_role = "technician",
    .other_limited_role = "reader",
    .granted_to_limited = "file.read",
    .not_granted_to_limited = "app.pump.write",
    .unknown_permission = "status.read",
    .password = "pin1",
    .other_password = "pin2",
    .rejected_password = "!secret-and-long-enough",
};

const crux_access_origin_t access_test_serial = {CRUX_ACCESS_CHANNEL_SERIAL, {0}, 0};
const crux_access_origin_t access_test_web = {CRUX_ACCESS_CHANNEL_WEB, {192, 0, 2, 1}, 4};
const crux_access_origin_t access_test_web_other = {CRUX_ACCESS_CHANNEL_WEB, {192, 0, 2, 2}, 4};
const crux_access_origin_t access_test_ssh = {CRUX_ACCESS_CHANNEL_SSH, {192, 0, 2, 1}, 4};

void access_test_boot(const crux_access_policy_t *policy)
{
    access_fake_reset();
    crux_access_test_reset();
    CHECK(crux_access_init(policy) == CRUX_ACCESS_OK);
}

void access_test_power_cycle(const crux_access_policy_t *policy)
{
    crux_access_test_reset();
    CHECK(crux_access_init(policy) == CRUX_ACCESS_OK);
}

void access_test_boot_with_admin(void)
{
    access_test_boot(&access_test_profile_reference.policy);
    CHECK(crux_access_provision_first_account(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD,
                                              strlen(ACCESS_TEST_PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    access_fake_events_clear();
    access_fake_derive_counters_clear();
}

crux_access_result_t access_test_login(const char *user, const char *password,
                                       const crux_access_origin_t *origin,
                                       crux_access_session_t *session)
{
    uint32_t retry_after_ms = 0;
    return crux_access_login_password(user, password, strlen(password), origin, session,
                                      &retry_after_ms);
}

crux_access_session_t access_test_session(const char *user, const char *password,
                                          const crux_access_origin_t *origin)
{
    crux_access_session_t session = {0};
    CHECK(access_test_login(user, password, origin, &session) == CRUX_ACCESS_OK);
    CHECK(session.value != 0);
    return session;
}

void access_test_create_user(crux_access_session_t admin)
{
    CHECK(crux_access_account_create(admin, ACCESS_TEST_USER, "operator", ACCESS_TEST_PASSWORD,
                                     strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
}

crux_access_origin_t access_test_web_from(uint32_t value)
{
    crux_access_origin_t origin = {CRUX_ACCESS_CHANNEL_WEB, {10}, 4};
    origin.source[1] = (uint8_t)(value >> 16);
    origin.source[2] = (uint8_t)(value >> 8);
    origin.source[3] = (uint8_t)value;
    return origin;
}
