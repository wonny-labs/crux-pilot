#include "service_test.h"

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

crux_access_session_t s_admin, s_reader, s_writer, s_both, s_saver, s_stranger;
const crux_access_session_t s_never_valid = {0};

static unsigned cases_run, cases_failed;

void run_case(const char *name, void (*body)(void))
{
    fflush(NULL);
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        exit(2);
    }
    if (child == 0) {
        body();
        fflush(NULL);
        _exit(access_test_failures == 0 ? 0 : 1);
    }
    int status = 0;
    waitpid(child, &status, 0);
    ++cases_run;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("CASE FAILED: %s (status %d)\n", name, status);
        ++cases_failed;
    }
}

int suite_finish(const char *suite)
{
    printf("%s: %u cases, %u failed\n", suite, cases_run, cases_failed);
    return cases_failed == 0 ? 0 : 1;
}

static const char *const admin_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE, "cfg.read",
                                                "cfg.write", CRUX_CONFIG_SERVICE_PERMISSION_SAVE};
static const char *const reader_permissions[] = {"cfg.read"};
static const char *const writer_permissions[] = {"cfg.write"};
static const char *const both_permissions[] = {"cfg.read", "cfg.write"};
static const char *const saver_permissions[] = {CRUX_CONFIG_SERVICE_PERMISSION_SAVE};
static const char *const stranger_permissions[] = {"other.thing"};
static const char *const read_saver_permissions[] = {"cfg.read",
                                                     CRUX_CONFIG_SERVICE_PERMISSION_SAVE};
static const char *const write_saver_permissions[] = {"cfg.write",
                                                      CRUX_CONFIG_SERVICE_PERMISSION_SAVE};
static const crux_access_role_t roles[] = {
    {"administrator", admin_permissions, 4},   {"reader", reader_permissions, 1},
    {"writer", writer_permissions, 1},         {"both", both_permissions, 2},
    {"saver", saver_permissions, 1},           {"stranger", stranger_permissions, 1},
    {"read_saver", read_saver_permissions, 2}, {"write_saver", write_saver_permissions, 2},
};
static const crux_access_policy_t policy = {roles, 8, 15u * 60u, 8u * 3600u, NULL};

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
static const crux_config_spec_t spec_middle = {NAME_MIDDLE, 1, U32_VALUE(3), NULL, NULL};
static const crux_config_spec_t spec_wide = {NAME_WIDE, 1, U32_VALUE(0), NULL, NULL};
static const crux_config_spec_t spec_host = {NAME_HOST, 1, STRING_VALUE("crux"), host_rule, NULL};
static const crux_config_spec_t spec_small = {NAME_SMALL, 1, STRING_VALUE(""), NULL, NULL};
static const crux_config_spec_t spec_secret = {NAME_SECRET, 1, STRING_VALUE("unset"), NULL, NULL};
static const crux_config_spec_t spec_fixed = {NAME_FIXED, 1, U32_VALUE(7), NULL, NULL};
static const crux_config_spec_t spec_private = {NAME_PRIVATE, 1, U32_VALUE(9), NULL, NULL};

crux_config_param_t param_mode, param_limit, param_middle, param_wide, param_host, param_small,
    param_secret, param_fixed, param_private;
static char storage_host[CRUX_CONFIG_SERVICE_TEXT_SIZE], storage_small[8],
    storage_secret[CRUX_CONFIG_SERVICE_TEXT_SIZE];

const crux_config_service_entry_t test_table[TABLE_ENTRIES] = {
    {&param_mode, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_limit, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_middle, "no.role.holds.this", NULL, false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_wide, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_host, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_small, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_secret, "cfg.read", "cfg.write", true, CRUX_CONFIG_ACTIVATION_RESTART},
    {&param_fixed, "cfg.read", NULL, false, CRUX_CONFIG_ACTIVATION_SAFE_STATE},
    {&param_private, "no.role.holds.this", "no.role.holds.this", false,
     CRUX_CONFIG_ACTIVATION_APPLICATION},
};

crux_access_session_t fixture_login(const char *user)
{
    return access_test_session(user, ACCESS_TEST_PASSWORD, &access_test_web);
}

static void create(crux_access_session_t admin, const char *user, const char *role)
{
    CHECK(crux_access_account_create(admin, user, role, ACCESS_TEST_PASSWORD,
                                     strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
}

void fixture_boot(void)
{
    access_test_boot(&policy);
    CHECK(crux_access_provision_first_account(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD,
                                              strlen(ACCESS_TEST_PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    s_admin = fixture_login(ACCESS_TEST_ADMIN);
    create(s_admin, "reader", "reader");
    create(s_admin, "writer", "writer");
    create(s_admin, "both", "both");
    create(s_admin, "saver", "saver");
    create(s_admin, "stranger", "stranger");
    s_reader = fixture_login("reader");
    s_writer = fixture_login("writer");
    s_both = fixture_login("both");
    s_saver = fixture_login("saver");
    s_stranger = fixture_login("stranger");

    config_fault_reset();
    CHECK(crux_config_init(&param_mode, &spec_mode) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&param_limit, &spec_limit) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&param_middle, &spec_middle) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&param_wide, &spec_wide) == CRUX_CONFIG_OK);
    CHECK(crux_config_init_string(&param_host, &spec_host, storage_host, sizeof(storage_host)) ==
          CRUX_CONFIG_OK);
    CHECK(crux_config_init_string(&param_small, &spec_small, storage_small,
                                  sizeof(storage_small)) == CRUX_CONFIG_OK);
    CHECK(crux_config_init_string(&param_secret, &spec_secret, storage_secret,
                                  sizeof(storage_secret)) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&param_fixed, &spec_fixed) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&param_private, &spec_private) == CRUX_CONFIG_OK);

    access_fake_events_clear();
    config_events_clear();
}

void fixture_start(void)
{
    fixture_boot();
    CHECK(crux_config_service_start(test_table, TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
}

size_t denials(void)
{
    return access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test helper
void expect_text(crux_access_session_t session, const char *name, const char *expected)
{
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE] = {0};
    size_t length = 0;
    crux_config_type_t type = CRUX_CONFIG_BOOL;
    CHECK(crux_config_service_read(session, name, text, sizeof(text), &length, &type) ==
          CRUX_CONFIG_SERVICE_OK);
    CHECK(length == strlen(expected));
    CHECK(strcmp(text, expected) == 0);
}

void expect_u32(const char *name, uint32_t expected)
{
    crux_config_value_t value = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_service_try_read(name, &value, NULL, 0) == CRUX_CONFIG_SERVICE_OK);
    CHECK(value.type == CRUX_CONFIG_U32 && value.data.u32 == expected);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test helper
void expect_string(const char *name, const char *expected)
{
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE] = {0};
    crux_config_value_t value = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_service_try_read(name, &value, text, sizeof(text)) == CRUX_CONFIG_SERVICE_OK);
    CHECK(value.type == CRUX_CONFIG_STRING && value.data.string.text == text);
    CHECK(strcmp(text, expected) == 0);
}

crux_config_service_result_t update_text(crux_access_session_t session, const char *name,
                                         const char *text)
{
    crux_config_service_item_t applied;
    return crux_config_service_update(session, name, text, strlen(text), &applied);
}
