#include "crux_native_management_services.h"
#include "entropy_guard.h"

#include "crux_web_management.h"
#include "crux_web_session.h"

#include <crux/cli.h>
#include <crux/network_settings.h>
#include <crux/network_settings_cli.h>
#include <crux_tls_certificate_adapter.h>

#include <zephyr/sys/util.h>

static const char *const dns_names[] = {"localhost"};
static const uint8_t ipv4_names[][4] = {{127, 0, 0, 1}};
static const crux_tls_certificate_options_t certificate_options = {
    .names = {.common_name = "crux native_sim test",
              .dns_names = dns_names,
              .dns_count = ARRAY_SIZE(dns_names),
              .ipv4 = ipv4_names,
              .ipv4_count = ARRAY_SIZE(ipv4_names)},
    .not_before_s = CRUX_TLS_CERTIFICATE_DEFAULT_NOT_BEFORE_S,
    .validity_days = 0u,
};
static bool certificate_generated;

static crux_file_root_t reserved_roots[2];
#if defined(CRUX_NATIVE_MANAGEMENT_SSH)
/* SFTP writes directly, and a managed-only area refuses every SFTP write (docs/sftp-design.md), so
 * the area of the SSH composition allows both modes, with room for a transfer of a few tens of
 * kilobytes. The disk's partition is 512 KiB, and the store holds two slots of it. */
static crux_file_area_t areas[] = {{
    .name = CRUX_NATIVE_MANAGEMENT_FILE_AREA,
    .read_permission = CRUX_NATIVE_MANAGEMENT_PERMISSION_FILE_READ,
    .write_permission = CRUX_NATIVE_MANAGEMENT_PERMISSION_FILE_WRITE,
    .max_file_bytes = 65536u,
    .max_area_bytes = 262144u,
    .max_entries = 16u,
    .partial_suffix = ".filepart",
    .write_modes = CRUX_FILE_SERVICE_MANAGED | CRUX_FILE_SERVICE_DIRECT,
}};
#else
static crux_file_area_t areas[] = {{
    .name = CRUX_NATIVE_MANAGEMENT_FILE_AREA,
    .read_permission = CRUX_NATIVE_MANAGEMENT_PERMISSION_FILE_READ,
    .write_permission = CRUX_NATIVE_MANAGEMENT_PERMISSION_FILE_WRITE,
    .max_file_bytes = 4096u,
    .max_area_bytes = 16384u,
    .max_entries = 8u,
    .partial_suffix = NULL,
    .write_modes = CRUX_FILE_SERVICE_MANAGED,
}};
#endif

static bool file_service_ready(void)
{
    static crux_file_service_config_t config;

    areas[0].root = crux_native_management_file_root();
    crux_native_management_store_roots(reserved_roots);
    config = (crux_file_service_config_t){
        .areas = areas,
        .area_count = ARRAY_SIZE(areas),
        .reserved_roots = reserved_roots,
        .reserved_root_count = ARRAY_SIZE(reserved_roots),
        .no_credential_store = false,
    };
    return crux_file_service_start(&config) == CRUX_FILE_SERVICE_OK;
}

/* The built-in commands, and the serial recovery command of the network settings service. */
static bool cli_ready(void)
{
    static const crux_cli_command_t *tables[1];
    static size_t counts[1];

    tables[0] = crux_network_settings_cli_commands(&counts[0]);
    return tables[0] != NULL && crux_cli_init(tables, counts, ARRAY_SIZE(tables)) == CRUX_CLI_OK;
}

crux_native_management_services_start_t crux_native_management_services_start(void)
{
    /* The TLS certificate needs the store and Access, which the composition has started. */
    crux_tls_certificate_start_t how = CRUX_TLS_CERTIFICATE_STARTED;
    if (crux_tls_certificate_start(&certificate_options, &how) != CRUX_TLS_CERTIFICATE_OK) {
        return CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_TLS_CERTIFICATE;
    }
    certificate_generated = how == CRUX_TLS_CERTIFICATE_GENERATED;
    if (crux_network_settings_start() != CRUX_NETWORK_OK ||
        crux_network_settings_start_worker() != CRUX_NETWORK_OK) {
        return CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_NETWORK;
    }
    if (!file_service_ready()) {
        return CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_FILE;
    }
    if (!cli_ready()) {
        return CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_CLI;
    }
    /* HTTPS only: the session module refuses "http" in a build like this one. */
    static const crux_web_session_config_t sessions = {"https", NULL, 0u};
    if (crux_web_session_init(&sessions) != CRUX_WEB_OK ||
        crux_web_session_start_worker() != CRUX_WEB_OK) {
        return CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_SESSIONS;
    }
    if (crux_web_management_init() != CRUX_WEB_OK ||
        crux_web_management_start_worker() != CRUX_WEB_OK) {
        return CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_ROUTES;
    }
    if (crux_native_management_tls_test_credentials(CONFIG_CRUX_WEB_TLS_SEC_TAG) != 0) {
        return CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_LISTENER_CREDENTIAL;
    }
    static const crux_web_config_t web = {
        .authenticate = crux_web_session_authenticate,
        .request_deadline_ms = CONFIG_CRUX_WEB_REQUEST_DEADLINE_MS,
        .handler_limit_ms = 100u,
    };
    if (crux_web_start(&web) != CRUX_WEB_OK) {
        return CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_WEB;
    }
    return CRUX_NATIVE_MANAGEMENT_SERVICES_STARTED;
}

bool crux_native_management_services_certificate_generated(void)
{
    return certificate_generated;
}
