/*
 * The management services on native_sim, with real PSA providers, for a test to run on. Test only:
 * the entropy is test-only, the device ID is an identifier, and the first account and the TLS
 * certificate are declared inputs. Nothing here is a statement about a device. See
 * docs/native-management-composition.md.
 */
#ifndef CRUX_NATIVE_MANAGEMENT_H
#define CRUX_NATIVE_MANAGEMENT_H

#include <crux/access.h>
#include <crux/config_service.h>
#include <crux/credential_store.h>

#include <stdbool.h>

#define CRUX_NATIVE_MANAGEMENT_ROLE_ADMINISTRATOR "administrator"
#define CRUX_NATIVE_MANAGEMENT_ROLE_OPERATOR "operator"
#define CRUX_NATIVE_MANAGEMENT_ROLE_VIEWER "viewer"
#define CRUX_NATIVE_MANAGEMENT_ROLE_GUEST "guest"
/* The reference table. A sensitive value is write-only through the service. */
#define CRUX_NATIVE_MANAGEMENT_PARAM_NAME "demo.display_name"
#define CRUX_NATIVE_MANAGEMENT_PARAM_INTERVAL "demo.interval_ms"
#define CRUX_NATIVE_MANAGEMENT_PARAM_TOKEN "demo.token"
#define CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_READ "config.read"
#define CRUX_NATIVE_MANAGEMENT_PERMISSION_CONFIG_WRITE "config.write"
/* Added only by the services composition of Issue #242, services.cmake. */
#define CRUX_NATIVE_MANAGEMENT_PERMISSION_FILE_READ "file.read"
#define CRUX_NATIVE_MANAGEMENT_PERMISSION_FILE_WRITE "file.write"
#define CRUX_NATIVE_MANAGEMENT_PERMISSION_TLS_MANAGE "tls.manage"

/* The first account: the declared test step, until the serial console of #181 is composed. */
#define CRUX_NATIVE_MANAGEMENT_TEST_USER "admin"
#define CRUX_NATIVE_MANAGEMENT_TEST_PASSWORD "composition test password"

/* Where each step of the start stopped, or 0 once every service runs. */
typedef enum {
    CRUX_NATIVE_MANAGEMENT_STARTED = 0,
    CRUX_NATIVE_MANAGEMENT_FAILED_EVENTS,
    CRUX_NATIVE_MANAGEMENT_FAILED_TIME,
    CRUX_NATIVE_MANAGEMENT_FAILED_MEDIUM,
    CRUX_NATIVE_MANAGEMENT_FAILED_ACCESS,
    CRUX_NATIVE_MANAGEMENT_FAILED_CONFIG,
    CRUX_NATIVE_MANAGEMENT_FAILED_STATUS,
    CRUX_NATIVE_MANAGEMENT_FAILED_EVENT_THREAD
} crux_native_management_start_t;

/*
 * Boots the services in the order the contracts state, from one thread. It mounts the simulated
 * disk, formatting a blank one, because this medium belongs to the test. An unusable store is not a
 * start failure: it fails closed, and the services report it.
 */
crux_native_management_start_t crux_native_management_start(void);

#if defined(CRUX_NATIVE_MANAGEMENT_SERVICES)
#include <crux/file.h>
/* The root of the file service's one area, bound on the same disk. Services composition only. */
crux_file_root_t crux_native_management_file_root(void);
/* The store's two roots, which no file area may use. */
void crux_native_management_store_roots(crux_file_root_t roots[2]);
#endif

/* What the store said at start, which is how a test tells a first boot from a later one. */
crux_credential_state_t crux_native_management_store_state_at_start(void);

/* The declared test step. An origin of serial, as provisioning requires. */
crux_access_result_t crux_native_management_provision_test_account(void);

/* A session from a web origin, source 192.0.2.1, or the login's refusal. */
crux_access_result_t crux_native_management_login(const char *user, const char *password,
                                                  crux_access_session_t *session);

/*
 * Registers the declared test certificate and its private key under this tag, for TLS sockets. See
 * tests/native/tls_offload_probe/credentials/README.md.
 */
int crux_native_management_tls_test_credentials(int sec_tag);

#endif
