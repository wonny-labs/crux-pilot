/* The server thread never waits for a password: with the real worker thread holding a verification
 * open, every other request is answered at once. */
#include "session_test.h"

#include <errno.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <time.h>

/* A verification that waits this long would show in every measurement below. */
#define HELD_DERIVE_MS 10000u
/* What a request on the server thread may take here: the attempt lock is held only to copy. */
#define SERVER_LIMIT_MS 1000u

static sem_t release_derive;
static atomic_bool in_derive;

static uint64_t monotonic_ms(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void sleep_ms(unsigned milliseconds)
{
    struct timespec pause = {0, (long)milliseconds * 1000000L};

    while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {
    }
}

static void hold_derive(void *context)
{
    struct timespec until;

    (void)context;
    atomic_store(&in_derive, true);
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += HELD_DERIVE_MS / 1000u;
    while (sem_timedwait(&release_derive, &until) != 0 && errno == EINTR) {
    }
    atomic_store(&in_derive, false);
}

static bool wait_for(bool (*condition)(void), unsigned limit_ms)
{
    uint64_t start = monotonic_ms();

    while (!condition()) {
        if (monotonic_ms() - start > limit_ms) {
            return false;
        }
        sleep_ms(1u);
    }
    return true;
}

static bool derive_held(void)
{
    return atomic_load(&in_derive);
}

static char polled_id[80];
static int polled_status;
static char polled_body[128];

static bool poll_finished(void)
{
    const fake_reply_t *reply = session_poll(polled_id);

    polled_status = reply->status;
    snprintf(polled_body, sizeof(polled_body), "%s", reply->body);
    return !(reply->status == 200 && strcmp(reply->body, "{\"state\":\"pending\"}") == 0);
}

static void test_held_verification_stalls_nobody(void)
{
    session_jar_t jar;
    char other[80];
    uint64_t longest = 0;
    unsigned requests = 0;

    session_test_setup();
    CHECK(session_login("admin", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(crux_web_session_start_worker() == CRUX_WEB_OK);

    access_fake_derive_hook(hold_derive, NULL);
    CHECK(session_post_login("viewer", "wrong password here", polled_id, sizeof(polled_id))
              ->status == 202);
    CHECK(wait_for(derive_held, 5000u));

    for (unsigned round = 0; round < 50u; ++round) {
        uint64_t started = monotonic_ms();
        uint64_t elapsed;

        CHECK(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL)->status == 200);
        CHECK(session_with(&jar, CRUX_WEB_POST, "/api/v1/test/session", NULL)->status == 200);
        CHECK(session_post_login("admin", SESSION_TEST_PASSWORD, other, sizeof(other))->status ==
              503);
        CHECK(strcmp(session_poll(polled_id)->body, "{\"state\":\"pending\"}") == 0);
        CHECK(session_poll("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")->status == 404);
        requests += 5u;
        elapsed = monotonic_ms() - started;
        if (elapsed > longest) {
            longest = elapsed;
        }
    }
    /* Still verifying: everything above ran beside it. */
    CHECK(derive_held());
    printf("held verification: %u requests answered, the slowest round of 5 took %llu ms\n",
           requests, (unsigned long long)longest);
    CHECK(longest < SERVER_LIMIT_MS);

    CHECK(sem_post(&release_derive) == 0);
    CHECK(wait_for(poll_finished, 5000u));
    CHECK(polled_status == 200 && strcmp(polled_body, "{\"state\":\"rejected\"}") == 0);
}

/* Failed logins, one after another through the worker, while another client is served. */
static void test_failed_logins_beside_a_client(void)
{
    session_jar_t jar;
    uint64_t longest = 0;
    unsigned answered = 0;

    session_test_setup();
    CHECK(session_login("admin", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(crux_web_session_start_worker() == CRUX_WEB_OK);
    for (unsigned attempt = 0; attempt < 40u; ++attempt) {
        uint64_t started;
        uint64_t elapsed;

        if (session_post_login("nobody", "wrong password here", polled_id, sizeof(polled_id))
                ->status == 202) {
            ++answered;
        }
        started = monotonic_ms();
        CHECK(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL)->status == 200);
        elapsed = monotonic_ms() - started;
        if (elapsed > longest) {
            longest = elapsed;
        }
        CHECK(wait_for(poll_finished, 5000u));
    }
    printf("failed logins: %u queued, the other client's slowest answer took %llu ms\n", answered,
           (unsigned long long)longest);
    CHECK(answered == 40u);
    CHECK(longest < SERVER_LIMIT_MS);
}

int main(void)
{
    CHECK(sem_init(&release_derive, 0, 0) == 0);
    test_held_verification_stalls_nobody();
    test_failed_logins_beside_a_client();
    return session_test_finish("web_session_concurrency");
}
