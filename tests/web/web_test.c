/* See web_test.h. */
#include "web_test.h"

#include "web_port.h"

#include <stdlib.h>
#include <string.h>

web_handler_record_t web_record;
crux_web_auth_t web_auth_force;
unsigned web_auth_calls;
const crux_web_request_head_t *web_auth_last_head;
crux_access_session_t web_admin, web_viewer, web_forced;
char web_admin_cookie[32], web_viewer_cookie[32], web_forced_cookie[32];

static uint64_t web_clock_ms;

uint64_t crux_web_port_now_ms(void)
{
    return web_clock_ms;
}

void web_test_clock_set(uint64_t now_ms)
{
    web_clock_ms = now_ms;
}

static crux_web_auth_t fake_authenticate(const crux_web_request_head_t *head,
                                         crux_access_session_t *session, void *context)
{
    (void)context;
    ++web_auth_calls;
    web_auth_last_head = head;
    if (web_auth_force != CRUX_WEB_AUTH_OK) {
        return web_auth_force;
    }
    if (head->cookie == NULL || strncmp(head->cookie, "sid=", 4) != 0) {
        return CRUX_WEB_AUTH_NO_SESSION;
    }
    session->value = (uint32_t)strtoul(head->cookie + 4, NULL, 10);
    return CRUX_WEB_AUTH_OK;
}

void web_test_use_cookie(char *cookie, size_t size, crux_access_session_t session)
{
    snprintf(cookie, size, "sid=%u", (unsigned)session.value);
}

static const char *const admin_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE, "network.read",
                                                "network.write", "config.save"};
static const char *const viewer_permissions[] = {"network.read"};
static const crux_access_role_t web_roles[] = {
    {"administrator", admin_permissions, 4},
    {"viewer", viewer_permissions, 1},
};
static const crux_access_policy_t web_policy = {web_roles, 2, 900u, 28800u, NULL};
static const crux_access_origin_t web_serial = {CRUX_ACCESS_CHANNEL_SERIAL, {0}, 0};
static const crux_access_origin_t web_origin = {CRUX_ACCESS_CHANNEL_WEB, {192, 0, 2, 7}, 4};

#define WEB_TEST_PASSWORD "correct horse battery"

static crux_access_session_t login(const char *user)
{
    crux_access_session_t session = {0};
    uint32_t retry = 0;

    CHECK(crux_access_login_password(user, WEB_TEST_PASSWORD, strlen(WEB_TEST_PASSWORD),
                                     &web_origin, &session, &retry) == CRUX_ACCESS_OK);
    return session;
}

void web_test_setup(bool no_port)
{
    crux_web_config_t config = {no_port ? NULL : fake_authenticate, NULL, WEB_TEST_DEADLINE_MS,
                                WEB_TEST_HANDLER_LIMIT_MS};

    memset(&web_record, 0, sizeof(web_record));
    web_auth_force = CRUX_WEB_AUTH_OK;
    web_auth_calls = 0;
    web_auth_last_head = NULL;
    web_clock_ms = 1000u;

    access_fake_reset();
    crux_access_test_reset();
    CHECK(crux_access_init(&web_policy) == CRUX_ACCESS_OK);
    CHECK(crux_access_provision_first_account("admin", WEB_TEST_PASSWORD, strlen(WEB_TEST_PASSWORD),
                                              "administrator", &web_serial) == CRUX_ACCESS_OK);
    web_admin = login("admin");
    CHECK(crux_access_account_create(web_admin, "viewer", "viewer", WEB_TEST_PASSWORD,
                                     strlen(WEB_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    web_viewer = login("viewer");
    CHECK(crux_access_account_create(web_admin, "forced", "viewer", WEB_TEST_PASSWORD,
                                     strlen(WEB_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_reset_password(web_admin, "forced", WEB_TEST_PASSWORD,
                                             strlen(WEB_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    web_forced = login("forced");
    web_test_use_cookie(web_admin_cookie, sizeof(web_admin_cookie), web_admin);
    web_test_use_cookie(web_viewer_cookie, sizeof(web_viewer_cookie), web_viewer);
    web_test_use_cookie(web_forced_cookie, sizeof(web_forced_cookie), web_forced);

    crux_web_core_init(&config);
    fake_transport_reset();
}
