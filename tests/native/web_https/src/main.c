/*
 * The web adapter over HTTPS, on the management composition of Issue #242 and its declared test
 * certificate. tests/native/web_https/https_driver.py is the client, and runs the plain HTTP suite
 * of tests/native/web_http over TLS. Test only: entropy is test-only and the certificate's key is
 * public.
 *
 * The real Access core enforces each route's class. The session lookup and the CSRF rule are
 * stand-ins for Issue #186: a cookie names one of two real sessions that start logs in.
 */
#include "crux_native_management.h"
#include "web_probe.h"

#include <crux_access_adapter.h>

#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "cmdline.h"
#include "soc.h"

#define VIEWER_USER "viewer1"
#define VIEWER_FIRST_PASSWORD "first password of an account"
#define VIEWER_PASSWORD "a password changed at first login"

void crux_nsos_set_rdhup(int on);

static bool no_tls_credential;
static bool no_rdhup;
static crux_access_session_t admin_session;
static crux_access_session_t viewer_session;

static void https_options(void)
{
    static struct args_struct_t options[] = {
        {.option = "no-tls-credential",
         .is_switch = true,
         .type = 'b',
         .dest = &no_tls_credential,
         .descript = "Start with no certificate under the service's tag, for a negative case"},
        {.option = "no-rdhup",
         .is_switch = true,
         .type = 'b',
         .dest = &no_rdhup,
         .descript = "Report a peer's FIN as the unmodified offload does, as a control"},
        ARG_TABLE_ENDMARKER};

    native_add_command_line_opts(options);
}
NATIVE_TASK(https_options, PRE_BOOT_1, 1);

static crux_web_auth_t https_authenticate(const crux_web_request_head_t *head,
                                          crux_access_session_t *session, void *context)
{
    const char *cookie = head->cookie;

    (void)context;
    if (cookie == NULL) {
        return CRUX_WEB_AUTH_NO_SESSION;
    }
    if (strstr(cookie, "crux_session=admin") != NULL) {
        *session = admin_session;
    } else if (strstr(cookie, "crux_session=viewer") != NULL) {
        *session = viewer_session;
    } else {
        return CRUX_WEB_AUTH_NO_SESSION;
    }
    if (head->method != CRUX_WEB_GET && head->xsrf_token == NULL) {
        return CRUX_WEB_AUTH_CSRF_REJECTED;
    }
    return CRUX_WEB_AUTH_OK;
}

static bool sessions_ready(void)
{
    uint32_t retry_after_ms = 0;

    return crux_native_management_provision_test_account() == CRUX_ACCESS_OK &&
           crux_native_management_login(CRUX_NATIVE_MANAGEMENT_TEST_USER,
                                        CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD,
                                        &admin_session) == CRUX_ACCESS_OK &&
           crux_access_account_create(admin_session, VIEWER_USER,
                                      CRUX_NATIVE_MANAGEMENT_ROLE_VIEWER, VIEWER_FIRST_PASSWORD,
                                      strlen(VIEWER_FIRST_PASSWORD)) == CRUX_ACCESS_OK &&
           crux_native_management_login(VIEWER_USER, VIEWER_FIRST_PASSWORD, &viewer_session) ==
               CRUX_ACCESS_OK &&
           crux_access_account_change_password(
               viewer_session, VIEWER_FIRST_PASSWORD, strlen(VIEWER_FIRST_PASSWORD),
               VIEWER_PASSWORD, strlen(VIEWER_PASSWORD), &retry_after_ms) == CRUX_ACCESS_OK;
}

int main(void)
{
    static const crux_web_config_t config = {
        .authenticate = https_authenticate,
        .request_deadline_ms = CONFIG_CRUX_WEB_REQUEST_DEADLINE_MS,
        .handler_limit_ms = 100u,
    };

    crux_nsos_set_rdhup(no_rdhup ? 0 : 1);
    if (crux_native_management_start() != CRUX_NATIVE_MANAGEMENT_STARTED || !sessions_ready()) {
        printk("CRUX_WEB_START_FAILED composition\n");
        return 1;
    }
    if (!no_tls_credential &&
        crux_native_management_tls_test_credentials(CONFIG_CRUX_WEB_TLS_SEC_TAG) != 0) {
        printk("CRUX_WEB_START_FAILED credential\n");
        return 1;
    }
    web_probe_fill_large_body();
    if (crux_web_start(&config) != CRUX_WEB_OK) {
        printk("CRUX_WEB_START_FAILED web\n");
        return 1;
    }
    printk("CRUX_WEB_READY %d tls %s rdhup %s\n", CONFIG_CRUX_WEB_PORT,
           no_tls_credential ? "none" : "test-certificate", no_rdhup ? "off" : "on");
    return 0;
}
