/*
 * Every management service on native_sim, on real PSA providers (Issue #242). It starts the
 * composition and its services, then runs the cases that have no web route, through each service's
 * public interface with real sessions: the command line, the file service, the TLS certificate,
 * the network settings service, security events and status. Each prints one CASE line. Then the
 * HTTPS listener serves, and driver.py runs the web cases.
 *
 * Test only: the entropy is test-only, the listener's certificate key is public, and the first
 * account and the CLI's transport are declared test steps.
 */
#include "crux_native_management_services.h"
#if defined(CRUX_NATIVE_MANAGEMENT_SSH)
#include "crux_native_management_ssh.h"
#endif

#include <crux/cli.h>
#include <crux/file_service.h>
#include <crux/management_status.h>
#include <crux/network_settings.h>
#include <crux/security_event.h>
#include <crux/tls_certificate.h>
#include <crux_access_adapter.h>
#include <crux_cli_adapter.h>
#include <crux_tls_certificate_adapter.h>

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define VIEWER "viewer1"
#define GUEST "guest1"
#define FIRST_PASSWORD "first password of an account"
#define PASSWORD "a password changed at first login"

static crux_access_session_t admin, viewer, guest;
/* A session that was valid and has ended: what a client holds after a logout. */
static crux_access_session_t ended;
static unsigned failures;

static void report(const char *name, bool passed, const char *detail)
{
    printk("CASE %s %s%s%s\n", name, passed ? "ok" : "FAIL", detail[0] != '\0' ? " " : "", detail);
    failures += passed ? 0u : 1u;
}

static bool account_ready(const char *user, const char *role, crux_access_session_t *session)
{
    uint32_t retry_after_ms = 0;
    crux_access_result_t created =
        crux_access_account_create(admin, user, role, FIRST_PASSWORD, strlen(FIRST_PASSWORD));
    if (created == CRUX_ACCESS_OK) {
        return crux_native_management_login(user, FIRST_PASSWORD, session) == CRUX_ACCESS_OK &&
               crux_access_account_change_password(*session, FIRST_PASSWORD, strlen(FIRST_PASSWORD),
                                                   PASSWORD, strlen(PASSWORD),
                                                   &retry_after_ms) == CRUX_ACCESS_OK;
    }
    return created == CRUX_ACCESS_ERR_ALREADY_EXISTS &&
           crux_native_management_login(user, PASSWORD, session) == CRUX_ACCESS_OK;
}

static bool sessions_ready(void)
{
    if (crux_native_management_store_state_at_start() == CRUX_CREDENTIAL_STATE_EMPTY &&
        crux_native_management_provision_test_account() != CRUX_ACCESS_OK) {
        return false;
    }
    crux_access_session_t ending = {0};
    if (crux_native_management_login(CRUX_NATIVE_MANAGEMENT_TEST_USER,
                                     CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD,
                                     &admin) != CRUX_ACCESS_OK ||
        !account_ready(VIEWER, CRUX_NATIVE_MANAGEMENT_ROLE_VIEWER, &viewer) ||
        !account_ready(GUEST, CRUX_NATIVE_MANAGEMENT_ROLE_GUEST, &guest) ||
        crux_native_management_login(GUEST, PASSWORD, &ending) != CRUX_ACCESS_OK) {
        return false;
    }
    /* Keep the handle as a client would, and end the session behind it. */
    ended = ending;
    crux_access_logout(&ending);
    return true;
}

static size_t count_events(crux_security_event_id_t id)
{
    crux_security_event_record_t record;
    uint32_t cursor = 0;
    size_t count = 0;
    while (crux_security_event_read(admin, &cursor, &record) == CRUX_SECURITY_READ_OK) {
        count += record.id == id;
    }
    return count;
}

/* The CLI's transport for these cases: output into a buffer. The serial console over Crux Serial is
 * not composed on native_sim, so this stands in for its transport and nothing else. */
static char cli_output[1024];
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

static crux_cli_result_t cli(crux_access_session_t session, const char *line, int *status)
{
    static crux_cli_context_t context;
    static const crux_cli_io_t io = {.read = NULL, .write = cli_write, .transport = NULL};
    cli_used = 0;
    cli_output[0] = '\0';
    return crux_cli_exec(&context, &io, CRUX_ACCESS_CHANNEL_SERIAL, session, line, strlen(line),
                         status);
}

static void case_cli(void)
{
    int status = -1;
    size_t rejected = count_events(CRUX_SECURITY_EVENT_ID_CLI_LINE_REJECTED);
    bool permitted = cli(admin, "account list", &status) == CRUX_CLI_OK && status == 0 &&
                     strstr(cli_output, VIEWER) != NULL;
    report("cli_permitted", permitted, "account list as the administrator");
    int viewer_status = -1;
    bool unpermitted = cli(viewer, "account list", &viewer_status) == CRUX_CLI_OK &&
                       viewer_status != 0 && strstr(cli_output, GUEST) == NULL;
    char detail[48];
    (void)snprintf(detail, sizeof(detail), "exit %d, no account named", viewer_status);
    report("cli_unpermitted", unpermitted, detail);
    report("cli_unauthenticated", cli(ended, "help", &status) == CRUX_CLI_ERR_DENIED,
           "an ended session is refused");
    /* An unknown command is rejected and recorded, without the line. */
    bool unknown = cli(admin, "no-such-command", &status) == CRUX_CLI_OK && status == 127;
    k_sleep(K_MSEC(50));
    report("cli_line_rejected_event",
           unknown && count_events(CRUX_SECURITY_EVENT_ID_CLI_LINE_REJECTED) == rejected + 1u,
           "cli.line_rejected through crux_cli_security_events");
    /* The recovery command of the network settings service is registered, serial only, and with no
     * Configuration provider it says that nothing can be saved. A session without network.write
     * does not see it. */
    int recover = -1;
    (void)cli(admin, "network recover", &recover);
    bool said = strstr(cli_output, "nothing can be saved on this device") != NULL;
    int hidden = -1;
    (void)cli(guest, "network recover", &hidden);
    (void)snprintf(detail, sizeof(detail), "administrator exit %d, guest exit %d", recover, hidden);
    report("cli_network_recover", recover == 0 && said && hidden == 127, detail);
}

static void case_file_service(void)
{
    static const char content[] = "a file written through the file service";
    crux_file_service_transfer_t *transfer = NULL;
    size_t written = 0;
    /* A later boot finds the file of the first. */
    (void)crux_file_service_remove(admin, CRUX_NATIVE_MANAGEMENT_FILE_AREA, "note.txt");
    size_t created = count_events(CRUX_SECURITY_EVENT_ID_FILE_CREATED);
    bool permitted = crux_file_service_open(admin, CRUX_NATIVE_MANAGEMENT_FILE_AREA, "note.txt",
                                            CRUX_FILE_SERVICE_OPEN_MANAGED, strlen(content),
                                            &transfer) == CRUX_FILE_SERVICE_OK &&
                     crux_file_service_write(admin, transfer, 0u, content, strlen(content),
                                             &written) == CRUX_FILE_SERVICE_OK &&
                     written == strlen(content) &&
                     crux_file_service_commit(admin, &transfer) == CRUX_FILE_SERVICE_OK;
    if (transfer != NULL) {
        (void)crux_file_service_close(&transfer);
    }
    k_sleep(K_MSEC(50));
    report("file_permitted",
           permitted && count_events(CRUX_SECURITY_EVENT_ID_FILE_CREATED) == created + 1u,
           "a managed write and commit, and file.created");

    crux_file_info_t stat;
    bool viewer_reads = crux_file_service_stat(viewer, CRUX_NATIVE_MANAGEMENT_FILE_AREA, "note.txt",
                                               &stat) == CRUX_FILE_SERVICE_OK;
    bool viewer_refused = crux_file_service_open(viewer, CRUX_NATIVE_MANAGEMENT_FILE_AREA,
                                                 "other.txt", CRUX_FILE_SERVICE_OPEN_MANAGED, 1u,
                                                 &transfer) == CRUX_FILE_SERVICE_ERR_DENIED;
    bool guest_hidden = crux_file_service_stat(guest, CRUX_NATIVE_MANAGEMENT_FILE_AREA, "note.txt",
                                               &stat) == CRUX_FILE_SERVICE_ERR_NOT_FOUND;
    report("file_unpermitted", viewer_reads && viewer_refused && guest_hidden,
           "the viewer reads and may not write, the guest does not see the area");
    report("file_unauthenticated",
           crux_file_service_stat(ended, CRUX_NATIVE_MANAGEMENT_FILE_AREA, "note.txt", &stat) ==
               CRUX_FILE_SERVICE_ERR_SESSION_INVALID,
           "an ended session");
}

static void case_tls_certificate(void)
{
    static uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX];
    crux_tls_certificate_info_t info;
    char fingerprint[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    size_t length = 0;
    bool permitted =
        crux_tls_certificate_get(admin, &info) == CRUX_TLS_CERTIFICATE_OK &&
        crux_tls_certificate_fingerprint(fingerprint) == CRUX_TLS_CERTIFICATE_OK &&
        strcmp(fingerprint, info.fingerprint_text) == 0 &&
        info.source == CRUX_TLS_CERTIFICATE_SOURCE_SELF_SIGNED && info.no_expiry &&
        crux_tls_certificate_copy(admin, der, sizeof(der), &length) == CRUX_TLS_CERTIFICATE_OK;
    report("tls_certificate_permitted", permitted, "get, fingerprint and copy");
    report("tls_certificate_unpermitted",
           crux_tls_certificate_get(viewer, &info) == CRUX_TLS_CERTIFICATE_ERR_DENIED,
           "the viewer holds no tls.manage");
    report("tls_certificate_unauthenticated",
           crux_tls_certificate_get(ended, &info) == CRUX_TLS_CERTIFICATE_ERR_SESSION_INVALID,
           "an ended session");
    printk("CERTIFICATE generated=%d fingerprint=%s\n",
           crux_native_management_services_certificate_generated() ? 1 : 0, fingerprint);
    /* The DER certificate is public. driver.py checks it with OpenSSL. */
    printk("CERTIFICATE der=");
    for (size_t index = 0; index < length; ++index) {
        printk("%02x", der[index]);
    }
    printk("\n");
}

static void case_network_settings(void)
{
    crux_network_state_t state;
    crux_network_boot_report_t boot;
    crux_network_settings_boot_report(&boot);
    report("network_permitted",
           crux_network_settings_get(viewer, &state) == CRUX_NETWORK_OK && !state.has_running,
           "the no-op apply port reports no running values");
    report("network_unpermitted",
           crux_network_settings_get(guest, &state) == CRUX_NETWORK_ERR_DENIED, "the guest");
    report("network_unauthenticated",
           crux_network_settings_get(ended, &state) == CRUX_NETWORK_ERR_SESSION_INVALID,
           "an ended session");
    report("network_boot", boot.reason == CRUX_NETWORK_BOOT_NOT_SUPPORTED && !boot.persistence,
           "no Configuration provider, decision C6");
}

/* A configuration update records its event, and status reports the counts. */
static void case_config_event_and_status(void)
{
    static const char value[] = "services image";
    crux_security_event_record_t record;
    crux_security_event_stats_t stats;
    uint32_t cursor = 0;
    crux_config_service_item_t applied;
    bool seen = false;
    bool updated = crux_config_service_update(admin, CRUX_NATIVE_MANAGEMENT_PARAM_NAME, value,
                                              strlen(value), &applied) == CRUX_CONFIG_SERVICE_OK;
    k_sleep(K_MSEC(1500));
    while (crux_security_event_read(admin, &cursor, &record) == CRUX_SECURITY_READ_OK) {
        seen |= record.id == CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED &&
                strcmp(record.user, CRUX_NATIVE_MANAGEMENT_TEST_USER) == 0 &&
                strcmp(record.subject, CRUX_NATIVE_MANAGEMENT_PARAM_NAME) == 0;
    }
    report("config_update_event", updated && seen, "config.updated with its user and parameter");
    crux_mgmt_status_entry_t entry;
    size_t status_cursor = 0;
    bool counted = false;
    (void)crux_security_event_get_stats(&stats);
    while (crux_mgmt_status_next(admin, &status_cursor, &entry) == CRUX_OK) {
        counted |= strcmp(entry.name, "security.events_emitted") == 0 && entry.status == CRUX_OK &&
                   entry.value.data.u32 >= stats.emitted && stats.emitted > 0u;
    }
    report("status_event_counts", counted && stats.dropped == 0u,
           "security.events_emitted at least the emitted count, nothing dropped");
}

int main(void)
{
    crux_native_management_start_t started = crux_native_management_start();
    if (started != CRUX_NATIVE_MANAGEMENT_STARTED || !sessions_ready()) {
        printk("CRUX_SERVICES_START_FAILED composition %d\n", (int)started);
        return 1;
    }
    crux_native_management_services_start_t services = crux_native_management_services_start();
    if (services != CRUX_NATIVE_MANAGEMENT_SERVICES_STARTED) {
        printk("CRUX_SERVICES_START_FAILED services %d\n", (int)services);
        return 1;
    }
#if defined(CRUX_NATIVE_MANAGEMENT_SSH)
    /* Issue #197: SSH beside the HTTPS listener. Started before the cases, so that they run with
     * its listener, threads and sessions present, and any interference shows in them. */
    static const char *const ssh_users[] = {CRUX_NATIVE_MANAGEMENT_TEST_USER, VIEWER};
    if (!crux_native_management_ssh_add_keys(admin, ssh_users, ARRAY_SIZE(ssh_users))) {
        printk("CRUX_SERVICES_START_FAILED ssh key\n");
        return 1;
    }
    crux_native_management_ssh_start_t ssh = crux_native_management_ssh_start();
    if (ssh != CRUX_NATIVE_MANAGEMENT_SSH_STARTED) {
        printk("CRUX_SERVICES_START_FAILED ssh %d\n", (int)ssh);
        return 1;
    }
    printk("SSH_READY %d\n", CONFIG_CRUX_SSH_PORT);
#endif
    case_cli();
    case_file_service();
    case_tls_certificate();
    case_network_settings();
    case_config_event_and_status();
#if defined(CRUX_NATIVE_MANAGEMENT_SSH)
    /* The cases' three sessions are held for the life of the services image, and each takes one of
     * the core's network session slots, which HTTPS and SSH share. They end here, so that the slots
     * that a client of this image finds are the ones a deployment would have.
     * docs/ssh-integrated-image.md has the numbers. */
    crux_access_logout(&admin);
    crux_access_logout(&viewer);
    crux_access_logout(&guest);
#endif
    printk("CRUX_SERVICES_CASES failures=%u\n", failures);
    printk("CRUX_SERVICES_READY %d\n", CONFIG_CRUX_WEB_PORT);
#if defined(CRUX_NATIVE_MANAGEMENT_SSH)
    /* The image keeps running for the clients on the host. The main thread reports what the run has
     * used every second, for the driver to read: simulation results only. */
    for (;;) {
        k_msleep(1000);
        crux_native_management_ssh_report();
    }
#endif
    return 0;
}
