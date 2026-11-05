/* See net_test.h. */
#include "net_test.h"

#include <sys/wait.h>
#include <unistd.h>

crux_access_session_t s_admin, s_reader, s_writer, s_both, s_saver, s_stranger, s_forced;
const crux_access_session_t s_never_valid = {0};

const crux_network_settings_t net_defaults = {0xC000020Au, 24u, false, 0u}; /* 192.0.2.10/24 */
const crux_network_settings_t net_set_a = {0xC6336414u, 24u, true,
                                           0xC6336401u}; /* 198.51.100.20/24 gw .1 */
const crux_network_settings_t net_set_b = {0xCB007114u, 26u, true,
                                           0xCB007101u}; /* 203.0.113.20/26 gw .1 */

static unsigned cases_run, cases_failed;

void net_run_case(const char *name, void (*body)(void))
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

int net_finish(const char *suite)
{
    printf("%s: %u cases, %u failed\n", suite, cases_run, cases_failed);
    return cases_failed == 0 ? 0 : 1;
}

static const char *const admin_permissions[] = {
    CRUX_ACCESS_PERMISSION_MANAGE,       "network.read", "network.write",
    CRUX_CONFIG_SERVICE_PERMISSION_SAVE, "cfg.read",     "cfg.write"};
static const char *const reader_permissions[] = {"network.read"};
static const char *const writer_permissions[] = {"network.write"};
static const char *const both_permissions[] = {"network.read", "network.write"};
static const char *const saver_permissions[] = {CRUX_CONFIG_SERVICE_PERMISSION_SAVE, "cfg.read",
                                                "cfg.write"};
static const char *const stranger_permissions[] = {"other.thing"};
static const crux_access_role_t roles[] = {
    {"administrator", admin_permissions, 6}, {"reader", reader_permissions, 1},
    {"writer", writer_permissions, 1},       {"both", both_permissions, 2},
    {"saver", saver_permissions, 3},         {"stranger", stranger_permissions, 1},
};
static const crux_access_policy_t policy = {roles, 6, 15u * 60u, 8u * 3600u, NULL};

static const crux_config_spec_t spec_limit = {
    NET_CFG_LIMIT, 1, {.type = CRUX_CONFIG_U32, .data.u32 = 5}, NULL, NULL};
static const crux_config_spec_t spec_tail = {
    NET_CFG_TAIL, 1, {.type = CRUX_CONFIG_U32, .data.u32 = 9}, NULL, NULL};
static crux_config_param_t param_limit, param_tail;
static crux_config_service_entry_t table[NET_TABLE_ENTRIES];

const crux_config_service_entry_t *net_table(void)
{
    return table;
}

static void create(crux_access_session_t admin, const char *user, const char *role)
{
    CHECK(crux_access_account_create(admin, user, role, ACCESS_TEST_PASSWORD,
                                     strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
}

void net_boot_with(const net_options_t *options, crux_network_boot_report_t *report)
{
    size_t count = 0;
    const crux_config_service_entry_t *entries = crux_network_settings_entries(&count);
    crux_network_settings_config_t config = {net_defaults};

    access_test_boot(&policy);
    CHECK(crux_access_provision_first_account(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD,
                                              strlen(ACCESS_TEST_PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    s_admin = access_test_session(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD, &access_test_web);
    create(s_admin, "reader", "reader");
    create(s_admin, "writer", "writer");
    create(s_admin, "both", "both");
    create(s_admin, "saver", "saver");
    create(s_admin, "stranger", "stranger");
    create(s_admin, "forced", "both");
    CHECK(crux_access_account_reset_password(s_admin, "forced", ACCESS_TEST_PASSWORD,
                                             strlen(ACCESS_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    s_reader = access_test_session("reader", ACCESS_TEST_PASSWORD, &access_test_web);
    s_writer = access_test_session("writer", ACCESS_TEST_PASSWORD, &access_test_web);
    s_both = access_test_session("both", ACCESS_TEST_PASSWORD, &access_test_web);
    s_saver = access_test_session("saver", ACCESS_TEST_PASSWORD, &access_test_web);
    s_stranger = access_test_session("stranger", ACCESS_TEST_PASSWORD, &access_test_web);
    s_forced = access_test_session("forced", ACCESS_TEST_PASSWORD, &access_test_web);

    if (!options->keep_store) {
        config_fault_reset();
    }
    net_port_reset();
    net_port_fail_applies(options->apply_failures);
    CHECK(crux_config_init(&param_limit, &spec_limit) == CRUX_CONFIG_OK ||
          param_limit.spec == &spec_limit);
    CHECK(crux_config_init(&param_tail, &spec_tail) == CRUX_CONFIG_OK ||
          param_tail.spec == &spec_tail);
    CHECK(count == CRUX_NETWORK_SETTINGS_ENTRY_COUNT);
    table[0] = (crux_config_service_entry_t){&param_limit, "cfg.read", "cfg.write", false,
                                             CRUX_CONFIG_ACTIVATION_APPLICATION};
    for (size_t index = 0; index < count; ++index) {
        table[1 + index] = entries[index];
    }
    table[NET_TABLE_ENTRIES - 1u] = (crux_config_service_entry_t){
        &param_tail, "cfg.read", "cfg.write", false, CRUX_CONFIG_ACTIVATION_APPLICATION};
    CHECK(crux_network_settings_boot(&config, report) == CRUX_NETWORK_OK);
    net_events_clear();
}

void net_boot(crux_network_boot_report_t *report)
{
    const net_options_t options = {false, 0};

    net_boot_with(&options, report);
}

void net_start(void)
{
    crux_network_boot_report_t report;

    net_boot(&report);
    CHECK(crux_config_service_start(table, NET_TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_network_settings_start() == CRUX_NETWORK_OK);
}

void net_start_on_store(void)
{
    crux_network_boot_report_t report;
    const net_options_t keep = {true, 0};

    net_boot_with(&keep, &report);
    CHECK(crux_config_service_start(table, NET_TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_network_settings_start() == CRUX_NETWORK_OK);
}

void net_store_u32(const char *key, uint32_t value)
{
    const uint8_t wire[8] = {1,
                             CRUX_CONFIG_U32,
                             1,
                             0,
                             (uint8_t)value,
                             (uint8_t)(value >> 8),
                             (uint8_t)(value >> 16),
                             (uint8_t)(value >> 24)};

    config_fault_record(key, wire, sizeof(wire));
}

void net_store_bool(const char *key, bool value)
{
    const uint8_t wire[5] = {1, CRUX_CONFIG_BOOL, 1, 0, value ? 1u : 0u};

    config_fault_record(key, wire, sizeof(wire));
}

crux_network_settings_t net_stored_form(const crux_network_settings_t *settings)
{
    crux_network_settings_t stored = *settings;

    if (!stored.has_gateway) {
        stored.gateway = 0u;
    }
    return stored;
}

bool net_same(const crux_network_settings_t *a, const crux_network_settings_t *b)
{
    crux_network_settings_t x = net_stored_form(a);
    crux_network_settings_t y = net_stored_form(b);

    return x.address == y.address && x.prefix_length == y.prefix_length &&
           x.has_gateway == y.has_gateway && x.gateway == y.gateway;
}

void net_store_set(const crux_network_settings_t *set, bool use_saved)
{
    crux_network_settings_t stored = net_stored_form(set);

    net_store_u32(CRUX_NETWORK_SETTINGS_KEY_ADDRESS, stored.address);
    net_store_u32(CRUX_NETWORK_SETTINGS_KEY_PREFIX, stored.prefix_length);
    net_store_u32(CRUX_NETWORK_SETTINGS_KEY_GATEWAY, stored.gateway);
    net_store_u32(CRUX_NETWORK_SETTINGS_KEY_CHECK, crux_network_settings_check_key(&stored));
    net_store_bool(CRUX_NETWORK_SETTINGS_KEY_USE_SAVED, use_saved);
}

crux_network_boot_reason_t net_reboot_decision(crux_network_settings_t *values)
{
    return network_test_reboot(values);
}

size_t net_denials(void)
{
    return access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED);
}

static size_t cut_remaining;
static bool cut_last_lands;

static void cut_hook(void *context)
{
    (void)context;
    if (cut_remaining == 0u) {
        /* The write that meets the cut fails, and after it nothing is written. If it lands, its
         * record is stored and its acknowledgement is what was lost. */
        config_fault_write((config_fault_write_options_t){CRUX_CONFIG_ERR_IO, cut_last_lands});
        cut_last_lands = false;
    } else {
        --cut_remaining;
    }
}

void net_power_cut_after(size_t writes, bool last_write_lands)
{
    cut_remaining = writes;
    cut_last_lands = last_write_lands;
    config_fault_write_hook(cut_hook, NULL);
}
