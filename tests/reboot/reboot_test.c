/* See reboot_test.h. */
#include "reboot_test.h"

#include <sys/wait.h>
#include <unistd.h>

crux_access_session_t s_admin, s_rebooter, s_reader, s_stranger, s_forced;
const crux_access_session_t s_never_valid = {0};

/* In fake_port.c. */
void reboot_fakes_reset(void);
bool reboot_source_probe(void *context);

static unsigned cases_run, cases_failed;

void reboot_run_case(const char *name, void (*body)(void))
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

int reboot_finish(const char *suite)
{
    printf("%s: %u cases, %u failed\n", suite, cases_run, cases_failed);
    return cases_failed == 0 ? 0 : 1;
}

static const char *const admin_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE,
                                                CRUX_REBOOT_PERMISSION, "network.read",
                                                "network.write", "config.save"};
static const char *const rebooter_permissions[] = {CRUX_REBOOT_PERMISSION};
static const char *const reader_permissions[] = {"network.read"};
static const char *const stranger_permissions[] = {"other.thing"};
static const crux_access_role_t roles[] = {
    {"administrator", admin_permissions, 5},
    {"rebooter", rebooter_permissions, 1},
    {"reader", reader_permissions, 1},
    {"stranger", stranger_permissions, 1},
};
static const crux_access_policy_t policy = {roles, 4, 15u * 60u, 8u * 3600u, NULL};

static void create(crux_access_session_t admin, const char *user, const char *role)
{
    CHECK(crux_access_account_create(admin, user, role, ACCESS_TEST_PASSWORD,
                                     strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
}

void reboot_boot(void)
{
    access_test_boot(&policy);
    CHECK(crux_access_provision_first_account(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD,
                                              strlen(ACCESS_TEST_PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    s_admin = access_test_session(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD, &access_test_web);
    create(s_admin, "rebooter", "rebooter");
    create(s_admin, "reader", "reader");
    create(s_admin, "stranger", "stranger");
    create(s_admin, "forced", "rebooter");
    CHECK(crux_access_account_reset_password(s_admin, "forced", ACCESS_TEST_PASSWORD,
                                             strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    s_rebooter = access_test_session("rebooter", ACCESS_TEST_PASSWORD, &access_test_web);
    s_reader = access_test_session("reader", ACCESS_TEST_PASSWORD, &access_test_web);
    s_stranger = access_test_session("stranger", ACCESS_TEST_PASSWORD, &access_test_web);
    s_forced = access_test_session("forced", ACCESS_TEST_PASSWORD, &access_test_web);
    reboot_fakes_reset();
    access_fake_events_clear();
}

crux_reboot_config_t reboot_config(void)
{
    static crux_reboot_wait_source_t sources[2];
    static size_t indexes[2] = {0u, 1u};

    sources[0] = (crux_reboot_wait_source_t){reboot_source_probe, &indexes[0], true};
    sources[1] = (crux_reboot_wait_source_t){reboot_source_probe, &indexes[1], true};
    return (crux_reboot_config_t){CRUX_REBOOT_POLICY_CALLBACK, reboot_policy_callback, NULL,
                                  sources, 2u};
}

void reboot_start(void)
{
    crux_reboot_config_t config = reboot_config();

    reboot_boot();
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
}

size_t reboot_denials(void)
{
    return access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED);
}

crux_reboot_state_t reboot_state_of(crux_access_session_t session)
{
    crux_reboot_state_t state;

    memset(&state, 0xA5, sizeof(state));
    CHECK(crux_reboot_get(session, &state) == CRUX_REBOOT_OK);
    return state;
}
