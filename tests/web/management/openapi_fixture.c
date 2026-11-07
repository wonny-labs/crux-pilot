/* Prints what the status and configuration routes answer, one record on each line, for
 * check_openapi_config.py to hold to the management API specification. The routes are the real
 * ones, on the real services, and a record is a real answer. Between them the cases reach every
 * status that the specification declares for these routes except the server's own, the 409.
 *
 * A record is: METHOD TEMPLATE STATUS RETRY-AFTER SECURITY-HEADERS CONTENT-TYPE BODY, with a dash
 * for a header that is absent, and 1 for SECURITY-HEADERS when all four of them are present. */
#include "management_test.h"

static const char *template_of(const char *url)
{
    if (strcmp(url, "/api/v1/config/save") == 0 || strcmp(url, "/api/v1/status") == 0 ||
        strcmp(url, "/api/v1/config") == 0) {
        return url;
    }
    return strncmp(url, "/api/v1/config/", 15u) == 0 ? "/api/v1/config/{name}" : url;
}

static const char *method_name(unsigned method)
{
    switch (method) {
    case CRUX_WEB_GET:
        return "GET";
    case CRUX_WEB_PUT:
        return "PUT";
    case CRUX_WEB_POST:
        return "POST";
    default:
        return "DELETE";
    }
}

static const fake_reply_t *show(unsigned method, const char *url, const fake_reply_t *reply)
{
    const char *retry = fake_reply_header(reply, "Retry-After");
    const char *type = fake_reply_header(reply, "Content-Type");
    bool security = fake_reply_header(reply, "Content-Security-Policy") != NULL &&
                    fake_reply_header(reply, "X-Content-Type-Options") != NULL &&
                    fake_reply_header(reply, "Referrer-Policy") != NULL &&
                    fake_reply_header(reply, "Cache-Control") != NULL;

    printf("RECORD %s %s %d %s %d %s %s\n", method_name(method), template_of(url), reply->status,
           retry != NULL ? retry : "-", security ? 1 : 0, type != NULL ? type : "-", reply->body);
    return reply;
}

static const fake_reply_t *get(const session_jar_t *jar, const char *url)
{
    return show(CRUX_WEB_GET, url, mgmt_get(jar, url));
}

static const fake_reply_t *put(const session_jar_t *jar, const char *url, const char *body)
{
    return show(CRUX_WEB_PUT, url, mgmt_put(jar, url, body));
}

static const fake_reply_t *post(const session_jar_t *jar, const char *url, const char *body)
{
    return show(CRUX_WEB_POST, url, mgmt_post(jar, url, body));
}

static void answers(void)
{
    static char big[CRUX_WEB_BODY_BUFFER + 8u];
    const session_jar_t *both;
    const session_jar_t *reader;
    const session_jar_t *writer;
    const session_jar_t *admin;

    mgmt_setup();
    both = mgmt_jar("both");
    reader = mgmt_jar("reader");
    writer = mgmt_jar("writer");
    admin = mgmt_jar("admin");
    /* Status. */
    (void)get(admin, "/api/v1/status");
    (void)get(mgmt_jar("stranger"), "/api/v1/status");
    (void)show(CRUX_WEB_GET, "/api/v1/status",
               mgmt_anonymous(CRUX_WEB_GET, "/api/v1/status", NULL));
    /* The list, for each kind of session, and without one. */
    (void)get(both, "/api/v1/config");
    (void)get(reader, "/api/v1/config");
    (void)get(writer, "/api/v1/config");
    (void)get(mgmt_jar("stranger"), "/api/v1/config");
    (void)show(CRUX_WEB_GET, "/api/v1/config",
               mgmt_anonymous(CRUX_WEB_GET, "/api/v1/config", NULL));
    /* One parameter. */
    (void)get(both, "/api/v1/config/mode");
    (void)get(both, "/api/v1/config/limit");
    (void)get(both, "/api/v1/config/host_name");
    (void)get(writer, "/api/v1/config/mode");
    (void)get(both, "/api/v1/config/link_secret");
    (void)get(both, "/api/v1/config/private_key");
    (void)get(both, "/api/v1/config/nothing_here");
    (void)show(CRUX_WEB_GET, "/api/v1/config/mode",
               mgmt_anonymous(CRUX_WEB_GET, "/api/v1/config/mode", NULL));
    /* An update, in each way that it ends. */
    (void)put(both, "/api/v1/config/mode", "{\"value\":true}");
    (void)put(both, "/api/v1/config/limit", "{\"value\":7}");
    (void)put(both, "/api/v1/config/host_name", "{\"value\":\"Pump \\\"4\\\"\"}");
    (void)put(admin, "/api/v1/config/link_secret", "{\"value\":\"a secret\"}");
    (void)put(both, "/api/v1/config/limit", "{");
    (void)put(both, "/api/v1/config/limit", "{\"value\":null}");
    (void)put(both, "/api/v1/config/limit", "{\"value\":\"7\"}");
    (void)put(both, "/api/v1/config/limit", "{\"value\":-7}");
    (void)put(both, "/api/v1/config/limit", "{\"value\":0}");
    (void)put(both, "/api/v1/config/host_name", "{\"value\":\"a bad one\"}");
    (void)put(reader, "/api/v1/config/mode", "{\"value\":true}");
    (void)put(both, "/api/v1/config/fixed", "{\"value\":8}");
    (void)put(both, "/api/v1/config/private_key", "{\"value\":8}");
    (void)show(CRUX_WEB_PUT, "/api/v1/config/mode",
               mgmt_anonymous(CRUX_WEB_PUT, "/api/v1/config/mode", "{\"value\":true}"));
    memset(big, ' ', sizeof(big) - 1u);
    (void)put(both, "/api/v1/config/mode", big);
    /* A save, refused and accepted, and what the list then says. */
    (void)post(reader, "/api/v1/config/save", NULL);
    (void)show(CRUX_WEB_POST, "/api/v1/config/save",
               mgmt_anonymous(CRUX_WEB_POST, "/api/v1/config/save", NULL));
    (void)post(admin, "/api/v1/config/save", "{}");
    (void)post(admin, "/api/v1/config/save", NULL);
    (void)post(admin, "/api/v1/config/save", NULL);
    (void)get(admin, "/api/v1/config");
    CHECK(web_mgmt_work_once());
    (void)get(admin, "/api/v1/config");
    (void)get(mgmt_jar("saver"), "/api/v1/config");
}

static void failed_storage(void)
{
    const session_jar_t *admin;

    mgmt_setup();
    admin = mgmt_jar("admin");
    (void)put(admin, "/api/v1/config/limit", "{\"value\":7}");
    config_fault_write_fail_key(NAME_LIMIT, CRUX_CONFIG_ERR_NO_SPACE);
    (void)post(admin, "/api/v1/config/save", NULL);
    CHECK(web_mgmt_work_once());
    (void)get(admin, "/api/v1/config");
}

static void failed_not_supported(void)
{
    const session_jar_t *admin;

    mgmt_setup();
    admin = mgmt_jar("admin");
    (void)put(admin, "/api/v1/config/limit", "{\"value\":7}");
    config_fault_write_fail_key(NAME_LIMIT, CRUX_CONFIG_ERR_NOT_SUPPORTED);
    (void)post(admin, "/api/v1/config/save", NULL);
    CHECK(web_mgmt_work_once());
    (void)get(admin, "/api/v1/config");
}

static void failed_unauthorized(void)
{
    const session_jar_t *saver;
    const session_jar_t *admin;

    mgmt_setup();
    saver = mgmt_jar("saver");
    admin = mgmt_jar("admin");
    (void)post(saver, "/api/v1/config/save", NULL);
    CHECK(mgmt_delete(saver, "/api/v1/session")->status == 200);
    CHECK(web_mgmt_work_once());
    (void)get(admin, "/api/v1/config");
}

static void failed_busy(void)
{
    const session_jar_t *admin;

    mgmt_setup();
    admin = mgmt_jar("admin");
    (void)post(admin, "/api/v1/config/save", NULL);
    mgmt_hold_service();
    CHECK(web_mgmt_work_once());
    /* While the application holds the service, a list, a read and an update are busy. */
    (void)get(admin, "/api/v1/config");
    (void)get(admin, "/api/v1/config/mode");
    (void)put(admin, "/api/v1/config/mode", "{\"value\":true}");
    mgmt_release_service();
    (void)get(admin, "/api/v1/config");
}

static void not_ready(void)
{
    const mgmt_options_t options = {.no_status = true, .no_config = true, .no_management = true};
    const session_jar_t *admin;

    mgmt_setup_with(&options);
    admin = mgmt_jar("admin");
    (void)get(admin, "/api/v1/status");
    (void)get(admin, "/api/v1/config");
    (void)get(admin, "/api/v1/config/mode");
    (void)put(admin, "/api/v1/config/mode", "{\"value\":true}");
    (void)post(admin, "/api/v1/config/save", NULL);
}

int main(void)
{
    mgmt_run_case("answers", answers);
    mgmt_run_case("a failed save", failed_storage);
    mgmt_run_case("no provider", failed_not_supported);
    mgmt_run_case("a session that ended", failed_unauthorized);
    mgmt_run_case("a busy service", failed_busy);
    mgmt_run_case("services not ready", not_ready);
    return mgmt_finish("web_management_openapi_fixture");
}
