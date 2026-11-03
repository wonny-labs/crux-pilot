/* Shared helpers for the Access test suites. */
#ifndef CRUX_ACCESS_TEST_H
#define CRUX_ACCESS_TEST_H

#include "access_internal.h"
#include "fake_ports.h"

#include <stdio.h>
#include <string.h>

extern unsigned access_test_checks, access_test_failures;

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++access_test_checks;                                                                      \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            ++access_test_failures;                                                                \
        }                                                                                          \
    } while (0)

/* Reports the totals, and is the suite's exit status. */
int access_test_finish(const char *suite);

/* Available only in the instrumented core built for tests with CRUX_ACCESS_TESTING. */
void crux_access_test_reset(void);
void crux_access_test_set_generation(size_t slot, uint32_t generation);

/* A product arrangement the conformance suite runs against. */
typedef struct {
    crux_access_policy_t policy;
    const char *administrator_role;
    const char *limited_role;
    const char *other_limited_role;
    const char *granted_to_limited;
    const char *not_granted_to_limited;
    const char *unknown_permission;
    const char *password;
    const char *other_password;
    const char *rejected_password;
} access_test_profile_t;

extern const access_test_profile_t access_test_profile_reference;
extern const access_test_profile_t access_test_profile_product;

extern const crux_access_origin_t access_test_serial;
extern const crux_access_origin_t access_test_web;
extern const crux_access_origin_t access_test_web_other;
extern const crux_access_origin_t access_test_ssh;

/* The password, 12 bytes or more, that the suites' reference policy accepts. */
#define ACCESS_TEST_PASSWORD "correct horse battery"
#define ACCESS_TEST_OTHER_PASSWORD "another long password"
#define ACCESS_TEST_ADMIN "admin"
#define ACCESS_TEST_USER "alice"

/* An SSH Ed25519 public key blob whose 32 key bytes all hold one value. Use it as an initialiser. */
#define ACCESS_TEST_KEY_8(b) b, b, b, b, b, b, b, b
#define ACCESS_TEST_ED25519_KEY(b)                                                                 \
    {0,                                                                                            \
     0,                                                                                            \
     0,                                                                                            \
     11,                                                                                           \
     's',                                                                                          \
     's',                                                                                          \
     'h',                                                                                          \
     '-',                                                                                          \
     'e',                                                                                          \
     'd',                                                                                          \
     '2',                                                                                          \
     '5',                                                                                          \
     '5',                                                                                          \
     '1',                                                                                          \
     '9',                                                                                          \
     0,                                                                                            \
     0,                                                                                            \
     0,                                                                                            \
     32,                                                                                           \
     ACCESS_TEST_KEY_8(b),                                                                         \
     ACCESS_TEST_KEY_8(b),                                                                         \
     ACCESS_TEST_KEY_8(b),                                                                         \
     ACCESS_TEST_KEY_8(b)}

/* Powers on with an empty medium, and initialises. */
void access_test_boot(const crux_access_policy_t *policy);
/* Simulates a reset: RAM is lost, and the medium and clock survive. Initialises again. */
void access_test_power_cycle(const crux_access_policy_t *policy);
/* Boots with the reference policy, and provisions ACCESS_TEST_ADMIN. */
void access_test_boot_with_admin(void);
crux_access_result_t access_test_login(const char *user, const char *password,
                                       const crux_access_origin_t *origin,
                                       crux_access_session_t *session);
/* Logs in and expects success. */
crux_access_session_t access_test_session(const char *user, const char *password,
                                          const crux_access_origin_t *origin);
/* Makes ACCESS_TEST_USER with the reference policy's operator role. */
void access_test_create_user(crux_access_session_t admin);
/* A web origin whose source differs for each value, to avoid the source delay. */
crux_access_origin_t access_test_web_from(uint32_t value);

#endif
