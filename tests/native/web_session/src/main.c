/*
 * Login and sessions on native_sim: the management composition with the real Access core on its
 * real PSA ports, the session module, and the web adapter on plain HTTP over the native socket
 * offload. test_driver.py is the client. Test only: the entropy is test-only, so no token strength
 * is shown, and the first account is the composition's declared test step.
 */
#include "crux_native_management.h"
#include "crux_web_session.h"

#include <crux_access_adapter.h>

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* A second account, for the wrong-role cases. Its password is a declared test value. */
#define VIEWER_USER "viewer"
#define VIEWER_PASSWORD "session test viewer password"

static bool accounts_ready(void)
{
    crux_access_session_t admin = {0};
    crux_access_result_t result;

    if (crux_native_management_store_state_at_start() == CRUX_CREDENTIAL_STATE_EMPTY &&
        crux_native_management_provision_test_account() != CRUX_ACCESS_OK) {
        return false;
    }
    if (crux_native_management_login(CRUX_NATIVE_MANAGEMENT_TEST_USER,
                                     CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD,
                                     &admin) != CRUX_ACCESS_OK) {
        return false;
    }
    result = crux_access_account_create(admin, VIEWER_USER, CRUX_NATIVE_MANAGEMENT_ROLE_VIEWER,
                                        VIEWER_PASSWORD, strlen(VIEWER_PASSWORD));
    crux_access_logout(&admin);
    return result == CRUX_ACCESS_OK || result == CRUX_ACCESS_ERR_ALREADY_EXISTS;
}

int main(void)
{
    /* The plain HTTP test image says so, and a product composition passes "https". */
    static const crux_web_session_config_t session_config = {"http", NULL, 0u};
    static const crux_web_config_t config = {
        .authenticate = crux_web_session_authenticate,
        .request_deadline_ms = CONFIG_CRUX_WEB_REQUEST_DEADLINE_MS,
        .handler_limit_ms = 100u,
    };
    crux_native_management_start_t started = crux_native_management_start();

    if (started != CRUX_NATIVE_MANAGEMENT_STARTED || !accounts_ready()) {
        printk("CRUX_SESSION_START_FAILED %d\n", (int)started);
        return 1;
    }
    if (crux_web_session_init(&session_config) != CRUX_WEB_OK ||
        crux_web_session_start_worker() != CRUX_WEB_OK || crux_web_start(&config) != CRUX_WEB_OK) {
        printk("CRUX_SESSION_START_FAILED web\n");
        return 1;
    }
    printk("CRUX_SESSION_READY %d\n", CONFIG_CRUX_WEB_PORT);
    return 0;
}
