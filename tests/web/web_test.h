/* What the Web Host suites share: the real Access core on its fake ports, a fake session lookup,
 * the fake transport and a recording handler. */
#ifndef CRUX_WEB_TEST_H
#define CRUX_WEB_TEST_H

#include "access_test.h"
#include "crux_web_adapter.h"
#include "fake_transport.h"

#define WEB_TEST_DEADLINE_MS 5000u
#define WEB_TEST_HANDLER_LIMIT_MS 100u

/* What the handlers do, set by a test. */
typedef enum {
    WEB_HANDLER_OK,
    WEB_HANDLER_NO_RESPONSE,
    WEB_HANDLER_FAIL,
    WEB_HANDLER_SLOW,
    WEB_HANDLER_API_CHECKS
} web_handler_mode_t;

typedef struct {
    unsigned calls;
    const char *route;
    crux_access_session_t session;
    char params[2][64];
    char query[64];
    char body[1200];
    size_t body_length;
    char content_type[64];
    web_handler_mode_t mode;
} web_handler_record_t;

extern web_handler_record_t web_record;

/* The fake lookup: the Cookie value "sid=<number>" names a session, and these force an outcome. */
extern crux_web_auth_t web_auth_force;
extern unsigned web_auth_calls;
extern const crux_web_request_head_t *web_auth_last_head;

/* Sessions of real logins, from web_test_setup(). */
extern crux_access_session_t web_admin, web_viewer, web_forced;
extern char web_admin_cookie[32], web_viewer_cookie[32], web_forced_cookie[32];

/* A fresh core, transport, Access and accounts. With @p no_port the core has no session lookup. */
void web_test_setup(bool no_port);
void web_test_use_cookie(char *cookie, size_t size, crux_access_session_t session);

/* A clock the core reads, in milliseconds. */
void web_test_clock_set(uint64_t now_ms);

/* The module's body store, to prove a body does not stay in it. */
bool web_core_holds_bytes(const void *bytes, size_t length);

#endif /* CRUX_WEB_TEST_H */
