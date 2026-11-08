/* Decision C7: the System and clock sources are read at most once per refresh period, however fast
 * and from however many threads clients poll, with a controlled clock. */
#include "status_test.h"

#include <pthread.h>
#include <stdatomic.h>

static void poll_snapshot_items(crux_access_session_t session)
{
    (void)entry_named(session, "system.clock");
    (void)entry_named(session, "system.watchdog_state");
}

static void reads_at_most_once_per_period(void)
{
    fixture_start();
    /* Nothing is read at start, and a list that does not reach them reads nothing. */
    CHECK(status_fake_system_reads() == 0);
    CHECK(status_fake_clock_reads() == 0);

    poll_snapshot_items(s_viewer);
    CHECK(status_fake_system_reads() == 1);
    CHECK(status_fake_clock_reads() == 1);

    /* Any number of polls inside the period, up to one tick before its end. */
    for (unsigned i = 0; i < 1000; ++i) {
        poll_snapshot_items(s_viewer);
    }
    CHECK(status_fake_system_reads() == 1);
    status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS - 1u);
    poll_snapshot_items(s_viewer);
    CHECK(status_fake_system_reads() == 1);
    CHECK(status_fake_clock_reads() == 1);

    /* The period's end refreshes once, and only once more for the next. */
    status_fake_advance_ms(1);
    poll_snapshot_items(s_viewer);
    poll_snapshot_items(s_viewer);
    CHECK(status_fake_system_reads() == 2);
    CHECK(status_fake_clock_reads() == 2);
    status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS);
    poll_snapshot_items(s_viewer);
    CHECK(status_fake_system_reads() == 3);
    CHECK(status_fake_clock_reads() == 3);

    /* However many periods pass, one poll reads once. */
    status_fake_advance_ms(UINT64_C(100) * CRUX_MGMT_STATUS_REFRESH_MS);
    poll_snapshot_items(s_viewer);
    CHECK(status_fake_system_reads() == 4);
}

static void answers_are_from_the_snapshot(void)
{
    fixture_start();
    expect_text_item(s_viewer, "system.watchdog_state", "active");
    expect_text_item(s_viewer, "system.clock", "rtc");

    /* A source that changed is not seen until the period ends. */
    status_fake_system(CRUX_OK, CRUX_OK, CRUX_WATCHDOG_FAULTED);
    status_fake_clock(CRUX_OK, CRUX_MGMT_STATUS_CLOCK_MANUAL);
    status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS - 1u);
    expect_text_item(s_viewer, "system.watchdog_state", "active");
    expect_text_item(s_viewer, "system.clock", "rtc");
    status_fake_advance_ms(1);
    expect_text_item(s_viewer, "system.watchdog_state", "faulted");
    expect_text_item(s_viewer, "system.clock", "manual");

    /* A failed read is an answer too, which also waits a period before the next read. */
    status_fake_system(CRUX_ERR_IO, CRUX_OK, CRUX_WATCHDOG_ACTIVE);
    status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS);
    expect_failed_item(s_viewer, "system.watchdog_state", CRUX_ERR_IO);
    expect_text_item(s_viewer, "system.clock", "manual");
    unsigned reads = status_fake_system_reads();
    status_fake_system(CRUX_OK, CRUX_OK, CRUX_WATCHDOG_ACTIVE);
    expect_failed_item(s_viewer, "system.watchdog_state", CRUX_ERR_IO);
    CHECK(status_fake_system_reads() == reads);
    status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS);
    expect_text_item(s_viewer, "system.watchdog_state", "active");
}

static void other_items_are_not_rate_limited(void)
{
    fixture_start();
    /* Only C7's sources are in the snapshot: uptime moves, and the network is read each time. */
    CHECK(entry_named(s_viewer, "system.uptime_ms").value.data.u64 == 1000u);
    status_fake_advance_ms(1);
    CHECK(entry_named(s_viewer, "system.uptime_ms").value.data.u64 == 1001u);
    unsigned reads = status_fake_net_reads();
    (void)entry_named(s_viewer, "net.link");
    CHECK(status_fake_net_reads() > reads);
}

static void a_clock_failure_keeps_the_last_answer(void)
{
    fixture_start();
    expect_text_item(s_viewer, "system.watchdog_state", "active");
    unsigned reads = status_fake_system_reads();
    /* With no clock it cannot know the age, so it does not read again, whatever a client does. */
    status_fake_uptime(CRUX_ERR_IO, 0);
    status_fake_system(CRUX_OK, CRUX_OK, CRUX_WATCHDOG_FAULTED);
    for (unsigned i = 0; i < 10; ++i) {
        expect_text_item(s_viewer, "system.watchdog_state", "active");
    }
    CHECK(status_fake_system_reads() == reads);
    expect_failed_item(s_viewer, "system.uptime_ms", CRUX_ERR_IO);
}

static void a_clock_failure_before_any_refresh_is_busy(void)
{
    fixture_boot();
    status_fake_uptime(CRUX_ERR_IO, 0);
    crux_mgmt_status_config_t config = fixture_config();
    CHECK(crux_mgmt_status_start(&config) == CRUX_OK);
    expect_failed_item(s_viewer, "system.watchdog_state", CRUX_ERR_BUSY);
    expect_failed_item(s_viewer, "system.clock", CRUX_ERR_BUSY);
    CHECK(status_fake_system_reads() == 0);
    /* Once the clock answers, so does the snapshot. */
    status_fake_uptime(CRUX_OK, 5000);
    expect_text_item(s_viewer, "system.watchdog_state", "active");
    CHECK(status_fake_system_reads() == 1);
}

static void a_clock_that_runs_backwards_reads_nothing_new(void)
{
    fixture_start();
    status_fake_uptime(CRUX_OK, 10000);
    expect_text_item(s_viewer, "system.watchdog_state", "active");
    unsigned reads = status_fake_system_reads();
    /* A monotonic source never does this. If one did, the rate limit is what matters more. */
    status_fake_uptime(CRUX_OK, 5000);
    expect_text_item(s_viewer, "system.watchdog_state", "active");
    CHECK(status_fake_system_reads() == reads);
    status_fake_uptime(CRUX_OK, 10000u + CRUX_MGMT_STATUS_REFRESH_MS);
    expect_text_item(s_viewer, "system.watchdog_state", "active");
    CHECK(status_fake_system_reads() == reads + 1u);
}

/* A loser of the poll, while another caller is inside a refresh. */
typedef struct {
    crux_mgmt_status_entry_t watchdog, clock;
    crux_access_session_t session;
} during_refresh_t;

static void *poll_during_refresh(void *argument)
{
    during_refresh_t *during = argument;
    during->watchdog = entry_named(during->session, "system.watchdog_state");
    during->clock = entry_named(during->session, "system.clock");
    return NULL;
}

/* Runs inside the system read of a refresh, and blocks that refresh until the other thread returns.
 * If a poll waited behind the refresh, this would hang until CTest's timeout. */
static void hook_poll_during_refresh(void *context)
{
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, poll_during_refresh, context) == 0);
    CHECK(pthread_join(thread, NULL) == 0);
}

static void a_loser_answers_from_the_previous_snapshot(void)
{
    fixture_start();
    /* The first refresh completes. */
    expect_text_item(s_viewer, "system.watchdog_state", "active");
    expect_text_item(s_viewer, "system.clock", "rtc");

    /* The next is in progress, with a new answer that is not yet published. */
    status_fake_advance_ms(CRUX_MGMT_STATUS_REFRESH_MS);
    status_fake_system(CRUX_OK, CRUX_OK, CRUX_WATCHDOG_FAULTED);
    status_fake_clock(CRUX_OK, CRUX_MGMT_STATUS_CLOCK_MANUAL);
    during_refresh_t during = {.session = s_viewer};
    status_fake_system_hook(hook_poll_during_refresh, &during);
    expect_text_item(s_viewer, "system.watchdog_state", "faulted");

    CHECK(during.watchdog.status == CRUX_OK);
    CHECK(strcmp(during.watchdog.value.data.text, "active") == 0);
    CHECK(during.clock.status == CRUX_OK);
    CHECK(strcmp(during.clock.value.data.text, "rtc") == 0);
    /* The refresh then completed, and everyone sees it. */
    expect_text_item(s_viewer, "system.clock", "manual");
    CHECK(status_fake_system_reads() == 2);
}

static void a_loser_before_the_first_refresh_is_busy(void)
{
    fixture_start();
    during_refresh_t during = {.session = s_viewer};
    status_fake_system_hook(hook_poll_during_refresh, &during);
    expect_text_item(s_viewer, "system.watchdog_state", "active");
    CHECK(during.watchdog.status == CRUX_ERR_BUSY);
    CHECK(during.clock.status == CRUX_ERR_BUSY);
    /* A busy answer holds no value. */
    CHECK(value_is_zero(&during.watchdog.value));
    CHECK(status_fake_system_reads() == 1);
}

/* Concurrent polls. */
static atomic_bool go;
static atomic_uint unexpected;
static crux_access_session_t poll_session;

static void *polling_thread(void *argument)
{
    (void)argument;
    while (!atomic_load(&go)) {
    }
    for (unsigned round = 0; round < 500; ++round) {
        size_t cursor = 0;
        crux_mgmt_status_entry_t entry;
        crux_status_t result;
        while ((result = crux_mgmt_status_next(poll_session, &cursor, &entry)) == CRUX_OK) {
            /* A failure here is only a poll that lost before the first refresh. */
            if (entry.status != CRUX_OK && entry.status != CRUX_ERR_BUSY) {
                atomic_fetch_add(&unexpected, 1u);
            }
            if (entry.status == CRUX_OK && strcmp(entry.name, "system.watchdog_state") == 0 &&
                strcmp(entry.value.data.text, "active") != 0) {
                atomic_fetch_add(&unexpected, 1u);
            }
        }
        if (result != CRUX_ERR_NO_DATA) {
            atomic_fetch_add(&unexpected, 1u);
        }
    }
    return NULL;
}

static void concurrent_polls_count_as_one_read(void)
{
    fixture_start();
    poll_session = s_viewer;
    pthread_t threads[4];
    for (size_t i = 0; i < 4; ++i) {
        CHECK(pthread_create(&threads[i], NULL, polling_thread, NULL) == 0);
    }
    atomic_store(&go, true);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    CHECK(atomic_load(&unexpected) == 0);
    /* Four threads and 2000 lists in one period read each source once. */
    CHECK(status_fake_system_reads() == 1);
    CHECK(status_fake_clock_reads() == 1);
}

static atomic_bool stop_advancing;

static void *advancing_clock_thread(void *argument)
{
    (void)argument;
    while (!atomic_load(&stop_advancing)) {
        status_fake_advance_ms(1);
    }
    return NULL;
}

static void reads_stay_bounded_as_time_passes(void)
{
    fixture_start();
    poll_session = s_viewer;
    uint64_t start_ms = entry_named(s_viewer, "system.uptime_ms").value.data.u64;
    pthread_t clock_thread;
    CHECK(pthread_create(&clock_thread, NULL, advancing_clock_thread, NULL) == 0);
    pthread_t threads[3];
    for (size_t i = 0; i < 3; ++i) {
        CHECK(pthread_create(&threads[i], NULL, polling_thread, NULL) == 0);
    }
    atomic_store(&go, true);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    atomic_store(&stop_advancing, true);
    CHECK(pthread_join(clock_thread, NULL) == 0);
    CHECK(atomic_load(&unexpected) == 0);

    /* The rule itself, whatever this machine's speed: the reads cannot outrun the clock. */
    uint64_t elapsed_ms = entry_named(s_viewer, "system.uptime_ms").value.data.u64 - start_ms;
    uint64_t allowed = elapsed_ms / CRUX_MGMT_STATUS_REFRESH_MS + 1u;
    CHECK(status_fake_system_reads() <= allowed);
    CHECK(status_fake_clock_reads() <= allowed);
}

int main(void)
{
    run_case("reads at most once per period", reads_at_most_once_per_period);
    run_case("answers are from the snapshot", answers_are_from_the_snapshot);
    run_case("other items are not rate limited", other_items_are_not_rate_limited);
    run_case("a clock failure keeps the last answer", a_clock_failure_keeps_the_last_answer);
    run_case("a clock failure before any refresh is busy",
             a_clock_failure_before_any_refresh_is_busy);
    run_case("a clock that runs backwards reads nothing new",
             a_clock_that_runs_backwards_reads_nothing_new);
    run_case("a loser answers from the previous snapshot",
             a_loser_answers_from_the_previous_snapshot);
    run_case("a loser before the first refresh is busy", a_loser_before_the_first_refresh_is_busy);
    run_case("concurrent polls count as one read", concurrent_polls_count_as_one_read);
    run_case("reads stay bounded as time passes", reads_stay_bounded_as_time_passes);
    return suite_finish("MgmtStatus.snapshot");
}
