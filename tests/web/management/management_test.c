/* See management_test.h. */
#include "management_test.h"

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

crux_config_param_t param_mode, param_limit, param_host, param_small, param_secret, param_fixed,
    param_private;

static unsigned cases_run, cases_failed;

void mgmt_run_case(const char *name, void (*body)(void))
{
    pid_t child;
    int status = 0;

    fflush(NULL);
    child = fork();
    if (child < 0) {
        perror("fork");
        exit(2);
    }
    if (child == 0) {
        body();
        fflush(NULL);
        _exit(access_test_failures == 0 ? 0 : 1);
    }
    waitpid(child, &status, 0);
    ++cases_run;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("CASE FAILED: %s (status %d)\n", name, status);
        ++cases_failed;
    }
}

int mgmt_finish(const char *suite)
{
    printf("%s: %u cases, %u failed\n", suite, cases_run, cases_failed);
    return cases_failed == 0 ? 0 : 1;
}

static const char *const admin_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE,
                                                CRUX_MGMT_STATUS_PERMISSION_READ,
                                                "app.read",
                                                "cfg.read",
                                                "cfg.write",
                                                CRUX_CONFIG_SERVICE_PERMISSION_SAVE};
static const char *const reader_permissions[] = {CRUX_MGMT_STATUS_PERMISSION_READ, "cfg.read"};
static const char *const writer_permissions[] = {"cfg.write"};
static const char *const both_permissions[] = {"cfg.read", "cfg.write"};
static const char *const saver_permissions[] = {CRUX_CONFIG_SERVICE_PERMISSION_SAVE};
static const char *const app_permissions[] = {"app.read"};
static const char *const stranger_permissions[] = {"other.thing"};
static const crux_access_role_t roles[] = {
    {"administrator", admin_permissions, 6}, {"reader", reader_permissions, 2},
    {"writer", writer_permissions, 1},       {"both", both_permissions, 2},
    {"saver", saver_permissions, 1},         {"app", app_permissions, 1},
    {"stranger", stranger_permissions, 1},
};
static const crux_access_policy_t policy = {roles, 7, 15u * 60u, 8u * 3600u, NULL};

static bool limit_range(const crux_config_value_t *value, void *context)
{
    (void)context;
    return value->type == CRUX_CONFIG_U32 && value->data.u32 >= 1 && value->data.u32 <= 100;
}

static bool host_rule(const crux_config_value_t *value, void *context)
{
    (void)context;
    return value->type == CRUX_CONFIG_STRING && strstr(value->data.string.text, "bad") == NULL;
}

#define BOOL_VALUE(v) {.type = CRUX_CONFIG_BOOL, .data.boolean = (v)}
#define U32_VALUE(v) {.type = CRUX_CONFIG_U32, .data.u32 = (v)}
#define STRING_VALUE(v)                                                                            \
    {                                                                                              \
        .type = CRUX_CONFIG_STRING, .data.string = {.text = (v), .length = sizeof(v) - 1u }        \
    }

static const crux_config_spec_t spec_mode = {NAME_MODE, 1, BOOL_VALUE(false), NULL, NULL};
static const crux_config_spec_t spec_limit = {NAME_LIMIT, 1, U32_VALUE(5), limit_range, NULL};
static const crux_config_spec_t spec_host = {NAME_HOST, 1, STRING_VALUE("crux"), host_rule, NULL};
static const crux_config_spec_t spec_small = {NAME_SMALL, 1, STRING_VALUE(""), NULL, NULL};
static const crux_config_spec_t spec_secret = {NAME_SECRET, 1, STRING_VALUE("unset"), NULL, NULL};
static const crux_config_spec_t spec_fixed = {NAME_FIXED, 1, U32_VALUE(7), NULL, NULL};
static const crux_config_spec_t spec_private = {NAME_PRIVATE, 1, U32_VALUE(9), NULL, NULL};

static char storage_host[CRUX_CONFIG_SERVICE_TEXT_SIZE], storage_small[8],
    storage_secret[CRUX_CONFIG_SERVICE_TEXT_SIZE];

const crux_config_service_entry_t mgmt_table[MGMT_TABLE_ENTRIES] = {
    {&param_mode, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_limit, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_host, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_RESTART},
    {&param_small, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_secret, "cfg.read", "cfg.write", true, CRUX_CONFIG_ACTIVATION_RESTART},
    {&param_fixed, "cfg.read", NULL, false, CRUX_CONFIG_ACTIVATION_SAFE_STATE},
    {&param_private, "no.role.holds.this", "no.role.holds.this", false,
     CRUX_CONFIG_ACTIVATION_APPLICATION},
};

static crux_status_t read_cycles(void *context, crux_mgmt_status_value_t *out)
{
    (void)context;
    out->type = CRUX_MGMT_STATUS_U32;
    out->data.u32 = 42u;
    return CRUX_OK;
}

static const crux_mgmt_status_item_t project_items[] = {
    {"app.cycle_count", "app.read", read_cycles, NULL},
};

static crux_mgmt_status_config_t default_status_config(void)
{
    crux_mgmt_status_config_t config = {
        .platform_version = "1.2.3",
        .project_version = "pilot-7",
        .boot_reset_cause_status = CRUX_OK,
        .boot_reset_cause = {.supported = CRUX_RESET_CAUSE_POR | CRUX_RESET_CAUSE_WATCHDOG,
                             .causes = CRUX_RESET_CAUSE_POR},
        .project_items = project_items,
        .project_item_count = 1u,
    };
    return config;
}

static crux_access_session_t direct[10];
static char direct_names[10][16];
static size_t direct_count;
static session_jar_t jars[10];
static char jar_names[10][16];
static size_t jar_count;

static void create(crux_access_session_t admin, const char *user, const char *role)
{
    CHECK(crux_access_account_create(admin, user, role, MGMT_PASSWORD, strlen(MGMT_PASSWORD)) ==
          CRUX_ACCESS_OK);
}

void mgmt_setup_with(const mgmt_options_t *options)
{
    crux_access_session_t admin;

    direct_count = 0u;
    jar_count = 0u;
    access_test_boot(&policy);
    CHECK(crux_access_provision_first_account(ACCESS_TEST_ADMIN, MGMT_PASSWORD,
                                              strlen(MGMT_PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    admin = access_test_session(ACCESS_TEST_ADMIN, MGMT_PASSWORD, &access_test_web);
    create(admin, "reader", "reader");
    create(admin, "writer", "writer");
    create(admin, "both", "both");
    create(admin, "saver", "saver");
    create(admin, "app", "app");
    create(admin, "stranger", "stranger");
    create(admin, "forced", "both");
    CHECK(crux_access_account_reset_password(admin, "forced", MGMT_PASSWORD,
                                             strlen(MGMT_PASSWORD)) == CRUX_ACCESS_OK);
    /* Access holds eight sessions, and a case may log in as many accounts, so this one ends here.
     */
    crux_access_logout(&admin);

    config_fault_reset();
    CHECK(crux_config_init(&param_mode, &spec_mode) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&param_limit, &spec_limit) == CRUX_CONFIG_OK);
    CHECK(crux_config_init_string(&param_host, &spec_host, storage_host, sizeof(storage_host)) ==
          CRUX_CONFIG_OK);
    CHECK(crux_config_init_string(&param_small, &spec_small, storage_small,
                                  sizeof(storage_small)) == CRUX_CONFIG_OK);
    CHECK(crux_config_init_string(&param_secret, &spec_secret, storage_secret,
                                  sizeof(storage_secret)) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&param_fixed, &spec_fixed) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&param_private, &spec_private) == CRUX_CONFIG_OK);
    status_fake_reset();
    if (options->prepare != NULL) {
        options->prepare();
    }
    if (!options->no_config) {
        CHECK(crux_config_service_start(options->table != NULL ? options->table : mgmt_table,
                                        options->table != NULL
                                            ? options->table_count
                                            : MGMT_TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    }
    if (!options->no_status) {
        crux_mgmt_status_config_t config =
            options->status_config != NULL ? *options->status_config : default_status_config();

        CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    }
    session_test_setup_web();
    if (!options->no_management) {
        CHECK(crux_web_management_init() == CRUX_WEB_OK);
    }
    access_fake_events_clear();
    config_events_clear();
}

void mgmt_setup(void)
{
    const mgmt_options_t options = {0};

    mgmt_setup_with(&options);
}

const session_jar_t *mgmt_jar(const char *user)
{
    for (size_t index = 0; index < jar_count; ++index) {
        if (strcmp(jar_names[index], user) == 0) {
            return &jars[index];
        }
    }
    CHECK(jar_count < 10u);
    if (jar_count >= 10u) {
        return &jars[0];
    }
    snprintf(jar_names[jar_count], sizeof(jar_names[jar_count]), "%s", user);
    CHECK(session_login(user, MGMT_PASSWORD, &jars[jar_count])->status == 200);
    CHECK(jars[jar_count].session[0] != '\0');
    return &jars[jar_count++];
}

crux_access_session_t mgmt_session(const char *user)
{
    for (size_t index = 0; index < direct_count; ++index) {
        if (strcmp(direct_names[index], user) == 0) {
            return direct[index];
        }
    }
    CHECK(direct_count < 10u);
    snprintf(direct_names[direct_count], sizeof(direct_names[direct_count]), "%s", user);
    direct[direct_count] = access_test_session(user, MGMT_PASSWORD, &access_test_web_other);
    return direct[direct_count++];
}

const fake_reply_t *mgmt_get(const session_jar_t *jar, const char *url)
{
    return session_with(jar, CRUX_WEB_GET, url, NULL);
}

const fake_reply_t *mgmt_put(const session_jar_t *jar, const char *url, const char *body)
{
    return session_with(jar, CRUX_WEB_PUT, url, body);
}

const fake_reply_t *mgmt_post(const session_jar_t *jar, const char *url, const char *body)
{
    return session_with(jar, CRUX_WEB_POST, url, body);
}

const fake_reply_t *mgmt_delete(const session_jar_t *jar, const char *url)
{
    return session_with(jar, CRUX_WEB_DELETE, url, NULL);
}

const fake_reply_t *mgmt_anonymous(unsigned method, const char *url, const char *body)
{
    session_request_t request = {
        .method = method, .url = url, .host = SESSION_TEST_HOST, .body = body};

    if (method != CRUX_WEB_GET) {
        request.origin = SESSION_TEST_ORIGIN;
        request.content_type = body != NULL ? "application/json" : NULL;
    }
    return session_send(&request);
}

const fake_reply_t *mgmt_vary(const session_jar_t *jar, unsigned method, const char *url,
                              const char *body, mgmt_variation_t variation)
{
    session_request_t request = {.method = method,
                                 .url = url,
                                 .cookie = jar->header,
                                 .host = SESSION_TEST_HOST,
                                 .body = body};

    if (method != CRUX_WEB_GET) {
        request.origin = variation.no_origin ? NULL : SESSION_TEST_ORIGIN;
        request.xsrf = variation.no_token ? NULL : jar->xsrf;
        if (body != NULL) {
            request.content_type = variation.content_type == NULL      ? "application/json"
                                   : variation.content_type[0] == '\0' ? NULL
                                                                       : variation.content_type;
        }
    }
    return session_send(&request);
}

void mgmt_expect_body(const fake_reply_t *reply, int status, const char *body)
{
    CHECK(reply->origin == FAKE_FROM_CORE);
    CHECK(reply->status == status);
    if (strcmp(reply->body, body) != 0 || reply->status != status) {
        printf("  expected %d %s\n  got      %d %s\n", status, body, reply->status, reply->body);
    }
    CHECK(strcmp(reply->body, body) == 0);
}

void mgmt_expect_error(const fake_reply_t *reply, int status, const char *code)
{
    char expected[64];

    snprintf(expected, sizeof(expected), "{\"code\":\"%s\"}", code);
    mgmt_expect_body(reply, status, expected);
}

void mgmt_expect_plain(const fake_reply_t *reply, int status, const char *text)
{
    CHECK(reply->origin == FAKE_FROM_SERVER);
    CHECK(reply->status == status);
    CHECK(strcmp(reply->body, text) == 0);
}

void mgmt_expect_headers(const fake_reply_t *reply, const char *content_type)
{
    const char *value = fake_reply_header(reply, "Content-Type");

    CHECK(value != NULL && strcmp(value, content_type) == 0);
    value = fake_reply_header(reply, "Cache-Control");
    CHECK(value != NULL && strcmp(value, "no-store") == 0);
    value = fake_reply_header(reply, "X-Content-Type-Options");
    CHECK(value != NULL && strcmp(value, "nosniff") == 0);
    value = fake_reply_header(reply, "Referrer-Policy");
    CHECK(value != NULL && strcmp(value, "no-referrer") == 0);
    value = fake_reply_header(reply, "Content-Security-Policy");
    CHECK(value != NULL && strstr(value, "default-src 'self'") != NULL);
    CHECK(fake_reply_header(reply, "Access-Control-Allow-Origin") == NULL);
}

void mgmt_expect_retry_after(const fake_reply_t *reply)
{
    const char *value = fake_reply_header(reply, "Retry-After");

    CHECK(value != NULL && value[0] >= '1' && value[0] <= '9');
    CHECK(value == NULL || strspn(value, "0123456789") == strlen(value));
}

void mgmt_expect_u32(const char *name, uint32_t expected)
{
    crux_config_value_t value = {.type = CRUX_CONFIG_BOOL};

    CHECK(crux_config_service_try_read(name, &value, NULL, 0) == CRUX_CONFIG_SERVICE_OK);
    CHECK(value.type == CRUX_CONFIG_U32 && value.data.u32 == expected);
}

void mgmt_expect_bool(const char *name, bool expected)
{
    crux_config_value_t value = {.type = CRUX_CONFIG_U32};

    CHECK(crux_config_service_try_read(name, &value, NULL, 0) == CRUX_CONFIG_SERVICE_OK);
    CHECK(value.type == CRUX_CONFIG_BOOL && value.data.boolean == expected);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test helper
void mgmt_expect_string(const char *name, const char *expected)
{
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE] = {0};
    crux_config_value_t value = {.type = CRUX_CONFIG_BOOL};

    CHECK(crux_config_service_try_read(name, &value, text, sizeof(text)) == CRUX_CONFIG_SERVICE_OK);
    CHECK(value.type == CRUX_CONFIG_STRING);
    if (strcmp(text, expected) != 0) {
        printf("  %s is \"%s\", expected \"%s\"\n", name, text, expected);
    }
    CHECK(strcmp(text, expected) == 0);
}

size_t mgmt_unsaved(void)
{
    /* Through a session that may write everything: the admin's listing marks each unsaved one. */
    crux_access_session_t session = mgmt_session("both");
    crux_config_service_item_t item;
    size_t cursor = 0u;
    size_t count = 0u;

    while (crux_config_service_next(session, &cursor, &item) == CRUX_CONFIG_SERVICE_OK) {
        count += item.unsaved ? 1u : 0u;
    }
    return count;
}

/* Holds the configuration service busy from another thread, for as long as a case wants. */
static pthread_t holder;
static pthread_mutex_t hold_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t hold_changed = PTHREAD_COND_INITIALIZER;
static bool hold_entered;
static bool hold_released;

static void wait_inside_the_write(void *context)
{
    (void)context;
    config_fault_write_hook(NULL, NULL);
    pthread_mutex_lock(&hold_lock);
    hold_entered = true;
    pthread_cond_broadcast(&hold_changed);
    while (!hold_released) {
        pthread_cond_wait(&hold_changed, &hold_lock);
    }
    pthread_mutex_unlock(&hold_lock);
}

static void *hold_main(void *argument)
{
    crux_config_service_save_report_t report;

    (void)argument;
    (void)crux_config_service_save_local(&report);
    return NULL;
}

void mgmt_hold_service(void)
{
    crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 11u};

    hold_entered = false;
    hold_released = false;
    /* A save with nothing marked would never write, so the application marks a private parameter.
     */
    CHECK(crux_config_service_try_update(NAME_PRIVATE, &value) == CRUX_CONFIG_SERVICE_OK);
    config_fault_write_hook(wait_inside_the_write, NULL);
    CHECK(pthread_create(&holder, NULL, hold_main, NULL) == 0);
    pthread_mutex_lock(&hold_lock);
    while (!hold_entered) {
        pthread_cond_wait(&hold_changed, &hold_lock);
    }
    pthread_mutex_unlock(&hold_lock);
}

void mgmt_release_service(void)
{
    pthread_mutex_lock(&hold_lock);
    hold_released = true;
    pthread_cond_broadcast(&hold_changed);
    pthread_mutex_unlock(&hold_lock);
    CHECK(pthread_join(holder, NULL) == 0);
}
