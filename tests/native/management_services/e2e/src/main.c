/*
 * The services image with a built web example packaged in it, for the browser cases of Issue #220.
 * It starts the composition of Issue #242 with this image's profile: the accounts, roles,
 * parameters and status items that the shared browser cases were written for against the mock, so
 * that the same cases run here on the real services. The services, their PSA providers, the routes
 * and the HTTPS listener are those of the services image, unchanged.
 *
 * Test only, as the composition is: the entropy is test-only, the listener's certificate key is
 * public, and the accounts are a declared test step with a test password that is public in the
 * repository. The idle limit is shortened for the test, see CMakeLists.txt.
 */
#include "crux_native_management_services.h"

#include <crux/access.h>
#include <crux/cli.h>
#include <crux/config_service.h>
#include <crux/configuration.h>
#include <crux/management_status.h>
#include <crux_access_adapter.h>
#include <crux_cli_adapter.h>

#include <string.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* The mock's test password, which the shared cases type. Public in the repository. */
#define PASSWORD "example-password-not-real"
#define OWNER "admin"
#define OWNER_PASSWORD "e2e owner password, test only"

/* The mock's permissions, by its account table in web/mock/server.ts. */
static const char *const administrator_permissions[] = {CRUX_MGMT_STATUS_PERMISSION_READ,
                                                        "network.read",
                                                        "network.write",
                                                        "system.reboot",
                                                        "app.cycle.read",
                                                        "config.read",
                                                        "config.write",
                                                        CRUX_CONFIG_SERVICE_PERMISSION_SAVE};
static const char *const viewer_permissions[] = {CRUX_MGMT_STATUS_PERMISSION_READ, "network.read",
                                                 "config.read"};
static const char *const installer_permissions[] = {"network.write", "config.write"};
/* The mock's guest holds nothing. A role needs one permission, so this one names no service. */
static const char *const guest_permissions[] = {"guest.only"};
/* The account that creates the others, which no browser case signs in with. */
static const char *const owner_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE};
static const crux_access_role_t roles[] = {
    {"owner", owner_permissions, ARRAY_SIZE(owner_permissions)},
    {"administrator", administrator_permissions, ARRAY_SIZE(administrator_permissions)},
    {"viewer", viewer_permissions, ARRAY_SIZE(viewer_permissions)},
    {"installer", installer_permissions, ARRAY_SIZE(installer_permissions)},
    {"guest", guest_permissions, ARRAY_SIZE(guest_permissions)},
};
static const crux_access_policy_t policy = {roles, ARRAY_SIZE(roles), CRUX_E2E_SESSION_IDLE_S,
                                            8u * 3600u, NULL};

/* The mock's parameter table, with its validators, which give no reason. */
static bool interval_valid(const crux_config_value_t *candidate, void *context)
{
    (void)context;
    return candidate->type == CRUX_CONFIG_U32 && candidate->data.u32 >= 1u &&
           candidate->data.u32 <= 3600u;
}

static bool label_valid(const crux_config_value_t *candidate, void *context)
{
    (void)context;
    if (candidate->type != CRUX_CONFIG_STRING) {
        return false;
    }
    const char *text = candidate->data.string.text;
    size_t length = candidate->data.string.length;
    for (size_t index = 0; index + 3u <= length; ++index) {
        if (memcmp(&text[index], "bad", 3u) == 0) {
            return false;
        }
    }
    return true;
}

static const crux_config_spec_t spec_verbose = {
    .key = "verbose", .default_value = {.type = CRUX_CONFIG_BOOL, .data.boolean = false}};
static const crux_config_spec_t spec_interval = {
    .key = "report_interval",
    .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 5u},
    .validate = interval_valid};
static const crux_config_spec_t spec_label = {
    .key = "device_label",
    .default_value = {.type = CRUX_CONFIG_STRING, .data.string = {"pilot", 5u}},
    .validate = label_valid};
static const crux_config_spec_t spec_secret = {
    .key = "link_secret",
    .default_value = {.type = CRUX_CONFIG_STRING, .data.string = {"unset", 5u}}};
static const crux_config_spec_t spec_limit = {
    .key = "build_limit", .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 64u}};
static const crux_config_spec_t spec_private = {
    .key = "private_key", .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 9u}};
static crux_config_param_t verbose, interval, label, secret, limit, private_key;
static char storage_label[CRUX_CONFIG_STRING_MAX + 1u], storage_secret[CRUX_CONFIG_STRING_MAX + 1u];

static const crux_config_service_entry_t entries[] = {
    {&verbose, "config.read", "config.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&interval, "config.read", "config.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&label, "config.read", "config.write", false, CRUX_CONFIG_ACTIVATION_RESTART},
    {&secret, "config.read", "config.write", true, CRUX_CONFIG_ACTIVATION_RESTART},
    {&limit, "config.read", NULL, false, CRUX_CONFIG_ACTIVATION_SAFE_STATE},
    /* Under a permission no account holds, as a composition keeps a parameter private. */
    {&private_key, "config.internal", "config.internal", false, CRUX_CONFIG_ACTIVATION_APPLICATION},
};

static bool parameters_ready(void)
{
    return crux_config_init(&verbose, &spec_verbose) == CRUX_CONFIG_OK &&
           crux_config_init(&interval, &spec_interval) == CRUX_CONFIG_OK &&
           crux_config_init_string(&label, &spec_label, storage_label, sizeof(storage_label)) ==
               CRUX_CONFIG_OK &&
           crux_config_init_string(&secret, &spec_secret, storage_secret, sizeof(storage_secret)) ==
               CRUX_CONFIG_OK &&
           crux_config_init(&limit, &spec_limit) == CRUX_CONFIG_OK &&
           crux_config_init(&private_key, &spec_private) == CRUX_CONFIG_OK;
}

/* A project item that the interface does not know, as the mock has. */
static crux_status_t read_cycle_count(void *context, crux_mgmt_status_value_t *out)
{
    (void)context;
    out->type = CRUX_MGMT_STATUS_U32;
    out->data.u32 = 42u;
    return CRUX_OK;
}

static const crux_mgmt_status_item_t status_items[] = {
    {"app.cycle_count", "app.cycle.read", read_cycle_count, NULL},
};

static const crux_native_management_profile_t profile = {
    .policy = &policy,
    .entries = entries,
    .entry_count = ARRAY_SIZE(entries),
    .init_parameters = parameters_ready,
    .platform_version = "0.1.0-native_sim",
    .project_version = "e2e-220",
    .status_items = status_items,
    .status_item_count = ARRAY_SIZE(status_items),
};

/* Creates an account with the test password. One left with its change required gets the
 * administrator's reset, which is what forces a change at the next login. */
static bool account_ready(crux_access_session_t owner, const char *user, const char *role,
                          bool leave_forced)
{
    return crux_access_account_create(owner, user, role, PASSWORD, strlen(PASSWORD)) ==
               CRUX_ACCESS_OK &&
           (!leave_forced || crux_access_account_reset_password(
                                 owner, user, PASSWORD, strlen(PASSWORD)) == CRUX_ACCESS_OK);
}

/* The command line's account list as the owner, through a buffer as #242's image does, which shows
 * the accounts the browser cases need. The serial console is not composed on native_sim. */
static char cli_output[512];
static size_t cli_used;

static int cli_write(void *transport, const void *buffer, size_t length, uint32_t timeout_ms)
{
    (void)transport;
    (void)timeout_ms;
    size_t room = sizeof(cli_output) - 1u - cli_used;
    size_t taken = length < room ? length : room;
    memcpy(&cli_output[cli_used], buffer, taken);
    cli_used += taken;
    cli_output[cli_used] = '\0';
    return (int)length;
}

static bool accounts_listed(crux_access_session_t owner)
{
    static crux_cli_context_t context;
    static const crux_cli_io_t io = {.read = NULL, .write = cli_write, .transport = NULL};
    static const char line[] = "account list";
    int status = -1;
    cli_used = 0;
    bool listed = crux_cli_exec(&context, &io, CRUX_ACCESS_CHANNEL_SERIAL, owner, line,
                                strlen(line), &status) == CRUX_CLI_OK &&
                  status == 0 && strstr(cli_output, "newuser") != NULL;
    /* Names, roles and flags only: the list holds no secret. */
    printk("CRUX_E2E_ACCOUNTS\n%s\n", cli_output);
    return listed;
}

/* The declared test step, on a blank disk only: the first account over serial, then the accounts of
 * the mock. A later boot finds them in the store. */
static bool accounts_ready(void)
{
    static const crux_access_origin_t serial = {.channel = CRUX_ACCESS_CHANNEL_SERIAL};
    crux_access_session_t owner = {0};
    if (crux_access_provision_first_account(OWNER, OWNER_PASSWORD, strlen(OWNER_PASSWORD), "owner",
                                            &serial) != CRUX_ACCESS_OK ||
        crux_native_management_login(OWNER, OWNER_PASSWORD, &owner) != CRUX_ACCESS_OK) {
        return false;
    }
    bool ready = account_ready(owner, "operator", "administrator", false) &&
                 account_ready(owner, "viewer", "viewer", false) &&
                 account_ready(owner, "writer", "installer", false) &&
                 account_ready(owner, "guest", "guest", false) &&
                 account_ready(owner, "newuser", "administrator", true);
    crux_access_logout(&owner);
    return ready;
}

/* The command line needs its tables, which the services start registers, so this runs after it. */
static bool accounts_seen(void)
{
    crux_access_session_t owner = {0};
    if (crux_native_management_login(OWNER, OWNER_PASSWORD, &owner) != CRUX_ACCESS_OK) {
        return false;
    }
    bool ready = accounts_listed(owner);
    crux_access_logout(&owner);
    return ready;
}

int main(void)
{
    crux_native_management_start_t started = crux_native_management_start_profile(&profile);
    if (started != CRUX_NATIVE_MANAGEMENT_STARTED) {
        printk("CRUX_E2E_START_FAILED composition %d\n", (int)started);
        return 1;
    }
    bool first = crux_native_management_store_state_at_start() == CRUX_CREDENTIAL_STATE_EMPTY;
    if (first && !accounts_ready()) {
        printk("CRUX_E2E_START_FAILED accounts\n");
        return 1;
    }
    crux_native_management_services_start_t services = crux_native_management_services_start();
    if (services != CRUX_NATIVE_MANAGEMENT_SERVICES_STARTED) {
        printk("CRUX_E2E_START_FAILED services %d\n", (int)services);
        return 1;
    }
    if (first && !accounts_seen()) {
        printk("CRUX_E2E_START_FAILED account list\n");
        return 1;
    }
    printk("CRUX_E2E_READY %d first=%d idle=%u\n", CONFIG_CRUX_WEB_PORT, first ? 1 : 0,
           (unsigned)CRUX_E2E_SESSION_IDLE_S);
    return 0;
}
