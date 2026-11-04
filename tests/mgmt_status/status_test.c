#include "status_test.h"

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

crux_access_session_t s_admin, s_viewer, s_app, s_none;
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

static const char *const admin_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE,
                                                CRUX_MGMT_STATUS_PERMISSION_READ, "app.read"};
static const char *const viewer_permissions[] = {CRUX_MGMT_STATUS_PERMISSION_READ};
static const char *const app_permissions[] = {"app.read"};
static const char *const none_permissions[] = {"other.thing"};
static const crux_access_role_t roles[] = {
    {"administrator", admin_permissions, 3},
    {"viewer", viewer_permissions, 1},
    {"app", app_permissions, 1},
    {"none", none_permissions, 1},
};
static const crux_access_policy_t policy = {roles, 4, 15u * 60u, 8u * 3600u, NULL};

static crux_access_session_t login(const char *user)
{
    return access_test_session(user, ACCESS_TEST_PASSWORD, &access_test_web);
}

void fixture_boot(void)
{
    access_test_boot(&policy);
    CHECK(crux_access_provision_first_account(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD,
                                              strlen(ACCESS_TEST_PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    s_admin = login(ACCESS_TEST_ADMIN);
    const size_t length = strlen(ACCESS_TEST_PASSWORD);
    CHECK(crux_access_account_create(s_admin, "viewer", "viewer", ACCESS_TEST_PASSWORD, length) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(s_admin, "app", "app", ACCESS_TEST_PASSWORD, length) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(s_admin, "none", "none", ACCESS_TEST_PASSWORD, length) ==
          CRUX_ACCESS_OK);
    s_viewer = login("viewer");
    s_app = login("app");
    s_none = login("none");
    access_fake_events_clear();
    status_fake_reset();
}

crux_mgmt_status_config_t fixture_config(void)
{
    crux_mgmt_status_config_t config = {
        .platform_version = "1.2.3",
        .project_version = "pilot-7",
        .boot_reset_cause_status = CRUX_OK,
        .boot_reset_cause = {.supported = CRUX_RESET_CAUSE_POR | CRUX_RESET_CAUSE_WATCHDOG |
                                          CRUX_RESET_CAUSE_PIN,
                             .causes = CRUX_RESET_CAUSE_POR | CRUX_RESET_CAUSE_WATCHDOG},
    };
    return config;
}

void fixture_start(void)
{
    fixture_boot();
    crux_mgmt_status_config_t config = fixture_config();
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
}

bool listed(crux_access_session_t session, const char *name)
{
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    while (crux_mgmt_status_next(session, &cursor, &entry) == CRUX_OK) {
        if (strcmp(entry.name, name) == 0) {
            return true;
        }
    }
    return false;
}

crux_mgmt_status_entry_t entry_named(crux_access_session_t session, const char *name)
{
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    while (crux_mgmt_status_next(session, &cursor, &entry) == CRUX_OK) {
        if (strcmp(entry.name, name) == 0) {
            return entry;
        }
    }
    printf("not listed: %s\n", name);
    CHECK(!"the item is listed");
    memset(&entry, 0, sizeof(entry));
    entry.name = "";
    entry.status = CRUX_ERR_IO;
    return entry;
}

void expect_text_item(crux_access_session_t session, const char *name, const char *expected)
{
    crux_mgmt_status_entry_t entry = entry_named(session, name);
    CHECK(entry.status == CRUX_OK);
    CHECK(entry.value.type == CRUX_MGMT_STATUS_TEXT);
    if (strcmp(entry.value.data.text, expected) != 0) {
        printf("%s: \"%s\", expected \"%s\"\n", name, entry.value.data.text, expected);
    }
    CHECK(strcmp(entry.value.data.text, expected) == 0);
}

bool value_is_zero(const crux_mgmt_status_value_t *value)
{
    if ((int)value->type != 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(value->data.text); ++i) {
        if (value->data.text[i] != '\0') {
            return false;
        }
    }
    return true;
}

void expect_failed_item(crux_access_session_t session, const char *name, crux_status_t status)
{
    crux_mgmt_status_entry_t entry = entry_named(session, name);
    if (entry.status != status) {
        printf("%s: status %d, expected %d\n", name, entry.status, status);
    }
    CHECK(entry.status == status);
    /* A failed entry holds nothing that an adapter could show by mistake. */
    CHECK(value_is_zero(&entry.value));
}
