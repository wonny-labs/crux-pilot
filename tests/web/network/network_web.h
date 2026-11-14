/* What the network route suites share: the real Web core, session module, Access core,
 * Configuration service and network settings service, with the fakes of tests/network_settings and
 * the fake transport of tests/web. Each case runs in a forked child (net_run_case). */
#ifndef CRUX_WEB_NETWORK_TEST_H
#define CRUX_WEB_NETWORK_TEST_H

#include "net_test.h"
#include "session_test.h"
#include "web_management_network.h"

#include <crux/network_settings.h>

/* Boots every service and the web half, and ends the direct sessions of net_test.c, because Access
 * holds eight and the web logins need room. net_test.h lists the accounts. */
void web_net_setup(void);
/* Everything but crux_network_settings_start(), for the answer of a service that is not ready. */
void web_net_setup_not_started(void);
/* The same on a store that keeps what the case wrote. */
void web_net_setup_on_store(void);

/* A web login of the account, made once for the case. */
const session_jar_t *web_net_jar(const char *user);

const fake_reply_t *web_net_get(const session_jar_t *jar);
const fake_reply_t *web_net_put(const session_jar_t *jar, const char *body);
/* A request with no session and no token, as a script would send it. */
const fake_reply_t *web_net_anonymous(unsigned method, const char *body);

typedef struct {
    bool no_token;
    bool no_origin;
    const char *content_type; /* NULL keeps application/json, "" sends none. */
} web_net_variation_t;
const fake_reply_t *web_net_vary(const session_jar_t *jar, unsigned method, const char *body,
                                 web_net_variation_t variation);

void web_net_expect_body(const fake_reply_t *reply, int status, const char *body);
void web_net_expect_error(const fake_reply_t *reply, int status, const char *code);
void web_net_expect_headers(const fake_reply_t *reply, const char *content_type);
void web_net_expect_retry_after(const fake_reply_t *reply);

#define NETWORK_URL "/api/v1/network"

#endif
