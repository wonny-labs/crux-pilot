/* A stand-in for the Access core and session lookup: this image only has to link. */
#include "crux_web_adapter.h"

crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission)
{
    (void)session;
    (void)permission;
    return CRUX_ACCESS_ERR_DENIED;
}

crux_access_result_t crux_access_permitted(crux_access_session_t session, const char *permission,
                                           bool *permitted)
{
    (void)session;
    (void)permission;
    *permitted = false;
    return CRUX_ACCESS_OK;
}

int main(void)
{
    static const crux_web_config_t config = {
        .request_deadline_ms = CONFIG_CRUX_WEB_REQUEST_DEADLINE_MS, .handler_limit_ms = 100u};

    return crux_web_start(&config) == CRUX_WEB_OK ? 0 : 1;
}
