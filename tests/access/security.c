/* Account discovery, the delay rules with a controlled clock, verification slots and logins racing
 * account changes. */
#include "access_test.h"

#define ADMIN ACCESS_TEST_ADMIN
#define USER ACCESS_TEST_USER
#define PASSWORD ACCESS_TEST_PASSWORD
#define OTHER_PASSWORD ACCESS_TEST_OTHER_PASSWORD
#define WRONG "wrong-password-here"

static const crux_access_policy_t *reference(void)
{
    return &access_test_profile_reference.policy;
}

/* Fails from a source of its own, so that only the name's bucket can delay the next attempt. */
static crux_access_result_t failure_from_new_source(const char *user, uint32_t *source)
{
    crux_access_origin_t origin = access_test_web_from(*source);
    *source += 1;
    crux_access_session_t session = {0};
    return access_test_login(user, WRONG, &origin, &session);
}

typedef struct {
    crux_access_result_t result;
    uint32_t session_value;
    uint32_t retry_after_ms;
    unsigned derivations;
    uint64_t iterations;
    size_t events;
    crux_access_event_type_t event_type;
    crux_access_result_t event_result;
} observation_t;

static observation_t observe(const char *user, const char *password,
                             const crux_access_origin_t *origin)
{
    observation_t observed = {0};
    crux_access_session_t session = {0};
    access_fake_derive_counters_clear();
    access_fake_events_clear();
    observed.result = crux_access_login_password(user, password, strlen(password), origin, &session,
                                                 &observed.retry_after_ms);
    observed.session_value = session.value;
    observed.derivations = access_fake_derive_count();
    observed.iterations = access_fake_derive_iterations();
    observed.events = access_fake_event_count();
    if (observed.events > 0) {
        observed.event_type = access_fake_event(0)->type;
        observed.event_result = access_fake_event(0)->result;
    }
    return observed;
}

static void expect_same_observation(const observation_t *left, const observation_t *right)
{
    CHECK(left->result == right->result);
    CHECK(left->session_value == right->session_value);
    CHECK(left->retry_after_ms == right->retry_after_ms);
    CHECK(left->derivations == right->derivations);
    CHECK(left->iterations == right->iterations);
    CHECK(left->events == right->events);
    CHECK(left->event_type == right->event_type);
    CHECK(left->event_result == right->event_result);
}

static void unknown_and_disabled_match_wrong_password(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_web);
    access_test_create_user(admin);
    CHECK(crux_access_account_create(admin, "disabled", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_disable(admin, "disabled") == CRUX_ACCESS_OK);

    /* A fresh source each time, so the observations cannot differ through delay state. */
    crux_access_origin_t origin = access_test_web_from(1);
    observation_t wrong = observe(USER, WRONG, &origin);
    CHECK(wrong.result == CRUX_ACCESS_ERR_DENIED);
    CHECK(wrong.derivations == 1);
    CHECK(wrong.iterations == CRUX_ACCESS_VERIFIER_ITERATIONS);
    CHECK(wrong.session_value == 0);

    origin = access_test_web_from(2);
    observation_t unknown = observe("no-such-user", WRONG, &origin);
    expect_same_observation(&wrong, &unknown);
    origin = access_test_web_from(3);
    observation_t disabled = observe("disabled", WRONG, &origin);
    expect_same_observation(&wrong, &disabled);
    /* The right password for a disabled account is no different. */
    origin = access_test_web_from(4);
    observation_t disabled_right = observe("disabled", PASSWORD, &origin);
    expect_same_observation(&wrong, &disabled_right);

    /* Events name a known account, and never a name no account has. */
    origin = access_test_web_from(5);
    (void)observe("disabled", WRONG, &origin);
    CHECK(strcmp(access_fake_event(0)->account, "disabled") == 0);
    origin = access_test_web_from(6);
    (void)observe("my-password-typed-here", WRONG, &origin);
    CHECK(access_fake_event(0)->account[0] == '\0');
    CHECK(access_fake_event(0)->type == CRUX_ACCESS_EVENT_LOGIN_FAILURE);

    /* A dummy match always fails, here with a derivation that really does match. */
    crux_access_session_t none = {0};
    origin = access_test_web_from(7);
    access_fake_derive_force(crux_access_dummy_derived);
    observation_t dummy_match = observe("no-such-user", WRONG, &origin);
    access_fake_derive_force(NULL);
    expect_same_observation(&wrong, &dummy_match);
    CHECK(none.value == 0);

    /* A role removed by a firmware update denies login like a wrong password, and the event says
     * so. A policy that still holds the administrator's role, but not the operator's. */
    static const char *const manage[] = {CRUX_ACCESS_PERMISSION_MANAGE};
    static const crux_access_role_t reduced_roles[] = {{"administrator", manage, 1}};
    static const crux_access_policy_t reduced = {reduced_roles, 1, 900, 28800, NULL};
    access_test_power_cycle(&reduced);
    origin = access_test_web_from(8);
    observation_t role_gone = observe(USER, PASSWORD, &origin);
    /* A client sees the same as for a wrong password, and only the private event says why. */
    CHECK(role_gone.event_type == CRUX_ACCESS_EVENT_LOGIN_REFUSED);
    role_gone.event_type = wrong.event_type;
    expect_same_observation(&wrong, &role_gone);
    CHECK(access_fake_event(0)->role_missing);
    CHECK(strcmp(access_fake_event(0)->account, USER) == 0);
    /* Such an account is not an administrator for the last-administrator rule. */
    admin = access_test_session(ADMIN, PASSWORD, &access_test_web);
    CHECK(crux_access_account_disable(admin, ADMIN) == CRUX_ACCESS_ERR_LAST_ADMINISTRATOR);
}

static void table_filling_probe(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_web);
    access_test_create_user(admin);
    CHECK(crux_access_account_create(admin, "carol", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(admin, "dave", "viewer", PASSWORD, strlen(PASSWORD)) ==
          CRUX_ACCESS_OK);
    /* Fill the seven network slots, which three accounts can do. */
    const char *const fillers[] = {USER, "carol", "dave"};
    for (size_t index = 0; index < 6; ++index) {
        (void)access_test_session(fillers[index % 3], PASSWORD, &access_test_web);
    }

    /* Real and unknown names, right and wrong passwords: all equal, and none verified. */
    const char *const names[] = {USER, "no-such-user", ADMIN};
    const char *const passwords[] = {PASSWORD, WRONG};
    observation_t first = {0};
    for (size_t name = 0; name < 3; ++name) {
        for (size_t password = 0; password < 2; ++password) {
            crux_access_origin_t origin = access_test_web_from((uint32_t)(name * 2u + password));
            observation_t observed = observe(names[name], passwords[password], &origin);
            CHECK(observed.result == CRUX_ACCESS_ERR_NO_RESOURCES);
            CHECK(observed.derivations == 0);
            if (name == 0 && password == 0) {
                first = observed;
            }
            expect_same_observation(&first, &observed);
        }
    }
    /* It never counted as a failure, so nothing is delayed afterwards. */
    crux_access_session_t serial = access_test_session(ADMIN, PASSWORD, &access_test_serial);
    CHECK(crux_access_check(serial, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
}

static void delay_window_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;

    /* Free attempts set no window. */
    uint32_t source = 1;
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS; ++attempt) {
        CHECK(failure_from_new_source(ADMIN, &source) == CRUX_ACCESS_ERR_DENIED);
    }
    /* Each further failure doubles the window, from the base up to the cap. */
    const uint32_t windows_ms[] = {1000, 2000, 4000, 8000, 16000, 32000, 60000, 60000, 60000};
    for (size_t index = 0; index < sizeof(windows_ms) / sizeof(windows_ms[0]); ++index) {
        crux_access_origin_t origin = access_test_web_from(source);
        CHECK(failure_from_new_source(ADMIN, &source) == CRUX_ACCESS_ERR_DENIED);

        /* Inside a window nothing is verified, counted or extended. */
        access_fake_derive_counters_clear();
        unsigned first_check =
            crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &origin, &session,
                                       &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER;
        CHECK(first_check);
        CHECK(retry_after_ms == windows_ms[index]);
        CHECK(access_fake_derive_count() == 0);
        access_fake_clock_advance(windows_ms[index] - 1u);
        CHECK(crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &origin, &session,
                                         &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
        CHECK(retry_after_ms == 1);
        CHECK(session.value == 0);
        access_fake_clock_advance(1);
    }
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_DELAY_APPLIED) > 0);

    /* The window has ended, and a right password succeeds and clears the name bucket. */
    crux_access_origin_t origin = access_test_web_from(source);
    source += 1;
    CHECK(crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &origin, &session,
                                     &retry_after_ms) == CRUX_ACCESS_OK);
    CHECK(retry_after_ms == 0);
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS; ++attempt) {
        CHECK(failure_from_new_source(ADMIN, &source) == CRUX_ACCESS_ERR_DENIED);
    }
    crux_access_origin_t still_free = access_test_web_from(source);
    CHECK(crux_access_login_password(ADMIN, WRONG, strlen(WRONG), &still_free, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_login_password(ADMIN, WRONG, strlen(WRONG), &still_free, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
}

static void decay_cases(void)
{
    access_test_boot_with_admin();
    uint32_t source = 1;
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 3u; ++attempt) {
        (void)failure_from_new_source(ADMIN, &source);
        access_fake_clock_advance(CRUX_ACCESS_DELAY_CAP_MS);
    }
    /* Just inside the quiet period the count stands, so a further failure sets a longer window. */
    access_fake_clock_advance(CRUX_ACCESS_DELAY_QUIET_MS - CRUX_ACCESS_DELAY_CAP_MS - 1u);
    CHECK(failure_from_new_source(ADMIN, &source) == CRUX_ACCESS_ERR_DENIED);
    crux_access_origin_t origin = access_test_web_from(source);
    CHECK(crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &origin, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == 8000);

    /* A whole quiet period with no failure returns the bucket to zero and restores free attempts.
     */
    access_fake_clock_advance(CRUX_ACCESS_DELAY_QUIET_MS);
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS; ++attempt) {
        CHECK(failure_from_new_source(ADMIN, &source) == CRUX_ACCESS_ERR_DENIED);
    }
    origin = access_test_web_from(source);
    source += 1;
    CHECK(crux_access_login_password(ADMIN, WRONG, strlen(WRONG), &origin, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_DENIED);
    CHECK(retry_after_ms == 0);
}

/* Guesses one name for a day, waiting a fixed time after each failure and retrying as soon as
 * allowed. Returns the number of guesses actually verified. */
static unsigned guess_for_a_day(uint64_t wait_after_failure_ms)
{
    const uint64_t day_ms = 24ull * 3600ull * 1000ull;
    access_test_boot_with_admin();
    const uint64_t start_ms = crux_access_port_now_ms();
    unsigned guesses = 0;
    uint32_t source = 0;
    while (crux_access_port_now_ms() - start_ms < day_ms) {
        crux_access_session_t session = {0};
        uint32_t retry_after_ms = 0;
        crux_access_origin_t origin = access_test_web_from(source / 16u);
        source += 1;
        crux_access_result_t result = crux_access_login_password(
            ADMIN, WRONG, strlen(WRONG), &origin, &session, &retry_after_ms);
        if (result == CRUX_ACCESS_ERR_DENIED) {
            guesses += 1;
            access_fake_clock_advance(wait_after_failure_ms);
        } else {
            CHECK(result == CRUX_ACCESS_ERR_RETRY_LATER);
            CHECK(retry_after_ms > 0);
            access_fake_clock_advance(retry_after_ms);
        }
    }
    return guesses;
}

/* Spends the free attempts and the short windows, then waits for the bucket to return to zero. */
static unsigned guess_in_bursts_for_a_day(void)
{
    const uint64_t day_ms = 24ull * 3600ull * 1000ull;
    access_test_boot_with_admin();
    const uint64_t start_ms = crux_access_port_now_ms();
    unsigned guesses = 0;
    unsigned in_burst = 0;
    while (crux_access_port_now_ms() - start_ms < day_ms) {
        crux_access_session_t session = {0};
        uint32_t retry_after_ms = 0;
        crux_access_origin_t origin = access_test_web_from(1);
        crux_access_result_t result = crux_access_login_password(
            ADMIN, WRONG, strlen(WRONG), &origin, &session, &retry_after_ms);
        if (result == CRUX_ACCESS_ERR_RETRY_LATER) {
            access_fake_clock_advance(retry_after_ms);
            continue;
        }
        CHECK(result == CRUX_ACCESS_ERR_DENIED);
        guesses += 1;
        in_burst += 1;
        if (in_burst == CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 6u) {
            in_burst = 0;
            access_fake_clock_advance(CRUX_ACCESS_DELAY_QUIET_MS);
        }
    }
    return guesses;
}

static void guessing_bound_cases(void)
{
    /* The proposal bounds sustained guessing to about 1,500 attempts a day per bucket. */
    const uint64_t waits_ms[] = {0, 1000, 60000, 120000, 600000, 899999, 900000, 1000000};
    unsigned most = 0;
    for (size_t index = 0; index < sizeof(waits_ms) / sizeof(waits_ms[0]); ++index) {
        unsigned guesses = guess_for_a_day(waits_ms[index]);
        printf("Waiting %llu ms after each failure: %u guesses in a day\n",
               (unsigned long long)waits_ms[index], guesses);
        most = guesses > most ? guesses : most;
        CHECK(guesses <= 1500);
    }
    /* Staying under the cap, about one a minute, is the fastest strategy. */
    CHECK(most >= 1400);
    /* Waiting out the quiet period after a burst is the proposal's other strategy: about eleven
     * attempts in each period of about sixteen minutes, which is slower than the first. */
    unsigned bursts = guess_in_bursts_for_a_day();
    printf("Bursts of %u then a quiet period: %u guesses in a day\n",
           CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 6u, bursts);
    CHECK(bursts >= 900);
    CHECK(bursts <= most);
}

static void collision_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t admin = access_test_session(ADMIN, PASSWORD, &access_test_serial);
    access_test_create_user(admin);
    access_fake_hmac_fixed(true);
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    crux_access_origin_t first = access_test_web_from(1);

    /* With every name and source in one bucket, failures on one name delay the other. */
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        CHECK(access_test_login("ghost", WRONG, &first, &session) == CRUX_ACCESS_ERR_DENIED);
    }
    crux_access_origin_t second = access_test_web_from(2);
    CHECK(crux_access_login_password(USER, PASSWORD, strlen(PASSWORD), &second, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == CRUX_ACCESS_DELAY_BASE_MS);

    /* A valid account's login clears the name bucket it shares, but not the source delay. */
    access_fake_clock_advance(retry_after_ms);
    CHECK(access_test_login(USER, PASSWORD, &second, &session) == CRUX_ACCESS_OK);
    CHECK(access_test_login("ghost", WRONG, &first, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_login_password("ghost", WRONG, strlen(WRONG), &first, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == 2000);
}

static void source_not_cleared_by_success_cases(void)
{
    /* A valid account does not help an insider spray guesses at others. */
    access_test_boot_with_admin();
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    char name[16];
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        snprintf(name, sizeof(name), "ghost-%u", attempt);
        CHECK(access_test_login(name, WRONG, &access_test_web, &session) == CRUX_ACCESS_ERR_DENIED);
        access_fake_clock_advance(1);
    }
    CHECK(crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &access_test_web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    access_fake_clock_advance(retry_after_ms);
    CHECK(access_test_login(ADMIN, PASSWORD, &access_test_web, &session) == CRUX_ACCESS_OK);
    CHECK(access_test_login("ghost-next", WRONG, &access_test_web, &session) ==
          CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_login_password("ghost-more", WRONG, strlen(WRONG), &access_test_web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == 2000);
}

/* A web source that does not share the target source's bucket, so that only the name can delay it.
 */
static crux_access_origin_t source_outside_bucket_of(const crux_access_origin_t *target)
{
    static uint32_t next_source = 0x400000u;
    access_delay_buckets_t target_buckets;
    CHECK(crux_access_delay_buckets(target, NULL, &target_buckets));
    while (true) {
        crux_access_origin_t origin = access_test_web_from(next_source);
        next_source += 1;
        access_delay_buckets_t buckets;
        CHECK(crux_access_delay_buckets(&origin, NULL, &buckets));
        if (buckets.source != target_buckets.source) {
            return origin;
        }
    }
}

/* The window that a bucket's failure of this number sets, stepped over so that the next one counts.
 */
static void step_over_window(unsigned failure)
{
    if (failure > CRUX_ACCESS_DELAY_FREE_ATTEMPTS) {
        uint32_t doublings = failure - CRUX_ACCESS_DELAY_FREE_ATTEMPTS - 1u;
        access_fake_clock_advance((uint64_t)CRUX_ACCESS_DELAY_BASE_MS << doublings);
    }
}

/* Rotating names and sources must not reset another entry. Nothing is evicted, and attempts inside
 * the target's window do not count. So the target's exact remaining time survives the flood, while
 * the flood's own windows, at most a second each, have passed. */
static void flooding_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;

    /* A target source at a 32 s window, then a target name at an 8 s window. */
    const crux_access_origin_t *target_source = &access_test_web_other;
    char sprayed[16];
    for (unsigned failure = 1; failure <= 11; ++failure) {
        snprintf(sprayed, sizeof(sprayed), "spray-%u", failure);
        CHECK(access_test_login(sprayed, WRONG, target_source, &session) == CRUX_ACCESS_ERR_DENIED);
        if (failure < 11) {
            step_over_window(failure);
        }
    }
    for (unsigned failure = 1; failure <= 9; ++failure) {
        crux_access_origin_t origin = source_outside_bucket_of(target_source);
        CHECK(access_test_login(ADMIN, WRONG, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
        if (failure < 9) {
            step_over_window(failure);
        }
    }
    const uint32_t source_window_ms = 32000u - (1000u + 2000u + 4000u);
    const uint32_t name_window_ms = 8000u;

    /* Far more names and sources than there are buckets, with the clock stopped. */
    char name[16];
    for (uint32_t flood = 0; flood < 2000; ++flood) {
        snprintf(name, sizeof(name), "flood-%u", (unsigned)flood);
        crux_access_origin_t origin = access_test_web_from(0x10000u + flood);
        crux_access_result_t result = access_test_login(name, WRONG, &origin, &session);
        CHECK(result == CRUX_ACCESS_ERR_DENIED || result == CRUX_ACCESS_ERR_RETRY_LATER);
    }

    /* Past the flood's windows, and the target's own state is exactly what it was. */
    const uint32_t elapsed_ms = 2u * CRUX_ACCESS_DELAY_BASE_MS;
    access_fake_clock_advance(elapsed_ms);
    crux_access_origin_t clean_source = source_outside_bucket_of(target_source);
    CHECK(crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &clean_source, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == name_window_ms - elapsed_ms);
    CHECK(crux_access_login_password("clean-name", WRONG, strlen(WRONG), target_source, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == source_window_ms - elapsed_ms);
    CHECK(session.value == 0);
}

static void serial_independence_cases(void)
{
    access_test_boot_with_admin();
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    uint32_t source = 1;

    /* A network flood at the cap never affects the serial channel. */
    for (unsigned attempt = 0; attempt < 20; ++attempt) {
        crux_access_origin_t origin = access_test_web_from(source);
        source += 1;
        (void)access_test_login(ADMIN, WRONG, &origin, &session);
        access_fake_clock_advance(CRUX_ACCESS_DELAY_CAP_MS);
    }
    crux_access_origin_t origin = access_test_web_from(source);
    (void)access_test_login(ADMIN, WRONG, &origin, &session);
    CHECK(crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &access_test_web, &session,
                                     &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    crux_access_session_t serial = access_test_session(ADMIN, PASSWORD, &access_test_serial);
    CHECK(crux_access_check(serial, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);

    /* Serial has one bucket for the whole channel, whatever the name, and it delays only serial. */
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        CHECK(access_test_login(attempt % 2u == 0u ? ADMIN : "ghost", WRONG, &access_test_serial,
                                &session) == CRUX_ACCESS_ERR_DENIED);
    }
    CHECK(crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &access_test_serial,
                                     &session, &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
    CHECK(retry_after_ms == CRUX_ACCESS_DELAY_BASE_MS);
    access_fake_clock_advance(CRUX_ACCESS_DELAY_QUIET_MS);
    crux_access_origin_t quiet = access_test_web_from(0x200000u);
    (void)access_test_session(ADMIN, PASSWORD, &quiet);
    /* A serial success does not clear the serial bucket: only a name bucket is cleared by success.
     */
    for (unsigned attempt = 0; attempt < CRUX_ACCESS_DELAY_FREE_ATTEMPTS + 1u; ++attempt) {
        CHECK(access_test_login(ADMIN, WRONG, &access_test_serial, &session) ==
              CRUX_ACCESS_ERR_DENIED);
    }
    access_fake_clock_advance(CRUX_ACCESS_DELAY_BASE_MS);
    CHECK(access_test_login(ADMIN, PASSWORD, &access_test_serial, &session) == CRUX_ACCESS_OK);
    CHECK(access_test_login(ADMIN, WRONG, &access_test_serial, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), &access_test_serial,
                                     &session, &retry_after_ms) == CRUX_ACCESS_ERR_RETRY_LATER);
}

typedef struct {
    const crux_access_origin_t *origin;
    crux_access_result_t result;
    uint32_t retry_after_ms;
    crux_access_session_t session;
} nested_login_t;

static void hook_nested_login(void *context)
{
    nested_login_t *nested = context;
    nested->result = crux_access_login_password(ADMIN, PASSWORD, strlen(PASSWORD), nested->origin,
                                                &nested->session, &nested->retry_after_ms);
}

static void verification_slot_cases(void)
{
    access_test_boot_with_admin();

    /* The network channels share one slot, whichever source or channel is asking. */
    nested_login_t web = {&access_test_web_other, CRUX_ACCESS_ERR_NOT_READY, 0, {0}};
    access_fake_derive_hook(hook_nested_login, &web);
    (void)access_test_session(ADMIN, PASSWORD, &access_test_web);
    CHECK(web.result == CRUX_ACCESS_ERR_BUSY);
    CHECK(web.session.value == 0);
    nested_login_t ssh = {&access_test_ssh, CRUX_ACCESS_ERR_NOT_READY, 0, {0}};
    access_fake_derive_hook(hook_nested_login, &ssh);
    (void)access_test_session(ADMIN, PASSWORD, &access_test_web);
    CHECK(ssh.result == CRUX_ACCESS_ERR_BUSY);

    /* Serial has its own slot, so a network flood cannot stall the local recovery path. */
    nested_login_t serial = {&access_test_serial, CRUX_ACCESS_ERR_NOT_READY, 0, {0}};
    access_fake_derive_hook(hook_nested_login, &serial);
    (void)access_test_session(ADMIN, PASSWORD, &access_test_web);
    CHECK(serial.result == CRUX_ACCESS_OK);
    CHECK(crux_access_check(serial.session, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);

    /* And a serial verification does not hold up the network. */
    nested_login_t network = {&access_test_web, CRUX_ACCESS_ERR_NOT_READY, 0, {0}};
    access_fake_derive_hook(hook_nested_login, &network);
    (void)access_test_session(ADMIN, PASSWORD, &access_test_serial);
    CHECK(network.result == CRUX_ACCESS_OK);

    /* The slot is released after every kind of outcome. */
    crux_access_session_t none = {0};
    CHECK(access_test_login(ADMIN, WRONG, &access_test_web, &none) == CRUX_ACCESS_ERR_DENIED);
    access_fake_derive_fail(true);
    CHECK(access_test_login(ADMIN, PASSWORD, &access_test_web, &none) == CRUX_ACCESS_ERR_NOT_READY);
    access_fake_derive_fail(false);
    (void)access_test_session(ADMIN, PASSWORD, &access_test_web);
}

/* --- Changes racing a login: each hook runs while the login verifies. --- */

static crux_access_session_t racing_admin;

static void hook_disable_user(void *context)
{
    (void)context;
    CHECK(crux_access_account_disable(racing_admin, USER) == CRUX_ACCESS_OK);
}

static void hook_reset_user(void *context)
{
    (void)context;
    CHECK(crux_access_account_reset_password(racing_admin, USER, OTHER_PASSWORD,
                                             strlen(OTHER_PASSWORD)) == CRUX_ACCESS_OK);
}

static void hook_remove_and_recreate_user(void *context)
{
    (void)context;
    CHECK(crux_access_account_remove(racing_admin, USER) == CRUX_ACCESS_OK);
    access_test_create_user(racing_admin);
}

static void hook_set_role(void *context)
{
    (void)context;
    CHECK(crux_access_account_set_role(racing_admin, USER, "viewer") == CRUX_ACCESS_OK);
}

static void hook_create_user(void *context)
{
    (void)context;
    access_test_create_user(racing_admin);
}

static void hook_own_password_change(void *context)
{
    crux_access_session_t *own = context;
    uint32_t retry_after_ms = 0;
    CHECK(crux_access_account_change_password(*own, PASSWORD, strlen(PASSWORD), OTHER_PASSWORD,
                                              strlen(OTHER_PASSWORD),
                                              &retry_after_ms) == CRUX_ACCESS_OK);
}

static void expect_race_denied(access_fake_hook_t hook, const char *password)
{
    crux_access_origin_t origin = access_test_web_from(1);
    crux_access_session_t session = {0};
    access_fake_derive_hook(hook, NULL);
    CHECK(access_test_login(USER, password, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(session.value == 0);
}

static void race_setup(bool create_user)
{
    access_test_boot_with_admin();
    racing_admin = access_test_session(ADMIN, PASSWORD, &access_test_serial);
    if (create_user) {
        access_test_create_user(racing_admin);
    }
}

static void login_race_cases(void)
{
    race_setup(true);
    expect_race_denied(hook_disable_user, PASSWORD);

    race_setup(true);
    expect_race_denied(hook_reset_user, PASSWORD);
    /* A login with the old password fails after it, and the new one needs the reset's flag. */
    crux_access_session_t session = {0};
    crux_access_origin_t origin = access_test_web_from(2);
    CHECK(access_test_login(USER, PASSWORD, &origin, &session) == CRUX_ACCESS_ERR_DENIED);

    /* The account's own password change, from serial, during verification on the network. */
    race_setup(true);
    crux_access_session_t own = access_test_session(USER, PASSWORD, &access_test_serial);
    access_fake_derive_hook(hook_own_password_change, &own);
    origin = access_test_web_from(5);
    CHECK(access_test_login(USER, PASSWORD, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(session.value == 0);

    /* An unknown name that gains an account during verification, with a derivation that matches
     * the dummy record: it must still fail. */
    race_setup(false);
    access_fake_derive_force(crux_access_dummy_derived);
    expect_race_denied(hook_create_user, PASSWORD);
    access_fake_derive_force(NULL);

    /* Removing and recreating a name with the same password still differs. */
    race_setup(true);
    expect_race_denied(hook_remove_and_recreate_user, PASSWORD);
    origin = access_test_web_from(3);
    (void)access_test_session(USER, PASSWORD, &origin);

    race_setup(true);
    expect_race_denied(hook_set_role, PASSWORD);

    /* An unknown name that gains an account during verification still fails: it ran the dummy. */
    race_setup(false);
    expect_race_denied(hook_create_user, PASSWORD);
    origin = access_test_web_from(4);
    (void)access_test_session(USER, PASSWORD, &origin);
}

/* --- Cost upgrade --- */

/* Lowers the admin record's cost on the medium, then powers on from it. */
static void boot_with_low_cost_record(uint32_t *low_cost)
{
    access_test_boot_with_admin();
    size_t length = 0;
    const uint8_t *stored = access_fake_store_record(&length);
    access_table_t table;
    CHECK(crux_access_record_decode(stored, length, &table));
    table.accounts[0].iterations = CRUX_ACCESS_VERIFIER_ITERATIONS / 2u;
    /* The fake derives differently per iteration count, so derive the value at the lower cost. */
    CHECK(crux_access_port_derive(table.accounts[0].algorithm, table.accounts[0].iterations,
                                  table.accounts[0].salt, PASSWORD, strlen(PASSWORD),
                                  table.accounts[0].derived));
    uint8_t record[CRUX_ACCESS_RECORD_SIZE_MAX];
    length = crux_access_record_encode(&table, record, sizeof(record));
    CHECK(length != 0);
    access_fake_store_set_record(record, length);
    access_test_power_cycle(reference());
    *low_cost = table.accounts[0].iterations;
    access_fake_derive_counters_clear();
}

static uint32_t stored_cost(void)
{
    size_t length = 0;
    const uint8_t *stored = access_fake_store_record(&length);
    access_table_t table;
    CHECK(crux_access_record_decode(stored, length, &table));
    return table.accounts[0].iterations;
}

static void upgrade_cases(void)
{
    uint32_t low_cost = 0;

    /* A failed login never upgrades. It runs at the record's own cost: the declared timing limit.
     */
    boot_with_low_cost_record(&low_cost);
    crux_access_session_t session = {0};
    crux_access_origin_t origin = access_test_web_from(1);
    CHECK(access_test_login(ADMIN, WRONG, &origin, &session) == CRUX_ACCESS_ERR_DENIED);
    CHECK(access_fake_derive_iterations() == low_cost);
    CHECK(stored_cost() == low_cost);

    /* A successful login upgrades, and existing sessions continue. */
    access_fake_derive_counters_clear();
    origin = access_test_web_from(2);
    crux_access_session_t existing = access_test_session(ADMIN, PASSWORD, &origin);
    CHECK(access_fake_derive_count() == 2);
    CHECK(access_fake_derive_iterations() == low_cost + CRUX_ACCESS_VERIFIER_ITERATIONS);
    CHECK(stored_cost() == CRUX_ACCESS_VERIFIER_ITERATIONS);
    CHECK(crux_access_check(existing, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    /* The next login needs no upgrade, and the password still works. */
    access_fake_derive_counters_clear();
    (void)access_test_session(ADMIN, PASSWORD, &origin);
    CHECK(access_fake_derive_count() == 1);

    /* A failed upgrade does not affect the login, whichever step fails. */
    boot_with_low_cost_record(&low_cost);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_FAIL);
    origin = access_test_web_from(3);
    existing = access_test_session(ADMIN, PASSWORD, &origin);
    CHECK(crux_access_check(existing, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    access_fake_store_save_mode(ACCESS_FAKE_SAVE_OK);
    CHECK(stored_cost() == low_cost);

    boot_with_low_cost_record(&low_cost);
    access_fake_random_fail(true);
    existing = access_test_session(ADMIN, PASSWORD, &origin);
    access_fake_random_fail(false);
    CHECK(crux_access_check(existing, CRUX_ACCESS_PERMISSION_MANAGE) == CRUX_ACCESS_OK);
    CHECK(stored_cost() == low_cost);
}

int main(void)
{
    unknown_and_disabled_match_wrong_password();
    table_filling_probe();
    delay_window_cases();
    decay_cases();
    guessing_bound_cases();
    collision_cases();
    source_not_cleared_by_success_cases();
    flooding_cases();
    serial_independence_cases();
    verification_slot_cases();
    login_race_cases();
    upgrade_cases();
    return access_test_finish("Access security");
}
