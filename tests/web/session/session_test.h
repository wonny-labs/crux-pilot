/* What the session Host suites share: the real Web, session and Access cores, the fake Access
 * ports, fake session ports, and the fake transport of tests/web. */
#ifndef CRUX_WEB_SESSION_TEST_H
#define CRUX_WEB_SESSION_TEST_H

#include "access_test.h"
#include "fake_transport.h"
#include "web_session_internal.h"

#define SESSION_TEST_HOST "device.test"
#define SESSION_TEST_ORIGIN "https://device.test"
#define SESSION_TEST_PASSWORD ACCESS_TEST_PASSWORD

/* One request as a browser would send it. Unset fields are absent headers. */
typedef struct {
    unsigned method;
    const char *url;
    const char *cookie;
    const char *origin;
    const char *host;
    const char *xsrf;
    const char *crux_request;
    const char *content_type;
    const char *body;
} session_request_t;

/* A browser's cookie jar for this device: the two values a login set. */
typedef struct {
    char session[64];
    char xsrf[64];
    char header[160]; /* The Cookie header a browser would send back. */
} session_jar_t;

extern crux_access_session_t session_admin; /* A session made directly, for account changes. */

/* Fresh Access with the accounts admin (administrator), viewer and forced (viewer, forced change),
 * a fresh core with the session lookup, and a fresh transport. No worker thread. */
void session_test_setup(void);
/* Only the web half of the above: the core, the session module, the fake ports and the transport,
 * for a suite that boots Access with its own accounts. The caller has initialised Access. */
void session_test_setup_web(void);

/* Sends a request on a new connection and returns the reply, which stays valid until the next. */
const fake_reply_t *session_send(const session_request_t *request);
/* Every value of a header in a reply, in order. Returns how many. */
size_t session_reply_headers(const fake_reply_t *reply, const char *name, const char **values,
                             size_t capacity);
/* The value of "name":"value" in a JSON body, or false. */
bool session_json_string(const char *body, const char *name, char *value, size_t size);

/* POST /api/v1/session with a well-formed browser request. Writes the identifier on 202. */
const fake_reply_t *session_post_login(const char *user, const char *password, char *id,
                                       size_t size);
/* GET /api/v1/session/attempt/{id} with X-Crux-Request: 1. */
const fake_reply_t *session_poll(const char *id);
/* A whole login through the worker step. Fills jar on success, and returns the final poll reply. */
const fake_reply_t *session_login(const char *user, const char *password, session_jar_t *jar);
/* A request with the jar's cookie, and on any method but GET also its token and Origin. */
const fake_reply_t *session_with(const session_jar_t *jar, unsigned method, const char *url,
                                 const char *body);

/* The fake session ports. */
void session_fake_random_fail(bool fail);
unsigned session_fake_random_calls(void);
/* The bytes of the last draw from the port. */
const uint8_t *session_fake_random_last(void);
void session_fake_hash_fail(bool fail);

/* Both clocks: the web core's and Access's. */
void session_clock_advance(uint64_t delta_ms);

int session_test_finish(const char *suite);

#endif /* CRUX_WEB_SESSION_TEST_H */
