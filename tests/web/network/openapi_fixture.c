/* Prints what the network routes answer, one record on each line, for check_openapi_network.py to
 * hold to the management API specification. The routes are the real ones, on the real services, and
 * a record is a real answer. Built twice: with a Configuration provider, which reaches every status
 * of the routes but one, and with the no-provider port, which reaches the 501. The 409 is the
 * server's own and no route produces it.
 *
 * A record is: METHOD TEMPLATE STATUS RETRY-AFTER SECURITY-HEADERS CONTENT-TYPE BODY, with a dash
 * for a header that is absent, and 1 for SECURITY-HEADERS when all four of them are present. */
#include "network_web.h"

static const fake_reply_t *show(const char *method, const fake_reply_t *reply)
{
    const char *retry = fake_reply_header(reply, "Retry-After");
    const char *type = fake_reply_header(reply, "Content-Type");
    bool security = fake_reply_header(reply, "Content-Security-Policy") != NULL &&
                    fake_reply_header(reply, "X-Content-Type-Options") != NULL &&
                    fake_reply_header(reply, "Referrer-Policy") != NULL &&
                    fake_reply_header(reply, "Cache-Control") != NULL;

    printf("RECORD %s %s %d %s %d %s %s\n", method, NETWORK_URL, reply->status,
           retry != NULL ? retry : "-", security ? 1 : 0, type != NULL ? type : "-", reply->body);
    return reply;
}

static const fake_reply_t *get(const session_jar_t *jar)
{
    return show("GET", web_net_get(jar));
}

static const fake_reply_t *put(const session_jar_t *jar, const char *body)
{
    return show("PUT", web_net_put(jar, body));
}

#define SET_A "{\"address\":\"198.51.100.20\",\"prefix_length\":24,\"gateway\":\"198.51.100.1\"}"
#define SET_B "{\"address\":\"203.0.113.20\",\"prefix_length\":26}"

#ifdef FIXTURE_NO_PROVIDER
static void no_provider(void)
{
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    (void)get(jar);
    (void)put(jar, SET_A);
    (void)put(jar, "{\"address\":\"224.0.0.1\",\"prefix_length\":24}");
}
#else
static void reads(void)
{
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    (void)get(jar);
    (void)get(web_net_jar("reader"));
    (void)show("GET", web_net_anonymous(CRUX_WEB_GET, NULL));
    (void)get(web_net_jar("writer"));
    (void)get(web_net_jar("forced"));
    net_port_set_running(false, NULL);
    (void)get(jar);
}

static void saves(void)
{
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    (void)put(jar, SET_A);
    (void)put(jar, SET_B);
    (void)get(jar);
    (void)crux_network_settings_work_once();
    (void)get(jar);
    (void)put(jar, SET_B);
    (void)crux_network_settings_work_once();
    (void)get(jar);
}

static void a_failed_save(void)
{
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    config_fault_write_fail_key(CRUX_NETWORK_SETTINGS_KEY_CHECK, CRUX_CONFIG_ERR_NO_SPACE);
    (void)put(jar, SET_A);
    (void)crux_network_settings_work_once();
    (void)get(jar);
}

static void saved_values_at_boot(void)
{
    const session_jar_t *jar;

    config_fault_reset();
    net_store_set(&net_set_a, true);
    web_net_setup_on_store();
    jar = web_net_jar("both");
    (void)get(jar);
}

static void refusals(void)
{
    const session_jar_t *jar;
    char big[WEB_MGMT_NETWORK_BODY_MAX + 2u];

    web_net_setup();
    jar = web_net_jar("both");
    (void)put(jar, "{");
    (void)put(jar, "{\"address\":\"224.0.0.1\",\"prefix_length\":31,\"gateway\":\"198.51.100.1\"}");
    (void)put(jar,
              "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"gateway\":\"198.51.100.1\"}");
    (void)show("PUT", web_net_anonymous(CRUX_WEB_PUT, SET_A));
    (void)put(web_net_jar("reader"), SET_A);
    (void)put(web_net_jar("forced"), SET_A);
    memset(big, ' ', sizeof(big));
    big[sizeof(big) - 1u] = '\0';
    (void)put(jar, big);
}

static void not_ready(void)
{
    const session_jar_t *jar;

    web_net_setup_not_started();
    jar = web_net_jar("both");
    (void)get(jar);
    (void)put(jar, SET_A);
}
#endif

int main(void)
{
#ifdef FIXTURE_NO_PROVIDER
    net_run_case("no provider", no_provider);
#else
    net_run_case("reads", reads);
    net_run_case("saves", saves);
    net_run_case("a failed save", a_failed_save);
    net_run_case("saved values at boot", saved_values_at_boot);
    net_run_case("refusals", refusals);
    net_run_case("not ready", not_ready);
#endif
    return net_finish("web_network_openapi_fixture");
}
