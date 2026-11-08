/* RT1060 compile vehicle: a copy of
 * tests/native/management_composition/composition/crux_native_management.c with one difference, the
 * TLS credentials. The native file includes the declared test certificate, whose header refuses any
 * build that is not native_sim, so this copy hands the generated certificate and the store's key to
 * the TLS credentials, as docs/tls-certificate.md step 6 says. Nothing else is changed. */
#include "crux_native_management.h"
#include <crux_tls_certificate_adapter.h>

#include "file_native.h"
#if defined(CRUX_NATIVE_MANAGEMENT_SERVICES)
#include <crux/network_settings.h>
#endif

#include <crux/management_status.h>
#include <crux/security_event.h>
#include <crux/system.h>
#include <crux/time.h>
#include <crux_access_adapter.h>
#include <crux_credential_adapter.h>

#include <errno.h>
#include <ff.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

enum { ROOT_SLOT_A = 1, ROOT_SLOT_B = 2, ROOT_FILES = 3 };
#define MOUNT_POINT "/SD:"
#define DISK_NAME "SD:"
#define PATH_SLOT_A MOUNT_POINT "/secure-a"
#define PATH_SLOT_B MOUNT_POINT "/secure-b"
#define PATH_FILES MOUNT_POINT "/files"

/* network.* and app.pump.read are the web suite's routes, which tests/native/web_https runs here.
 * The services composition of Issue #242 adds the file area's and the TLS certificate's
 * permissions, and nothing else: see services.cmake. */
static const char *const viewer_permissions[] = {CRUX_MGMT_STATUS_PERMISSION_READ, "network.read",
                                                 "app.pump.read"
#if defined(CRUX_NATIVE_MANAGEMENT_SERVICES)
                                                 ,
                                                 CRUX_NATIVE_MANAGEMENT_PERMISSION_FILE_READ
#endif
};
static const char *const operator_permissions[] = {CRUX_MGMT_STATUS_PERMISSION_READ,
                                                   CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_READ,
                                                   CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_WRITE};
static const char *const administrator_permissions[] = {
    CRUX_MGMT_STATUS_PERMISSION_READ,
    CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_READ,
    CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_WRITE,
    CRUX_CONFIG_SERVICE_PERMISSION_SAVE,
    CRUX_SECURITY_EVENT_PERMISSION_READ,
    CRUX_ACCESS_PERMISSION_MANAGE,
    "network.read",
    "network.write",
    "app.pump.read"
#if defined(CRUX_NATIVE_MANAGEMENT_SERVICES)
    ,
    CRUX_NATIVE_MANAGEMENT_PERMISSION_FILE_READ,
    CRUX_NATIVE_MANAGEMENT_PERMISSION_FILE_WRITE,
    CRUX_NATIVE_MANAGEMENT_PERMISSION_TLS_MANAGE
#endif
};
/* A permission no service names, so that this role can be refused by every service. */
static const char *const guest_permissions[] = {"guest.only"};
static const crux_access_role_t roles[] = {
    {CRUX_NATIVE_MANAGEMENT_ROLE_GUEST, guest_permissions, ARRAY_SIZE(guest_permissions)},
    {CRUX_NATIVE_MANAGEMENT_ROLE_VIEWER, viewer_permissions, ARRAY_SIZE(viewer_permissions)},
    {CRUX_NATIVE_MANAGEMENT_ROLE_OPERATOR, operator_permissions, ARRAY_SIZE(operator_permissions)},
    {CRUX_NATIVE_MANAGEMENT_ROLE_ADMINISTRATOR, administrator_permissions,
     ARRAY_SIZE(administrator_permissions)},
};
static const crux_access_policy_t policy = {roles, ARRAY_SIZE(roles), 15u * 60u, 8u * 3600u, NULL};

static const crux_config_spec_t spec_name = {
    .key = CRUX_NATIVE_MANAGEMENT_PARAM_NAME,
    .default_value = {.type = CRUX_CONFIG_STRING, .data.string = {"unnamed", 7u}},
};
static const crux_config_spec_t spec_interval = {
    .key = CRUX_NATIVE_MANAGEMENT_PARAM_INTERVAL,
    .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 1000u},
};
static const crux_config_spec_t spec_token = {
    .key = CRUX_NATIVE_MANAGEMENT_PARAM_TOKEN,
    .default_value = {.type = CRUX_CONFIG_STRING, .data.string = {"", 0u}},
};
static crux_config_param_t param_name, param_interval, param_token;
static char storage_name[64], storage_token[64];
static const crux_config_service_entry_t table[] = {
    {&param_name, CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_READ,
     CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_WRITE, false, CRUX_CONFIG_ACTIVATION_APPLICATION},
    {&param_interval, CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_READ,
     CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_WRITE, false, CRUX_CONFIG_ACTIVATION_RESTART},
    {&param_token, CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_READ,
     CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_WRITE, true, CRUX_CONFIG_ACTIVATION_APPLICATION},
};
#if defined(CRUX_NATIVE_MANAGEMENT_SERVICES)
/* The reference table and then the five entries of the network settings service, which it hands
 * over in the order the table must keep. They sit under network.private, which no role holds. */
static crux_config_service_entry_t
    services_table[MAX(ARRAY_SIZE(table), CRUX_NATIVE_MANAGEMENT_PROFILE_ENTRIES_MAX) +
                   CRUX_NETWORK_SETTINGS_ENTRY_COUNT];
#endif
/* The image's profile, or NULL for the reference one. Set once, before anything starts. */
static const crux_native_management_profile_t *profile_in_use;

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = MOUNT_POINT,
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static crux_credential_state_t state_at_start = CRUX_CREDENTIAL_STATE_UNUSABLE;

static bool directory_ready(const char *path)
{
    int result = fs_mkdir(path);
    return result == 0 || result == -EEXIST;
}

/* The disk is a file on the host, and it belongs to the test. A failure leaves the roots unbound,
 * so the store reads UNUSABLE: it still starts, as the contract says. */
static bool medium_ready(void)
{
    if (fs_mount(&mount) != 0) {
        if (fs_mkfs(FS_FATFS, (uintptr_t)DISK_NAME, NULL, 0) != 0 || fs_mount(&mount) != 0) {
            return false;
        }
    }
    return directory_ready(PATH_SLOT_A) && directory_ready(PATH_SLOT_B) &&
           crux_file_zephyr_bind_root(ROOT_SLOT_A, PATH_SLOT_A) == CRUX_FILE_OK &&
           crux_file_zephyr_bind_root(ROOT_SLOT_B, PATH_SLOT_B) == CRUX_FILE_OK
#if defined(CRUX_NATIVE_MANAGEMENT_SERVICES)
           && directory_ready(PATH_FILES) &&
           crux_file_zephyr_bind_root(ROOT_FILES, PATH_FILES) == CRUX_FILE_OK
#endif
        ;
}

#if defined(CRUX_NATIVE_MANAGEMENT_SERVICES)
/* Copies the network settings service's entries into the table and runs its boot, which must come
 * before the Configuration service starts. The defaults are from the documentation range, RFC 5737.
 * Nothing is applied to an interface: the composition links the no-op apply port. */
static bool network_settings_booted(const crux_config_service_entry_t *own, size_t own_count)
{
    static const crux_network_settings_config_t network = {.defaults = {.address = 0xC000020Au,
                                                                        .prefix_length = 24u,
                                                                        .has_gateway = true,
                                                                        .gateway = 0xC0000201u}};
    size_t count = 0;
    const crux_config_service_entry_t *entries = crux_network_settings_entries(&count);

    if (entries == NULL || count != CRUX_NETWORK_SETTINGS_ENTRY_COUNT) {
        return false;
    }
    if (own_count + count > ARRAY_SIZE(services_table)) {
        return false;
    }
    memcpy(services_table, own, sizeof(services_table[0]) * own_count);
    memcpy(&services_table[own_count], entries, sizeof(services_table[0]) * count);
    return crux_network_settings_boot(&network, NULL) == CRUX_NETWORK_OK;
}
#endif

static bool reference_parameters_ready(void)
{
    return crux_config_init_string(&param_name, &spec_name, storage_name, sizeof(storage_name)) ==
               CRUX_CONFIG_OK &&
           crux_config_init(&param_interval, &spec_interval) == CRUX_CONFIG_OK &&
           crux_config_init_string(&param_token, &spec_token, storage_token,
                                   sizeof(storage_token)) == CRUX_CONFIG_OK;
}

static bool config_ready(void)
{
    const crux_config_service_entry_t *own = table;
    size_t own_count = ARRAY_SIZE(table);
    bool (*init)(void) = reference_parameters_ready;

    if (profile_in_use != NULL && profile_in_use->entry_count != 0u) {
        if (profile_in_use->entries == NULL || profile_in_use->init_parameters == NULL ||
            profile_in_use->entry_count > CRUX_NATIVE_MANAGEMENT_PROFILE_ENTRIES_MAX) {
            return false;
        }
        own = profile_in_use->entries;
        own_count = profile_in_use->entry_count;
        init = profile_in_use->init_parameters;
    }
    if (!init()) {
        return false;
    }
#if defined(CRUX_NATIVE_MANAGEMENT_SERVICES)
    return network_settings_booted(own, own_count) &&
           crux_config_service_start(services_table,
                                     own_count + CRUX_NETWORK_SETTINGS_ENTRY_COUNT) ==
               CRUX_CONFIG_SERVICE_OK;
#else
    return crux_config_service_start(own, own_count) == CRUX_CONFIG_SERVICE_OK;
#endif
}

/* Each profile's device keys, as its listeners would ask for them. A failure stops that listener
 * and no other service, so it is not a start failure, and a test sees it through the store. */
static void device_keys_ensure(void)
{
    (void)crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY,
                                     CRUX_CREDENTIAL_KEY_ECDSA_P256);
#if defined(CONFIG_WOLFPSA)
    (void)crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY,
                                     CRUX_CREDENTIAL_KEY_ED25519);
#endif
}

crux_native_management_start_t crux_native_management_start(void)
{
    return crux_native_management_start_profile(NULL);
}

crux_native_management_start_t
crux_native_management_start_profile(const crux_native_management_profile_t *profile)
{
    profile_in_use = profile;
    /* First, so that everything that follows can emit. */
    if (crux_security_event_init(NULL, 0) != CRUX_OK) {
        return CRUX_NATIVE_MANAGEMENT_FAILED_EVENTS;
    }
    if (crux_realtime_init() != CRUX_OK) {
        return CRUX_NATIVE_MANAGEMENT_FAILED_TIME;
    }
    /* A medium that failed fails closed inside the store. */
    if (!medium_ready()) {
        printk("COMPOSITION medium not ready\n");
    }
    crux_credential_result_t store = crux_credential_start(ROOT_SLOT_A, ROOT_SLOT_B);
    if (store != CRUX_CREDENTIAL_OK && store != CRUX_CREDENTIAL_ERR_UNUSABLE) {
        return CRUX_NATIVE_MANAGEMENT_FAILED_MEDIUM;
    }
    state_at_start = crux_credential_state();

    /* An unusable store reads NOT_READY here and stays so: it fails closed. */
    crux_access_result_t access =
        crux_access_init(profile != NULL && profile->policy != NULL ? profile->policy : &policy);
    if (access != CRUX_ACCESS_OK && access != CRUX_ACCESS_ERR_NOT_READY) {
        return CRUX_NATIVE_MANAGEMENT_FAILED_ACCESS;
    }
    device_keys_ensure();
    if (!config_ready()) {
        return CRUX_NATIVE_MANAGEMENT_FAILED_CONFIG;
    }
    crux_reset_cause_t cause = {0};
    crux_mgmt_status_config_t status = {
        .platform_version = "native_sim composition",
        .project_version = "test",
        .boot_reset_cause_status = crux_system_reset_cause_get(&cause),
        .boot_reset_cause = cause,
    };
    if (profile != NULL) {
        status.platform_version =
            profile->platform_version != NULL ? profile->platform_version : status.platform_version;
        status.project_version =
            profile->project_version != NULL ? profile->project_version : status.project_version;
        status.project_items = profile->status_items;
        status.project_item_count = profile->status_item_count;
    }
    if (crux_mgmt_status_start(&status) != CRUX_OK) {
        return CRUX_NATIVE_MANAGEMENT_FAILED_STATUS;
    }
    if (crux_security_event_start() != CRUX_OK) {
        return CRUX_NATIVE_MANAGEMENT_FAILED_EVENT_THREAD;
    }
    return CRUX_NATIVE_MANAGEMENT_STARTED;
}

#if defined(CRUX_NATIVE_MANAGEMENT_SERVICES)
crux_file_root_t crux_native_management_file_root(void)
{
    return ROOT_FILES;
}

void crux_native_management_store_roots(crux_file_root_t roots[2])
{
    roots[0] = ROOT_SLOT_A;
    roots[1] = ROOT_SLOT_B;
}
#endif

crux_credential_state_t crux_native_management_store_state_at_start(void)
{
    return state_at_start;
}

crux_access_result_t crux_native_management_provision_test_account(void)
{
    static const crux_access_origin_t serial = {.channel = CRUX_ACCESS_CHANNEL_SERIAL};
    return crux_access_provision_first_account(CRUX_NATIVE_MANAGEMENT_TEST_USER,
                                               CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD,
                                               strlen(CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD),
                                               CRUX_NATIVE_MANAGEMENT_ROLE_ADMINISTRATOR, &serial);
}

crux_access_result_t crux_native_management_login(const char *user, const char *password,
                                                  crux_access_session_t *session)
{
    static const crux_access_origin_t web = {
        .channel = CRUX_ACCESS_CHANNEL_WEB, .source = {192, 0, 2, 1}, .source_length = 4};
    uint32_t retry_after_ms = 0;
    return crux_access_login_password(user, password, strlen(password), &web, session,
                                      &retry_after_ms);
}

int crux_mgmt_rt1060_tls_credentials(int sec_tag)
{
    static crux_tls_certificate_material_t material;

    if (crux_tls_certificate_material_take(&material) != CRUX_TLS_CERTIFICATE_OK) {
        return -ENOENT;
    }
    int result = tls_credential_add(sec_tag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
                                    material.certificate, material.certificate_length);
    if (result == 0) {
        result = tls_credential_add(sec_tag, TLS_CREDENTIAL_PRIVATE_KEY, material.private_key,
                                    material.private_key_length);
    }
    return result;
}
