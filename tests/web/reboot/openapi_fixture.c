/* Prints what the reboot routes answer, one record on each line, for check_openapi_reboot.py to
 * hold to the management API specification. The routes are the real ones, on the real service, and
 * a record is a real answer. The 409 is the server's own and no route produces it.
 *
 * A record is: METHOD TEMPLATE STATUS RETRY-AFTER SECURITY-HEADERS CONTENT-TYPE BODY, with a dash
 * for a header that is absent, and 1 for SECURITY-HEADERS when all four of them are present. */
#include "reboot_web.h"

static const fake_reply_t *show(const char *method, const fake_reply_t *reply)
{
    const char *retry = fake_reply_header(reply, "Retry-After");
    const char *type = fake_reply_header(reply, "Content-Type");
    bool security = fake_reply_header(reply, "Content-Security-Policy") != NULL &&
                    fake_reply_header(reply, "X-Content-Type-Options") != NULL &&
                    fake_reply_header(reply, "Referrer-Policy") != NULL &&
                    fake_reply_header(reply, "Cache-Control") != NULL;

    printf("RECORD %s %s %d %s %d %s %s\n", method, REBOOT_URL, reply->status,
           retry != NULL ? retry : "-", security ? 1 : 0, type != NULL ? type : "-", reply->body);
    return reply;
}

static const fake_reply_t *get(const session_jar_t *jar)
{
    return show("GET", web_reboot_get(jar));
}

static const fake_reply_t *post(const session_jar_t *jar)
{
    return show("POST", web_reboot_post(jar));
}

static void reads(void)
{
    web_reboot_setup();
    (void)get(web_reboot_jar("admin"));
    (void)get(web_reboot_jar("rebooter"));
    (void)show("GET", web_reboot_anonymous(CRUX_WEB_GET));
    (void)get(web_reboot_jar("reader"));
    (void)get(web_reboot_jar("forced"));
}

static void accepted_and_then_each_outcome(void)
{
    const bool script[] = {true, false};
    const session_jar_t *jar;

    web_reboot_setup();
    jar = web_reboot_jar("rebooter");
    (void)post(jar);
    (void)get(jar);
    (void)post(jar);
    /* The reset returns: failed. */
    (void)crux_reboot_work_once();
    (void)get(jar);
    /* The second check refuses: refused, with the reason. */
    reboot_policy_script(script, 2u);
    reboot_policy_reason("Machine cycle started", 22u);
    (void)post(jar);
    (void)crux_reboot_work_once();
    (void)get(jar);
}

static void refused_at_the_request(void)
{
    const session_jar_t *jar;

    web_reboot_setup();
    reboot_policy_always(false);
    reboot_policy_reason("Machine cycle running", 22u);
    jar = web_reboot_jar("admin");
    (void)post(jar);
    (void)get(jar);
    reboot_policy_reason("Say \"no\" to C:\\run", 19u);
    (void)post(jar);
    (void)get(jar);
    reboot_policy_reason("bad\x01text", 8u);
    (void)post(jar);
}

static void refusals(void)
{
    web_reboot_variation_t no_token = {.no_token = true};
    web_reboot_variation_t no_origin = {.no_origin = true};
    const session_jar_t *jar;

    web_reboot_setup();
    jar = web_reboot_jar("rebooter");
    (void)show("POST", web_reboot_anonymous(CRUX_WEB_POST));
    (void)post(web_reboot_jar("reader"));
    (void)post(web_reboot_jar("forced"));
    (void)show("POST", web_reboot_vary(jar, CRUX_WEB_POST, no_token));
    (void)show("POST", web_reboot_vary(jar, CRUX_WEB_POST, no_origin));
}

static void not_ready(void)
{
    const session_jar_t *jar;

    web_reboot_setup_not_started();
    jar = web_reboot_jar("admin");
    (void)get(jar);
    (void)post(jar);
}

static void busy(void)
{
    const session_jar_t *jar;

    web_reboot_setup();
    jar = web_reboot_jar("rebooter");
    web_reboot_fail_lock(20u);
    (void)get(jar);
    (void)post(jar);
    web_reboot_fail_lock(0u);
}

int main(void)
{
    reboot_run_case("reads", reads);
    reboot_run_case("accepted and then each outcome", accepted_and_then_each_outcome);
    reboot_run_case("refused at the request", refused_at_the_request);
    reboot_run_case("refusals", refusals);
    reboot_run_case("not ready", not_ready);
    reboot_run_case("busy", busy);
    return reboot_finish("web_reboot_openapi_fixture");
}
