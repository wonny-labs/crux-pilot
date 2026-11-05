/*
 * The calls that the access entry points of #192 make into crux_access, each refusing. This image is a
 * compile and link check of the settings arrangement, is never run, and has no access core of its own.
 */
#include <crux_access_adapter.h>

crux_access_result_t crux_access_login_password(const char *user, const char *password,
                                                size_t length, const crux_access_origin_t *origin,
                                                crux_access_session_t *session,
                                                uint32_t *retry_after_ms)
{
    (void)user;
    (void)password;
    (void)length;
    (void)origin;
    (void)session;
    (void)retry_after_ms;
    return CRUX_ACCESS_ERR_NOT_READY;
}

crux_access_result_t crux_access_key_is_authorised(const char *user, const uint8_t *key,
                                                   size_t key_length, bool signed_request,
                                                   const crux_access_origin_t *origin,
                                                   uint32_t *retry_after_ms)
{
    (void)user;
    (void)key;
    (void)key_length;
    (void)signed_request;
    (void)origin;
    (void)retry_after_ms;
    return CRUX_ACCESS_ERR_NOT_READY;
}

crux_access_result_t crux_access_login_verified(const char *user, const uint8_t *key,
                                                size_t key_length,
                                                const crux_access_origin_t *origin,
                                                crux_access_session_t *session)
{
    (void)user;
    (void)key;
    (void)key_length;
    (void)origin;
    (void)session;
    return CRUX_ACCESS_ERR_NOT_READY;
}

void crux_access_report_key_failure(const char *user, const crux_access_origin_t *origin)
{
    (void)user;
    (void)origin;
}

crux_access_result_t crux_access_session_user(crux_access_session_t session, char *name, size_t size)
{
    (void)session;
    (void)name;
    (void)size;
    return CRUX_ACCESS_ERR_NOT_READY;
}

void crux_access_logout(crux_access_session_t *session)
{
    (void)session;
}
