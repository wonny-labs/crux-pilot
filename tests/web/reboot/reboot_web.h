/* What the reboot route suites share: the real Web core, session module, Access core and reboot
 * service, with the fakes of tests/reboot and the fake transport of tests/web. Each case runs in a
 * forked child (reboot_run_case). */
#ifndef CRUX_WEB_REBOOT_TEST_H
#define CRUX_WEB_REBOOT_TEST_H

#include "reboot_test.h"
#include "session_test.h"

#define REBOOT_URL "/api/v1/system/reboot"

/* Boots Access, the service and the web half, and ends the direct sessions of reboot_test.c,
 * because Access holds eight and the web logins need room. reboot_test.h lists the accounts. */
void web_reboot_setup(void);
/* Everything but crux_reboot_start(), for the answer of a service that is not ready. */
void web_reboot_setup_not_started(void);

/* Makes the service's wait of this many ms for its state lock fail, or none with 0. */
void web_reboot_fail_lock(uint32_t wait_ms);

/* A web login of the account, made once for the case. */
const session_jar_t *web_reboot_jar(const char *user);

const fake_reply_t *web_reboot_get(const session_jar_t *jar);
const fake_reply_t *web_reboot_post(const session_jar_t *jar);
/* A request with no session and no token, as a script would send it. */
const fake_reply_t *web_reboot_anonymous(unsigned method);

typedef struct {
    bool no_token;
    bool no_origin;
    const char *body; /* NULL sends no body. */
} web_reboot_variation_t;
const fake_reply_t *web_reboot_vary(const session_jar_t *jar, unsigned method,
                                    web_reboot_variation_t variation);

void web_reboot_expect_body(const fake_reply_t *reply, int status, const char *body);
void web_reboot_expect_error(const fake_reply_t *reply, int status, const char *code);
void web_reboot_expect_headers(const fake_reply_t *reply, const char *content_type);
void web_reboot_expect_retry_after(const fake_reply_t *reply);

#endif
