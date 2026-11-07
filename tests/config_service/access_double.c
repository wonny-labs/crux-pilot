/* A double of access that permits everything and takes no lock, for the suites where access is not
 * what is under test. The other suites run on the real core. */
#include <crux/access.h>

crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission)
{
    (void)session;
    (void)permission;
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_permitted(crux_access_session_t session, const char *permission,
                                           bool *permitted)
{
    (void)session;
    (void)permission;
    *permitted = true;
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_session_user(crux_access_session_t session, char *name,
                                              size_t size)
{
    (void)session;
    (void)size;
    name[0] = '\0';
    return CRUX_ACCESS_OK;
}
