/* The fixture of the service suites: the real Access core with its fake ports, and the fakes. */
#ifndef TLS_CERTIFICATE_SERVICE_TEST_H
#define TLS_CERTIFICATE_SERVICE_TEST_H

#include "fakes.h"
#include "fixtures.h"
#include "tls_test.h"

#include <crux/tls_certificate.h>
#include <crux_access_adapter.h>
#include <crux_tls_certificate_adapter.h>

/* From the instrumented core, built with CRUX_TLS_CERTIFICATE_TESTING. */
void crux_tls_certificate_test_reset(void);
bool crux_tls_certificate_test_ram_holds(const uint8_t *bytes, size_t length);
bool crux_tls_certificate_test_key_cleared(void);

/* From tests/access, which these suites link. */
extern unsigned access_test_failures;
extern const crux_access_origin_t access_test_web;
void access_test_boot(const crux_access_policy_t *policy);
crux_access_session_t access_test_session(const char *user, const char *password,
                                          const crux_access_origin_t *origin);
#define FIXTURE_PASSWORD "correct horse battery"

/* Logged in by fixture_boot(). */
extern crux_access_session_t s_admin;    /* access.manage and tls.manage. */
extern crux_access_session_t s_operator; /* tls.manage alone. */
extern crux_access_session_t s_viewer;   /* Something else. */

/* The names the fixture starts with. */
extern const crux_tls_certificate_options_t fixture_options;

/* Boots Access and logs in, with fresh fakes. Does not start the module. */
void fixture_boot(void);
/* The above, then a start that must generate. */
void fixture_start(void);
/* A power cycle: the module's RAM is lost, the fake store is kept. */
crux_tls_certificate_result_t fixture_restart(crux_tls_certificate_start_t *how);

/* The active certificate, through the public API. */
size_t active_der(uint8_t der[CRUX_TLS_CERTIFICATE_DER_MAX]);
/* A stored record of this module's form around der. */
size_t record_of(crux_tls_certificate_source_t source, const uint8_t *der, size_t length,
                 uint8_t *record);

#endif
