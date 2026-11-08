/*
 * The core under a clock that has failed, which the Time provider of the clock port reports as 0.
 * This holds what it does, limits included, so that changing them is a decision. See
 * docs/native-management-composition.md#what-each-port-maps.
 */
#include "access_test.h"

#define NOW_MS (CRUX_ACCESS_DELAY_QUIET_MS + 5000u)

static const char wrong[] = "a wrong password for this";

static void start(void)
{
    access_fake_reset();
    crux_access_test_reset();
    access_fake_clock_set(NOW_MS);
    CHECK(crux_access_init(&access_test_profile_reference.policy) == CRUX_ACCESS_OK);
    CHECK(crux_access_provision_first_account(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD,
                                              strlen(ACCESS_TEST_PASSWORD),
                                              access_test_profile_reference.administrator_role,
                                              &access_test_serial) == CRUX_ACCESS_OK);
}

static crux_access_result_t login(const char *password, crux_access_session_t *session)
{
    uint32_t retry_after_ms = 0;
    return crux_access_login_password(ACCESS_TEST_ADMIN, password, strlen(password),
                                      &access_test_web, session, &retry_after_ms);
}

static void a_session_from_before_the_fault_expires(void)
{
    crux_access_session_t session = {0};
    start();
    CHECK(login(ACCESS_TEST_PASSWORD, &session) == CRUX_ACCESS_OK);
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_OK);
    access_fake_clock_set(0);
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
}

static void a_window_stays_closed_during_the_fault(void)
{
    crux_access_session_t session = {0};
    start();
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        CHECK(login(wrong, &session) == CRUX_ACCESS_ERR_DENIED);
    }
    CHECK(login(ACCESS_TEST_PASSWORD, &session) == CRUX_ACCESS_ERR_RETRY_LATER);
    access_fake_clock_set(0);
    CHECK(login(ACCESS_TEST_PASSWORD, &session) == CRUX_ACCESS_ERR_RETRY_LATER);
}

/* A limit: this session has no real age while the clock stays broken. */
static void a_login_during_the_fault_stays_valid_while_it_lasts(void)
{
    crux_access_session_t session = {0};
    start();
    access_fake_clock_set(0);
    CHECK(login(ACCESS_TEST_PASSWORD, &session) == CRUX_ACCESS_OK);
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_OK);
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_OK);
    /* It expires when the clock returns, because it then looks old. */
    access_fake_clock_set(NOW_MS);
    CHECK(crux_access_check(session, "status.read") == CRUX_ACCESS_ERR_SESSION_INVALID);
}

/* A limit: failures during the fault date the bucket to 0, so the clock's return clears it, a
 * moment after its real failures, and an attacker gets the free attempts again. */
static void failures_during_the_fault_shrink_and_then_clear_a_bucket(void)
{
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    start();
    for (unsigned attempt = 0; attempt < 3u; ++attempt) {
        CHECK(login(wrong, &session) == CRUX_ACCESS_ERR_DENIED);
    }
    access_fake_clock_set(0);
    for (unsigned attempt = 0; attempt < 3u; ++attempt) {
        CHECK(login(wrong, &session) == CRUX_ACCESS_ERR_DENIED);
    }
    /* The window this opened is shorter than the one the same count would have at the real time. */
    CHECK(crux_access_login_password(ACCESS_TEST_ADMIN, wrong, strlen(wrong), &access_test_web,
                                     &session, &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms > 0u && retry_after_ms <= CRUX_ACCESS_DELAY_BASE_MS);
    access_fake_clock_set(NOW_MS + 1u);
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        CHECK(login(wrong, &session) == CRUX_ACCESS_ERR_DENIED);
    }
}

int main(void)
{
    a_session_from_before_the_fault_expires();
    a_window_stays_closed_during_the_fault();
    a_login_during_the_fault_stays_valid_while_it_lasts();
    failures_during_the_fault_shrink_and_then_clear_a_bucket();
    return access_test_finish("access under a failed clock");
}
