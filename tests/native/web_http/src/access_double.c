/* A stand-in for the Access core and the session lookup of Issue #186. Access has no Zephyr
 * composition until Issue #242, and the Host suites run enforcement on the real core. The cookie
 * names a session. */
#include "crux_web_adapter.h"
#include "web_probe.h"

#include <string.h>

#define SESSION_ADMIN 1u
#define SESSION_VIEWER 2u

crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission)
{
    if (session.value == SESSION_ADMIN) {
        return CRUX_ACCESS_OK;
    }
    if (session.value == SESSION_VIEWER) {
        return strcmp(permission, "network.read") == 0 || strcmp(permission, "app.pump.read") == 0
                   ? CRUX_ACCESS_OK
                   : CRUX_ACCESS_ERR_DENIED;
    }
    return CRUX_ACCESS_ERR_SESSION_INVALID;
}

crux_access_result_t crux_access_permitted(crux_access_session_t session, const char *permission,
                                           bool *permitted)
{
    crux_access_result_t result = crux_access_check(session, permission);

    if (result == CRUX_ACCESS_ERR_SESSION_INVALID) {
        return result;
    }
    *permitted = result == CRUX_ACCESS_OK;
    return CRUX_ACCESS_OK;
}

crux_web_auth_t web_probe_authenticate(const crux_web_request_head_t *head,
                                       crux_access_session_t *session, void *context)
{
    const char *cookie = head->cookie;

    (void)context;
    if (cookie == NULL) {
        return CRUX_WEB_AUTH_NO_SESSION;
    }
    if (strstr(cookie, "crux_session=admin") != NULL) {
        session->value = SESSION_ADMIN;
    } else if (strstr(cookie, "crux_session=viewer") != NULL) {
        session->value = SESSION_VIEWER;
    } else {
        return CRUX_WEB_AUTH_NO_SESSION;
    }
    /* Enough of #186's rule to show the header reaches this port on a state-changing request. */
    if (head->method != CRUX_WEB_GET && head->xsrf_token == NULL) {
        return CRUX_WEB_AUTH_CSRF_REJECTED;
    }
    return CRUX_WEB_AUTH_OK;
}
