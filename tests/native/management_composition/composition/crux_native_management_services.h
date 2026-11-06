/*
 * The rest of the management services on the native_sim composition, on the same real PSA
 * providers (Issue #242): web sessions and the status, configuration and network routes over HTTPS,
 * the network settings service, the TLS certificate module, the command line and the file service.
 * Test only, as crux_native_management.h states. See docs/native-management-composition.md.
 */
#ifndef CRUX_NATIVE_MANAGEMENT_SERVICES_H
#define CRUX_NATIVE_MANAGEMENT_SERVICES_H

#include "crux_native_management.h"

#include <crux/file_service.h>

/* The file service's one area, on the composition's disk. */
#define CRUX_NATIVE_MANAGEMENT_FILE_AREA "files"

/* Where the start of these services stopped, or 0 once every one runs. */
typedef enum {
    CRUX_NATIVE_MANAGEMENT_SERVICES_STARTED = 0,
    CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_TLS_CERTIFICATE,
    CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_NETWORK,
    CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_FILE,
    CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_CLI,
    CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_SESSIONS,
    CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_ROUTES,
    CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_LISTENER_CREDENTIAL,
    CRUX_NATIVE_MANAGEMENT_SERVICES_FAILED_WEB
} crux_native_management_services_start_t;

/*
 * Starts the services above, in the order their contracts state, after
 * crux_native_management_start() and the first account, from one thread. The listener is HTTPS
 * only, and serves the declared test certificate of tests/native/tls_offload_probe/credentials, as
 * the HTTPS composition of #185 does. The TLS certificate module starts on the store's key and its
 * PSA port, and its material is not handed to the listener: see the design document.
 */
crux_native_management_services_start_t crux_native_management_services_start(void);

/* How the TLS certificate module started, valid after a successful start. */
bool crux_native_management_services_certificate_generated(void);

#endif
