/*
 * ADR-0004 phase 0. Access, the credential store and the security events come from the Zephyr
 * module under CONFIG_CRUX_ACCESS, and the Configuration, status, network settings, TLS
 * certificate, file and reboot services under their own symbols, on real PSA providers, a real
 * FatFs on a simulated disk and the Crux Logging provider. This file is the application: it starts
 * them in the order of docs/management-in-a-product.md and checks that they work together. Test
 * only: the entropy is test-only and the first account is a declared input.
 */
#include "crux_tf_psa_threading.h"

#include <crux/access.h>
#include <crux/config_service.h>
#include <crux/configuration.h>
#include <crux/credential_store.h>
#include <crux/file_service.h>
#include <crux/file_zephyr.h>
#include <crux/management_status.h>
#include <crux/network_settings.h>
#include <crux/reboot.h>
#include <crux/security_event.h>
#include <crux/system.h>
#include <crux/time.h>
#include <crux_access_adapter.h>
#include <crux_credential_adapter.h>
#include <crux_tls_certificate_adapter.h>

#include <ff.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>

enum { ROOT_SLOT_A = 1, ROOT_SLOT_B = 2, ROOT_FILES = 3 };
#define MOUNT_POINT "/SD:"
#define DISK_NAME "SD:"
#define PATH_SLOT_A MOUNT_POINT "/secure-a"
#define PATH_SLOT_B MOUNT_POINT "/secure-b"
#define PATH_FILES MOUNT_POINT "/files"
#define FILE_AREA "files"
#define FILE_READ "file.read"
#define FILE_WRITE "file.write"
#define ADMIN "admin"
#define ADMIN_PASSWORD "module test password"
#define WRONG_PASSWORD "this is the wrong password"
#define TEST_PERMISSION "module.test"
#define CONFIG_READ "config.read"
#define CONFIG_WRITE "config.write"
#define PARAM_INTERVAL "demo.interval_ms"

static const char *const admin_permissions[] = {
    TEST_PERMISSION,
    CRUX_SECURITY_EVENT_PERMISSION_READ,
    CRUX_ACCESS_PERMISSION_MANAGE,
    CRUX_MGMT_STATUS_PERMISSION_READ,
    CONFIG_READ,
    CONFIG_WRITE,
    CRUX_CONFIG_SERVICE_PERMISSION_SAVE,
    CRUX_NETWORK_SETTINGS_PERMISSION_READ,
    CRUX_NETWORK_SETTINGS_PERMISSION_WRITE,
    CRUX_REBOOT_PERMISSION,
    FILE_READ,
    FILE_WRITE,
};
static const char *const guest_permissions[] = {"guest.only"};
static const crux_access_role_t roles[] = {
    {"guest", guest_permissions, ARRAY_SIZE(guest_permissions)},
    {"administrator", admin_permissions, ARRAY_SIZE(admin_permissions)},
};
static const crux_access_policy_t policy = {roles, ARRAY_SIZE(roles), 15u * 60u, 8u * 3600u, NULL};

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = MOUNT_POINT,
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static const crux_config_spec_t spec_interval = {
    .key = PARAM_INTERVAL,
    .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 1000u},
};
static crux_config_param_t param_interval;
static const crux_config_service_entry_t table[] = {
    {&param_interval, CONFIG_READ, CONFIG_WRITE, false, CRUX_CONFIG_ACTIVATION_RESTART},
};
/* The reference table and then the five entries of the network settings service, in the order that
 * the table must keep. */
static crux_config_service_entry_t
    services_table[ARRAY_SIZE(table) + CRUX_NETWORK_SETTINGS_ENTRY_COUNT];

static const char *const dns_names[] = {"localhost"};
static const uint8_t ipv4_names[][4] = {{127, 0, 0, 1}};
static const crux_tls_certificate_options_t certificate_options = {
    .names = {.common_name = "crux module test",
              .dns_names = dns_names,
              .dns_count = ARRAY_SIZE(dns_names),
              .ipv4 = ipv4_names,
              .ipv4_count = ARRAY_SIZE(ipv4_names)},
    .not_before_s = CRUX_TLS_CERTIFICATE_DEFAULT_NOT_BEFORE_S,
    .validity_days = 0u,
};
static crux_tls_certificate_start_t certificate_how = CRUX_TLS_CERTIFICATE_STARTED;

static crux_file_root_t reserved_roots[] = {ROOT_SLOT_A, ROOT_SLOT_B};
static const crux_file_area_t areas[] = {{
    .name = FILE_AREA,
    .root = ROOT_FILES,
    .read_permission = FILE_READ,
    .write_permission = FILE_WRITE,
    .max_file_bytes = 4096u,
    .max_area_bytes = 16384u,
    .max_entries = 8u,
    .partial_suffix = NULL,
    .write_modes = CRUX_FILE_SERVICE_MANAGED,
}};
static const crux_access_session_t never_valid = {0};
static crux_credential_state_t state_at_start = CRUX_CREDENTIAL_STATE_UNUSABLE;
static crux_access_session_t admin;

static bool directory_ready(const char *path)
{
    int result = fs_mkdir(path);
    return result == 0 || result == -EEXIST;
}

static bool medium_ready(void)
{
    if (fs_mount(&mount) != 0) {
        if (fs_mkfs(FS_FATFS, (uintptr_t)DISK_NAME, NULL, 0) != 0 || fs_mount(&mount) != 0) {
            return false;
        }
    }
    return directory_ready(PATH_SLOT_A) && directory_ready(PATH_SLOT_B) &&
           directory_ready(PATH_FILES) &&
           crux_file_zephyr_bind_root(ROOT_SLOT_A, PATH_SLOT_A) == CRUX_FILE_OK &&
           crux_file_zephyr_bind_root(ROOT_SLOT_B, PATH_SLOT_B) == CRUX_FILE_OK &&
           crux_file_zephyr_bind_root(ROOT_FILES, PATH_FILES) == CRUX_FILE_OK;
}

static size_t count_events(crux_access_session_t session, crux_security_event_id_t id)
{
    crux_security_event_record_t record;
    uint32_t cursor = 0;
    size_t count = 0;
    while (crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK) {
        count += record.id == id;
    }
    return count;
}

static void *start_services(void)
{
    /* First, so that everything that follows can emit. */
    zassert_equal(crux_security_event_init(NULL, 0), CRUX_OK);
    zassert_equal(crux_realtime_init(), CRUX_OK);
    zassert_true(medium_ready());
    crux_credential_result_t store = crux_credential_start(ROOT_SLOT_A, ROOT_SLOT_B);
    zassert_true(store == CRUX_CREDENTIAL_OK || store == CRUX_CREDENTIAL_ERR_UNUSABLE);
    state_at_start = crux_credential_state();
    zassert_equal(crux_access_init(&policy), CRUX_ACCESS_OK);
    zassert_equal(crux_config_init(&param_interval, &spec_interval), CRUX_CONFIG_OK);
    /* Network settings boot before the Configuration service starts, on a table that holds its
     * entries. */
    static const crux_network_settings_config_t network = {.defaults = {.address = 0xC000020Au,
                                                                        .prefix_length = 24u,
                                                                        .has_gateway = true,
                                                                        .gateway = 0xC0000201u}};
    size_t network_count = 0;
    const crux_config_service_entry_t *network_entries =
        crux_network_settings_entries(&network_count);
    zassert_not_null(network_entries);
    zassert_equal(network_count, CRUX_NETWORK_SETTINGS_ENTRY_COUNT);
    memcpy(services_table, table, sizeof(table));
    memcpy(&services_table[ARRAY_SIZE(table)], network_entries,
           sizeof(services_table[0]) * network_count);
    zassert_equal(crux_network_settings_boot(&network, NULL), CRUX_NETWORK_OK);
    zassert_equal(crux_config_service_start(services_table, ARRAY_SIZE(services_table)),
                  CRUX_CONFIG_SERVICE_OK);
    crux_reset_cause_t cause = {0};
    crux_mgmt_status_config_t status = {
        .platform_version = "native_sim module",
        .project_version = "test",
        .boot_reset_cause_status = crux_system_reset_cause_get(&cause),
        .boot_reset_cause = cause,
    };
    zassert_equal(crux_mgmt_status_start(&status), CRUX_OK);
    zassert_equal(crux_security_event_start(), CRUX_OK);

    /* The services that need the store, Access and the events to be running. */
    zassert_equal(crux_tls_certificate_start(&certificate_options, &certificate_how),
                  CRUX_TLS_CERTIFICATE_OK);
    zassert_equal(crux_network_settings_start(), CRUX_NETWORK_OK);
    zassert_equal(crux_network_settings_start_worker(), CRUX_NETWORK_OK);
    static const crux_file_service_config_t file_config = {
        .areas = areas,
        .area_count = ARRAY_SIZE(areas),
        .reserved_roots = reserved_roots,
        .reserved_root_count = ARRAY_SIZE(reserved_roots),
        .no_credential_store = false,
    };
    zassert_equal(crux_file_service_start(&file_config), CRUX_FILE_SERVICE_OK);
    static const crux_reboot_config_t reboot = {.policy_kind = CRUX_REBOOT_POLICY_ALWAYS_ALLOW};
    zassert_equal(crux_reboot_start(&reboot), CRUX_REBOOT_OK);
    zassert_equal(crux_reboot_start_worker(), CRUX_REBOOT_OK);
    return NULL;
}

ZTEST_SUITE(management_module, NULL, start_services, NULL, NULL, NULL);

ZTEST(management_module, test_01_the_store_started_on_the_module_s_core)
{
    zassert_not_equal(state_at_start, CRUX_CREDENTIAL_STATE_UNUSABLE);
    zassert_equal(crux_credential_state(), state_at_start);
}

ZTEST(management_module, test_02_first_account_login_and_refusals)
{
    static const crux_access_origin_t serial = {.channel = CRUX_ACCESS_CHANNEL_SERIAL};
    static const crux_access_origin_t web = {
        .channel = CRUX_ACCESS_CHANNEL_WEB, .source = {192, 0, 2, 1}, .source_length = 4};
    crux_access_session_t guest = {0};
    uint32_t retry_after_ms = 0;

    zassert_equal(crux_access_login_password(ADMIN, ADMIN_PASSWORD, strlen(ADMIN_PASSWORD), &web,
                                             &admin, &retry_after_ms),
                  CRUX_ACCESS_ERR_DENIED, "no account yet");
    zassert_equal(crux_access_provision_first_account(ADMIN, ADMIN_PASSWORD, strlen(ADMIN_PASSWORD),
                                                      "administrator", &serial),
                  CRUX_ACCESS_OK);
    zassert_equal(crux_access_login_password(ADMIN, WRONG_PASSWORD, strlen(WRONG_PASSWORD), &web,
                                             &guest, &retry_after_ms),
                  CRUX_ACCESS_ERR_DENIED);
    zassert_equal(crux_access_login_password(ADMIN, ADMIN_PASSWORD, strlen(ADMIN_PASSWORD), &web,
                                             &admin, &retry_after_ms),
                  CRUX_ACCESS_OK);
    zassert_equal(crux_access_check(admin, TEST_PERMISSION), CRUX_ACCESS_OK);
    zassert_equal(crux_access_check(admin, "nothing.names.this"), CRUX_ACCESS_ERR_DENIED);
}

ZTEST(management_module, test_03_events_status_and_configuration_are_the_module_s)
{
    static char document[4096];
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    size_t length = 0;
    crux_config_type_t type;
    crux_config_service_item_t applied;
    crux_config_service_save_report_t report;

    /* The event provider of Access, from security_event. */
    zassert_equal(count_events(admin, CRUX_SECURITY_EVENT_ID_ACCESS_PROVISIONED), 1u);
    zassert_true(count_events(admin, CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED) >= 1u);
    zassert_true(count_events(admin, CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_OK) >= 1u);

    /* The status service, with its System, Time, network, credential and event sources. */
    zassert_equal(crux_mgmt_status_report_json(admin, document, sizeof(document), &length),
                  CRUX_OK);
    zassert_not_null(strstr(document, "\"system.uptime_ms\""));
    zassert_not_null(strstr(document, "\"security.events_emitted\""));
    zassert_not_null(strstr(document, "\"security.credential_state\""));
    zassert_not_null(strstr(document, "\"net.admin_up\""));
    zassert_equal(crux_mgmt_status_report_json(never_valid, document, sizeof(document), &length),
                  CRUX_ERR_INVALID_STATE);

    /* The Configuration service, over the no-provider port: it updates in memory and cannot save.
     */
    zassert_equal(
        crux_config_service_read(never_valid, PARAM_INTERVAL, text, sizeof(text), &length, &type),
        CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    zassert_equal(crux_config_service_update(admin, PARAM_INTERVAL, "2000", 4u, &applied),
                  CRUX_CONFIG_SERVICE_OK);
    zassert_equal(applied.activation, CRUX_CONFIG_ACTIVATION_RESTART);
    zassert_equal(
        crux_config_service_read(admin, PARAM_INTERVAL, text, sizeof(text), &length, &type),
        CRUX_CONFIG_SERVICE_OK);
    zassert_equal(type, CRUX_CONFIG_U32);
    zassert_mem_equal(text, "2000", 4u);
    zassert_equal(crux_config_service_update(admin, PARAM_INTERVAL, "x", 1u, &applied),
                  CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_config_service_save(admin, &report), CRUX_CONFIG_SERVICE_ERR_STORE);
    zassert_equal(report.failed_result, CRUX_CONFIG_ERR_NOT_SUPPORTED);
    /* The event provider of the Configuration service, from config_service. */
    zassert_equal(count_events(admin, CRUX_SECURITY_EVENT_ID_CONFIG_SAVE_FAILED), 1u);

    /* Network settings, over the port that applies nothing: it shows no running values. */
    crux_network_state_t network;
    zassert_equal(crux_network_settings_get(admin, &network), CRUX_NETWORK_OK);
    zassert_false(network.has_running);
    zassert_equal(crux_network_settings_get(never_valid, &network),
                  CRUX_NETWORK_ERR_SESSION_INVALID);

    /* The TLS certificate module made a certificate, and it has a fingerprint. */
    char fingerprint[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    zassert_equal(certificate_how, CRUX_TLS_CERTIFICATE_GENERATED);
    zassert_equal(crux_tls_certificate_fingerprint(fingerprint), CRUX_TLS_CERTIFICATE_OK);
    zassert_true(fingerprint[0] != '\0');

    /* The file service shows its one area, writable, and a file that does not exist. */
    size_t cursor = 0;
    const crux_file_area_t *area = NULL;
    bool writable = false;
    crux_file_info_t info;
    zassert_equal(crux_file_service_area_next(admin, &cursor, &area, &writable),
                  CRUX_FILE_SERVICE_OK);
    zassert_equal(strcmp(area->name, FILE_AREA), 0);
    zassert_true(writable);
    zassert_equal(crux_file_service_stat(admin, FILE_AREA, "missing.txt", &info),
                  CRUX_FILE_SERVICE_ERR_NOT_FOUND);

    /* Reboot: started with a policy, and no request made. */
    crux_reboot_state_t reboot_state;
    zassert_equal(crux_reboot_get(admin, &reboot_state), CRUX_REBOOT_OK);
    zassert_equal(reboot_state.state, CRUX_REBOOT_STATE_NONE);
}
