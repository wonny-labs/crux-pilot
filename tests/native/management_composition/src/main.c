/*
 * The composition's own test: login, status, configuration and events through the real services, on
 * real PSA providers, a real FatFs on a file-backed disk and the Crux Logging provider, with the
 * TLS stack in the same image. scripts/test-native-composition.sh runs this image more than once on
 * one disk, and the store's state at start selects the suite: a first boot, a later boot, or a boot
 * with another device ID. Each run prints what the script compares between runs.
 */
#include "crux_native_management.h"
#include "test_tls_credentials.h"
#if defined(CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
#include "crux_tf_psa_threading.h"
#endif

#include <crux/file.h>
#include <crux/management_status.h>
#include <crux/security_event.h>
#include <crux_access_adapter.h>
#include <crux_credential_adapter.h>

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#define VIEWER "viewer1"
#define OPERATOR "operator1"
#define GUEST "guest1"
#define FIRST_PASSWORD "first password of an account"
#define CHANGED_PASSWORD "a password changed at first login"
#define WRONG_PASSWORD "this is the wrong password"
#define SECRET_VALUE "secret-config-value"
#define OPERATOR_NAME "composition-device"
#define SLOT_FILE "store.img"
#define SLOT_BYTES 16384u
#define ROOT_SLOT_A 1
#define ROOT_SLOT_B 2

static crux_access_session_t admin, viewer, operator, guest;
static const crux_access_session_t never_valid = {0};

/* A first login, and the change of password that an account's owner makes. */
static void login_with_changed_password(const char *user, crux_access_session_t *session)
{
    uint32_t retry_after_ms = 0;
    zassert_equal(crux_native_management_login(user, FIRST_PASSWORD, session), CRUX_ACCESS_OK);
    zassert_equal(crux_access_account_change_password(*session, FIRST_PASSWORD,
                                                      strlen(FIRST_PASSWORD), CHANGED_PASSWORD,
                                                      strlen(CHANGED_PASSWORD), &retry_after_ms),
                  CRUX_ACCESS_OK);
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

static void print_public_key(const char *label, crux_credential_record_t which)
{
    uint8_t record[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t length = 0;
    crux_credential_key_view_t view;
    zassert_equal(crux_credential_read(which, record, sizeof(record), &length), CRUX_CREDENTIAL_OK);
    zassert_equal(crux_credential_key_parse(record, length, &view), CRUX_CREDENTIAL_OK);
    printk("COMPOSITION %s type=%d public=", label, (int)view.type);
    for (size_t index = 0; index < view.public_length; ++index) {
        printk("%02x", view.public_key[index]);
    }
    printk("\n");
    memset(record, 0, sizeof(record));
}

static void let_the_drain_run(void *fixture);

static bool first_boot(const void *state)
{
    ARG_UNUSED(state);
    return crux_native_management_store_state_at_start() == CRUX_CREDENTIAL_STATE_EMPTY;
}

static bool later_boot(const void *state)
{
    ARG_UNUSED(state);
    return crux_native_management_store_state_at_start() == CRUX_CREDENTIAL_STATE_VALID;
}

static bool other_device(const void *state)
{
    ARG_UNUSED(state);
    return crux_native_management_store_state_at_start() == CRUX_CREDENTIAL_STATE_UNUSABLE;
}

/* The declaration, as simulation and not as a device property: an ID that is not secret, on a store
 * that is not confidential. */
ZTEST(first_boot, test_01_declaration)
{
    crux_credential_store_info_t info;
    zassert_equal(crux_credential_store_get_info(&info), CRUX_OK);
    zassert_equal(info.basis, CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY);
    zassert_equal(info.evidence, CRUX_CREDENTIAL_EVIDENCE_SOFTWARE);
    zassert_false(info.confidential_against_medium_reader);
    zassert_true(info.forgery_detected_without_device_id);
    zassert_false(info.rollback_detected);
    /* The device key the profile's listener asks for was made at start. */
    zassert_equal(crux_credential_state(), CRUX_CREDENTIAL_STATE_VALID);
    print_public_key("tls_key", CRUX_CREDENTIAL_RECORD_TLS_KEY);
}

/* Only wolfPSA generates Ed25519. On the Mbed profile the port can say so only as ERR_ENTROPY, and
 * that profile provides no SSH, so nothing asks for it there. */
ZTEST(first_boot, test_02_ssh_host_key_by_profile)
{
#if defined(CONFIG_WOLFPSA)
    print_public_key("ssh_host_key", CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY);
#else
    uint8_t record[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t length = 0;
    zassert_equal(crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY,
                                             CRUX_CREDENTIAL_KEY_ED25519),
                  CRUX_CREDENTIAL_ERR_ENTROPY);
    zassert_equal(
        crux_credential_read(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, record, sizeof(record), &length),
        CRUX_CREDENTIAL_ERR_NOT_FOUND);
#endif
}

ZTEST(first_boot, test_03_login)
{
    crux_access_session_t refused = {0};
    zassert_equal(crux_native_management_login(CRUX_NATIVE_MANAGEMENT_TEST_USER,
                                               CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD, &refused),
                  CRUX_ACCESS_ERR_DENIED, "no account yet");
    zassert_equal(crux_native_management_provision_test_account(), CRUX_ACCESS_OK);
    zassert_equal(crux_native_management_provision_test_account(), CRUX_ACCESS_ERR_DENIED,
                  "provisioning closes once an account exists");

    zassert_equal(
        crux_native_management_login(CRUX_NATIVE_MANAGEMENT_TEST_USER, WRONG_PASSWORD, &refused),
        CRUX_ACCESS_ERR_DENIED);
    zassert_equal(crux_native_management_login("nobody", WRONG_PASSWORD, &refused),
                  CRUX_ACCESS_ERR_DENIED);
    zassert_equal(crux_native_management_login(CRUX_NATIVE_MANAGEMENT_TEST_USER,
                                               CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD, &admin),
                  CRUX_ACCESS_OK);

    zassert_equal(crux_access_account_create(admin, VIEWER, CRUX_NATIVE_MANAGEMENT_ROLE_VIEWER,
                                             FIRST_PASSWORD, strlen(FIRST_PASSWORD)),
                  CRUX_ACCESS_OK);
    zassert_equal(crux_access_account_create(admin, OPERATOR, CRUX_NATIVE_MANAGEMENT_ROLE_OPERATOR,
                                             FIRST_PASSWORD, strlen(FIRST_PASSWORD)),
                  CRUX_ACCESS_OK);
    zassert_equal(crux_access_account_create(admin, GUEST, CRUX_NATIVE_MANAGEMENT_ROLE_GUEST,
                                             FIRST_PASSWORD, strlen(FIRST_PASSWORD)),
                  CRUX_ACCESS_OK);
    let_the_drain_run(NULL);
    login_with_changed_password(VIEWER, &viewer);
    login_with_changed_password(OPERATOR, &operator);
    login_with_changed_password(GUEST, &guest);
    zassert_equal(crux_access_check(operator, CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_WRITE),
                  CRUX_ACCESS_OK);

    /* Permitted, unpermitted and unauthenticated, on Access itself. */
    zassert_equal(crux_access_check(viewer, CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_WRITE),
                  CRUX_ACCESS_ERR_DENIED);
    zassert_equal(crux_access_check(never_valid, CRUX_MGMT_STATUS_PERMISSION_READ),
                  CRUX_ACCESS_ERR_SESSION_INVALID);
}

ZTEST(first_boot, test_04_status)
{
    static char document[4096];
    size_t length = 0;
    /* Permitted. */
    zassert_equal(crux_mgmt_status_report_json(viewer, document, sizeof(document), &length),
                  CRUX_OK);
    zassert_not_null(strstr(document, "\"system.uptime_ms\""));
    zassert_not_null(strstr(document, "\"security.events_emitted\""));
    zassert_not_null(strstr(document,
                            "{\"name\":\"security.credential_state\",\"status\":\"ok\",\"type\":"
                            "\"text\",\"value\":\"valid\"}"));
    zassert_not_null(strstr(document, "\"value\":\"device_id_key\""));
    zassert_not_null(strstr(document, "\"value\":\"software\""));
    zassert_not_null(strstr(document, "\"net.admin_up\""));
    printk("COMPOSITION status %s\n", document);
    /* Unauthenticated. */
    zassert_equal(crux_mgmt_status_report_json(never_valid, document, sizeof(document), &length),
                  CRUX_ERR_INVALID_STATE);
    /* Unpermitted: the items are hidden, and nothing else tells. */
    zassert_equal(crux_mgmt_status_report_json(guest, document, sizeof(document), &length),
                  CRUX_OK);
    zassert_equal(strcmp(document, "{\"items\":[]}"), 0, "%s", document);
}

ZTEST(first_boot, test_05_configuration)
{
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    size_t length = 0;
    crux_config_type_t type;
    crux_config_service_item_t applied;
    crux_config_service_save_report_t report;
    const char *name = CRUX_NATIVE_MANAGEMENT_PARAM_NAME;

    /* Unauthenticated. */
    zassert_equal(crux_config_service_read(never_valid, name, text, sizeof(text), &length, &type),
                  CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    zassert_equal(crux_config_service_update(never_valid, name, OPERATOR_NAME,
                                             strlen(OPERATOR_NAME), &applied),
                  CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID);
    /* Unpermitted: a parameter this session may not see looks missing. */
    zassert_equal(crux_config_service_read(viewer, name, text, sizeof(text), &length, &type),
                  CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    zassert_equal(
        crux_config_service_update(viewer, name, OPERATOR_NAME, strlen(OPERATOR_NAME), &applied),
        CRUX_CONFIG_SERVICE_ERR_NOT_FOUND);
    /* Permitted, and what it reads back. */
    zassert_equal(
        crux_config_service_update(operator, name, OPERATOR_NAME, strlen(OPERATOR_NAME), &applied),
        CRUX_CONFIG_SERVICE_OK);
    zassert_equal(applied.activation, CRUX_CONFIG_ACTIVATION_APPLICATION);
    zassert_equal(crux_config_service_read(operator, name, text, sizeof(text), &length, &type),
                  CRUX_CONFIG_SERVICE_OK);
    zassert_equal(type, CRUX_CONFIG_STRING);
    zassert_mem_equal(text, OPERATOR_NAME, strlen(OPERATOR_NAME));
    /* Text that does not parse keeps the old value. */
    zassert_equal(crux_config_service_update(operator, CRUX_NATIVE_MANAGEMENT_PARAM_INTERVAL, "x",
                                             1u, &applied),
                  CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT);
    /* A sensitive value goes in and never comes out. */
    zassert_equal(crux_config_service_update(operator, CRUX_NATIVE_MANAGEMENT_PARAM_TOKEN,
                                             SECRET_VALUE, strlen(SECRET_VALUE), &applied),
                  CRUX_CONFIG_SERVICE_OK);
    zassert_equal(crux_config_service_read(operator, CRUX_NATIVE_MANAGEMENT_PARAM_TOKEN, text,
                                           sizeof(text), &length, &type),
                  CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY);
    /* Saving needs its own permission, and this composition has no save provider: decision C6. */
    zassert_equal(crux_config_service_save(operator, & report), CRUX_CONFIG_SERVICE_ERR_DENIED);
    zassert_equal(crux_config_service_save(admin, &report), CRUX_CONFIG_SERVICE_ERR_STORE);
    zassert_equal(report.failed_result, CRUX_CONFIG_ERR_NOT_SUPPORTED);
}

ZTEST(first_boot, test_06_events)
{
    crux_security_event_record_t record;
    crux_security_event_stats_t stats;
    uint32_t cursor = 0;
    bool updated_seen = false;
    /* Unauthenticated, and unpermitted. */
    zassert_equal(crux_security_event_read(never_valid, &cursor, &record),
                  CRUX_SECURITY_READ_ERR_SESSION_INVALID);
    zassert_equal(crux_security_event_read(viewer, &cursor, &record),
                  CRUX_SECURITY_READ_ERR_DENIED);
    /* Permitted: a configuration update records its event, with who and from where. */
    while (crux_security_event_read(admin, &cursor, &record) == CRUX_SECURITY_READ_OK) {
        /* No record holds a value or a password, in any field. */
        zassert_is_null(strstr(record.subject, "secret"));
        zassert_is_null(strstr(record.subject, "password"));
        if (record.id == CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED &&
            strcmp(record.subject, CRUX_NATIVE_MANAGEMENT_PARAM_NAME) == 0) {
            updated_seen = true;
            zassert_equal(record.result, CRUX_SECURITY_EVENT_OK);
            zassert_equal(strcmp(record.user, OPERATOR), 0, "%s", record.user);
            zassert_equal(record.channel, CRUX_ACCESS_CHANNEL_WEB);
            zassert_equal(record.source_length, 4u);
            zassert_equal(record.source[0], 192u);
        }
    }
    zassert_true(updated_seen);
    zassert_equal(count_events(admin, CRUX_SECURITY_EVENT_ID_ACCESS_PROVISIONED), 1u);
    /* Before provisioning, a wrong password and an unknown name. */
    zassert_equal(count_events(admin, CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED), 3u);
    zassert_equal(count_events(admin, CRUX_SECURITY_EVENT_ID_CONFIG_REJECTED), 1u);
    /* Saving stops at the first failure, and the service keeps every mark. */
    zassert_equal(count_events(admin, CRUX_SECURITY_EVENT_ID_CONFIG_SAVE_FAILED), 1u);
    zassert_equal(count_events(admin, CRUX_SECURITY_EVENT_ID_CREDENTIAL_KEY_GENERATED),
                  IS_ENABLED(CONFIG_WOLFPSA) ? 2u : 1u);
    /* The drain ran on the Logging provider, and status reports the same counts. */
    k_sleep(K_MSEC(1500));
    zassert_equal(crux_security_event_get_stats(&stats), CRUX_OK);
    zassert_true(stats.emitted > 0u);
    zassert_equal(stats.dropped, 0u);
    zassert_true(stats.delivered > 0u, "the drain delivered nothing");
    zassert_equal(stats.log_errors, 0u);

    crux_mgmt_status_entry_t entry;
    size_t status_cursor = 0;
    bool counted = false;
    while (crux_mgmt_status_next(admin, &status_cursor, &entry) == CRUX_OK) {
        if (strcmp(entry.name, "security.events_emitted") == 0) {
            zassert_equal(entry.status, CRUX_OK);
            /* Emits go on, so the count taken later cannot be smaller. */
            zassert_true(entry.value.data.u32 >= stats.emitted);
            counted = true;
        }
    }
    zassert_true(counted);
    printk("COMPOSITION events emitted=%u delivered=%u dropped=%u log_errors=%u high_water=%u\n",
           stats.emitted, stats.delivered, stats.dropped, stats.log_errors, stats.high_water);
}

static bool holds(const uint8_t *image, size_t length, const char *text)
{
    size_t text_length = strlen(text);
    for (size_t offset = 0; offset + text_length <= length; ++offset) {
        if (memcmp(image + offset, text, text_length) == 0) {
            return true;
        }
    }
    return false;
}

/* What a reader of the medium sees: the real format, sealed. This shows only that the records are
 * not plain, because someone with the device ID can open them. */
ZTEST(first_boot, test_07_medium)
{
    static uint8_t image[SLOT_BYTES];
    for (crux_file_root_t root = ROOT_SLOT_A; root <= ROOT_SLOT_B; ++root) {
        crux_file_t *file = NULL;
        size_t total = 0;
        zassert_equal(crux_file_open_at(root, SLOT_FILE, CRUX_FILE_READ, &file), CRUX_FILE_OK);
        while (total < sizeof(image)) {
            size_t count = 0;
            zassert_equal(crux_file_read(file, image + total, sizeof(image) - total, &count),
                          CRUX_FILE_OK);
            if (count == 0u) {
                break;
            }
            total += count;
        }
        zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
        zassert_equal(total, SLOT_BYTES);
        zassert_mem_equal(image, "CXCS", 4u);
        /* The protection identifier is the declared basis. */
        zassert_equal(image[6], (uint8_t)CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY);
        zassert_equal(image[7], 0u);
        zassert_false(holds(image, total, CRUX_NATIVE_MANAGEMENT_TEST_USER));
        zassert_false(holds(image, total, OPERATOR));
        zassert_false(holds(image, total, CHANGED_PASSWORD));
    }
}

/* A TLS socket beside the services: a server and a client in this image, over the offload, on one
 * PSA provider, with a login, a keyed hash and a store write while the connection is held. The real
 * client on the host is in tests/native/tls_offload_probe, and this shows only that they coexist.
 */
#define TLS_SERVER_TAG 41
#define TLS_CLIENT_TAG 42
#define TLS_PORT 14222
static K_THREAD_STACK_DEFINE(server_stack, 24576);
static struct k_thread server_thread;
static K_SEM_DEFINE(server_listening, 0, 1);
static volatile int server_result = -1;
static uint8_t tls_buffer[256];

static void tls_server(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    static const sec_tag_t tags[] = {TLS_SERVER_TAG};
    static const int reuse = 1;
    struct net_sockaddr_in address = {.sin_family = NET_AF_INET, .sin_port = net_htons(TLS_PORT)};
    zsock_inet_pton(NET_AF_INET, "127.0.0.1", &address.sin_addr);
    int listener = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TLS_1_2);
    if (listener < 0 ||
        zsock_setsockopt(listener, ZSOCK_SOL_SOCKET, ZSOCK_SO_REUSEADDR, &reuse, sizeof(reuse)) <
            0 ||
        zsock_setsockopt(listener, ZSOCK_SOL_TLS, ZSOCK_TLS_SEC_TAG_LIST, tags, sizeof(tags)) < 0 ||
        zsock_bind(listener, (struct net_sockaddr *)&address, sizeof(address)) < 0 ||
        zsock_listen(listener, 1) < 0) {
        server_result = -errno;
        k_sem_give(&server_listening);
        return;
    }
    k_sem_give(&server_listening);
    int client = zsock_accept(listener, NULL, NULL);
    if (client < 0) {
        server_result = -errno;
    } else {
        ssize_t received = zsock_recv(client, tls_buffer, sizeof(tls_buffer), 0);
        server_result =
            received > 0 && zsock_send(client, tls_buffer, received, 0) == received ? 0 : -EIO;
        zsock_close(client);
    }
    zsock_close(listener);
}

ZTEST(first_boot, test_08_tls_beside_the_services)
{
    static const char message[] = "beside the services";
    static const sec_tag_t tags[] = {TLS_CLIENT_TAG};
    static uint8_t received[64];
    crux_access_session_t beside = {0};
    int required = ZSOCK_TLS_PEER_VERIFY_REQUIRED;
#if defined(CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
    uint32_t locks_at_start = crux_tf_psa_threading_lock_count();
#endif

    zassert_equal(crux_native_management_tls_test_credentials(TLS_SERVER_TAG), 0);
    zassert_equal(tls_credential_add(TLS_CLIENT_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
                                     crux_test_tls_certificate, sizeof(crux_test_tls_certificate)),
                  0);

    k_thread_create(&server_thread, server_stack, K_THREAD_STACK_SIZEOF(server_stack), tls_server,
                    NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
    zassert_equal(k_sem_take(&server_listening, K_SECONDS(10)), 0);
    zassert_equal(server_result, -1, "the listener failed: %d", server_result);

    struct net_sockaddr_in address = {.sin_family = NET_AF_INET, .sin_port = net_htons(TLS_PORT)};
    zsock_inet_pton(NET_AF_INET, "127.0.0.1", &address.sin_addr);
    int socket = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TLS_1_2);
    zassert_true(socket >= 0, "errno %d", errno);
    zassert_ok(zsock_setsockopt(socket, ZSOCK_SOL_TLS, ZSOCK_TLS_SEC_TAG_LIST, tags, sizeof(tags)));
    zassert_ok(zsock_setsockopt(socket, ZSOCK_SOL_TLS, ZSOCK_TLS_HOSTNAME, "localhost",
                                sizeof("localhost")));
    zassert_ok(zsock_setsockopt(socket, ZSOCK_SOL_TLS, ZSOCK_TLS_PEER_VERIFY, &required,
                                sizeof(required)));
    zassert_ok(zsock_connect(socket, (struct net_sockaddr *)&address, sizeof(address)), "errno %d",
               errno);

    /* The Access and store providers, while TLS holds its own keys in the same provider. */
    zassert_equal(crux_native_management_login(VIEWER, CHANGED_PASSWORD, &beside), CRUX_ACCESS_OK);
    crux_access_logout(&beside);
    zassert_equal(crux_access_account_create(admin, "beside1", CRUX_NATIVE_MANAGEMENT_ROLE_VIEWER,
                                             FIRST_PASSWORD, strlen(FIRST_PASSWORD)),
                  CRUX_ACCESS_OK);

    zassert_equal(zsock_send(socket, message, sizeof(message), 0), (ssize_t)sizeof(message));
    zassert_equal(zsock_recv(socket, received, sizeof(received), 0), (ssize_t)sizeof(message));
    zassert_mem_equal(received, message, sizeof(message));
    zsock_close(socket);
    zassert_equal(k_thread_join(&server_thread, K_SECONDS(10)), 0);
    zassert_equal(server_result, 0, "%d", server_result);
#if defined(CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
    /* Both a Crux provider and TLS go through the core's locks. No race can show here: a native_sim
     * thread that does not sleep is never preempted. */
    zassert_true(crux_tf_psa_threading_lock_count() > locks_at_start);
    printk("COMPOSITION tf_psa_core_locks=%u\n", crux_tf_psa_threading_lock_count());
#endif
    printk("COMPOSITION first boot complete\n");
}

/* The drain runs at a low priority, and a native_sim thread that does not sleep lets nothing else
 * run, so a burst with no pause would fill the ring. A real client's requests leave it room. */
static void let_the_drain_run(void *fixture)
{
    ARG_UNUSED(fixture);
    k_sleep(K_MSEC(600));
}

ZTEST_SUITE(first_boot, first_boot, NULL, NULL, let_the_drain_run, NULL);

/* What the first boot wrote comes back through the real provider, from a file on the host. */
ZTEST(later_boot, test_01_accounts_and_keys_survive)
{
    crux_access_session_t session = {0};
    crux_credential_store_info_t info;
    zassert_equal(crux_credential_store_get_info(&info), CRUX_OK);
    zassert_equal(info.state, CRUX_CREDENTIAL_STATE_VALID);
    zassert_equal(crux_native_management_provision_test_account(), CRUX_ACCESS_ERR_DENIED,
                  "provisioning stays closed");
    zassert_equal(
        crux_native_management_login(CRUX_NATIVE_MANAGEMENT_TEST_USER, WRONG_PASSWORD, &session),
        CRUX_ACCESS_ERR_DENIED);
    zassert_equal(crux_native_management_login(CRUX_NATIVE_MANAGEMENT_TEST_USER,
                                               CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD, &session),
                  CRUX_ACCESS_OK);
    zassert_equal(crux_native_management_login(VIEWER, CHANGED_PASSWORD, &viewer), CRUX_ACCESS_OK);
    /* The key from the first boot, and no new one. */
    print_public_key("tls_key", CRUX_CREDENTIAL_RECORD_TLS_KEY);
    if (IS_ENABLED(CONFIG_WOLFPSA)) {
        print_public_key("ssh_host_key", CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY);
    }
    zassert_equal(count_events(session, CRUX_SECURITY_EVENT_ID_CREDENTIAL_KEY_GENERATED), 0u);
    /* A save on this boot, with this boot's nonce, reads back. */
    zassert_equal(
        crux_access_account_reset_password(session, VIEWER, FIRST_PASSWORD, strlen(FIRST_PASSWORD)),
        CRUX_ACCESS_OK);
    zassert_equal(crux_access_account_reset_password(session, VIEWER, CHANGED_PASSWORD,
                                                     strlen(CHANGED_PASSWORD)),
                  CRUX_ACCESS_OK);
    printk("COMPOSITION later boot complete\n");
}

ZTEST_SUITE(later_boot, later_boot, NULL, NULL, NULL, NULL);

/* The same disk with another device ID: the tag fails, and everything stays closed. */
ZTEST(other_device, test_01_everything_stays_closed)
{
    crux_access_session_t session = {0};
    crux_credential_store_info_t info;
    uint8_t record[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    size_t length = 0;
    crux_file_info_t slot;
    zassert_equal(crux_credential_store_get_info(&info), CRUX_OK);
    zassert_equal(info.state, CRUX_CREDENTIAL_STATE_UNUSABLE);
    zassert_equal(
        crux_credential_read(CRUX_CREDENTIAL_RECORD_TLS_KEY, record, sizeof(record), &length),
        CRUX_CREDENTIAL_ERR_UNUSABLE);
    zassert_equal(crux_native_management_login(CRUX_NATIVE_MANAGEMENT_TEST_USER,
                                               CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD, &session),
                  CRUX_ACCESS_ERR_NOT_READY);
    zassert_equal(crux_native_management_provision_test_account(), CRUX_ACCESS_ERR_DENIED,
                  "an unusable store never opens provisioning");
    zassert_equal(
        crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_TLS_KEY, CRUX_CREDENTIAL_KEY_ECDSA_P256),
        CRUX_CREDENTIAL_ERR_UNUSABLE);
    /* Nothing was rewritten: the next boot with the right ID still opens them. */
    zassert_equal(crux_file_stat(ROOT_SLOT_A, SLOT_FILE, &slot), CRUX_FILE_OK);
    zassert_equal(slot.size, SLOT_BYTES);
    printk("COMPOSITION other device complete\n");
}

ZTEST_SUITE(other_device, other_device, NULL, NULL, NULL, NULL);

void test_main(void)
{
    crux_native_management_start_t started = crux_native_management_start();
    printk("COMPOSITION started=%d store_at_start=%d profile=%s\n", (int)started,
           (int)crux_native_management_store_state_at_start(),
           IS_ENABLED(CONFIG_WOLFPSA) ? "wolf" : "mbed");
    if (started != CRUX_NATIVE_MANAGEMENT_STARTED) {
        ztest_test_fail();
    }
    ztest_run_all(NULL, false, 1, 1);
}
