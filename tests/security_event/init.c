/* Before init, a failed init, repeated calls, project identifiers, refused calls and the start of
 * the drain. */
#include "access_check_stub.h"
#include "event_test.h"

static const crux_access_session_t session = {1};

static void before_init(void)
{
    event_test_reset();
    unsigned clock_reads = event_test_clock_reads();
    event_test_emit_routine();
    event_test_emit(CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_CREATED, CRUX_SECURITY_EVENT_OK,
                    "Bad User", NULL);

    /* Dropped and counted, whatever the class, with nothing looked at. */
    crux_security_event_stats_t stats = event_test_stats();
    CHECK(stats.dropped == 2);
    CHECK(stats.emitted == 0);
    CHECK(stats.invalid_fields == 0);
    CHECK(event_test_clock_reads() == clock_reads);

    /* Nothing else is ready either. */
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    unsigned checks = access_check_stub_count();
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_ERR_NOT_READY);
    CHECK(access_check_stub_count() == checks);
    CHECK(crux_security_event_start() == CRUX_ERR_NOT_READY);
    CHECK(crux_security_event_test_drain_step() == 0);

    /* The count survives init, and the first report includes it, with no number to name. */
    CHECK(crux_security_event_init(NULL, 0) == CRUX_OK);
    CHECK(event_test_stats().dropped == 2);
    CHECK(crux_security_event_test_drain_step() == 1);
    CHECK(strstr(event_test_last_line(), "evt=event.lost seq=1 ") != NULL);
    CHECK(strstr(event_test_last_line(), "first=") == NULL);
    CHECK(strstr(event_test_last_line(), " count=2 res=failed") != NULL);

    /* After init an emit is admitted. */
    event_test_emit_routine();
    CHECK(event_test_stats().emitted == 1);
}

static void failed_init(void)
{
    event_test_reset();
    event_test_mutex_create_fail(CRUX_ERR_NO_RESOURCES);
    CHECK(crux_security_event_init(NULL, 0) == CRUX_ERR_NO_RESOURCES);
    event_test_emit_routine();
    CHECK(event_test_stats().dropped == 1);
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_ERR_NOT_READY);

    /* A failure after the mutex leaves no mutex behind: a retry does not use up the pool. */
    event_test_semaphore_create_fail(CRUX_ERR_NO_RESOURCES);
    CHECK(crux_security_event_init(NULL, 0) == CRUX_ERR_NO_RESOURCES);
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        event_test_semaphore_create_fail(CRUX_ERR_IO);
        CHECK(crux_security_event_init(NULL, 0) == CRUX_ERR_IO);
    }
    event_test_emit_routine();
    CHECK(event_test_stats().dropped == 2);
    CHECK(event_test_stats().emitted == 0);

    /* It can be retried. */
    CHECK(crux_security_event_init(NULL, 0) == CRUX_OK);
    event_test_emit_routine();
    CHECK(event_test_stats().emitted == 1);
}

static void repeated_init(void)
{
    static const crux_security_event_project_id_t first[] = {
        {0x8000, "app.first", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t second[] = {
        {0x8000, "app.second", CRUX_SECURITY_EVENT_ROUTINE}};
    event_test_reset();
    CHECK(crux_security_event_init(first, 1) == CRUX_OK);
    event_test_emit_routine();
    CHECK(crux_security_event_init(second, 1) == CRUX_ERR_INVALID_STATE);
    CHECK(strcmp(crux_security_event_name(0x8000), "app.first") == 0);
    CHECK(event_test_stats().emitted == 1);
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
}

static void invalid_project_tables(void)
{
    static const crux_security_event_project_id_t low_id[] = {
        {0x7FFF, "app.low", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t crux_id[] = {
        {CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_OK, "app.login", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t duplicate[] = {
        {0x8000, "app.one", CRUX_SECURITY_EVENT_ROUTINE},
        {0x8000, "app.two", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t no_name[] = {
        {0x8000, NULL, CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t empty_name[] = {
        {0x8000, "", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t long_name[] = {
        {0x8000, "abcdefghijklmnopqrstuvwxy", CRUX_SECURITY_EVENT_ROUTINE}}; /* 25 characters */
    static const crux_security_event_project_id_t upper_name[] = {
        {0x8000, "App.Name", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t space_name[] = {
        {0x8000, "app name", CRUX_SECURITY_EVENT_ROUTINE}};
    static const crux_security_event_project_id_t bad_class[] = {
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): the case under test.
        {0x8000, "app.name", (crux_security_event_class_t)7}};
    const crux_security_event_project_id_t *const tables[] = {
        low_id, crux_id, no_name, empty_name, long_name, upper_name, space_name, bad_class,
    };
    event_test_reset();
    for (size_t index = 0; index < sizeof(tables) / sizeof(tables[0]); ++index) {
        CHECK(crux_security_event_init(tables[index], 1) == CRUX_ERR_INVALID_ARGUMENT);
    }
    CHECK(crux_security_event_init(duplicate, 2) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_init(NULL, 1) == CRUX_ERR_INVALID_ARGUMENT);
    event_test_emit_routine();
    CHECK(event_test_stats().dropped == 1);

    /* It can then succeed. A table of zero entries may be anything. */
    CHECK(crux_security_event_init(low_id, 0) == CRUX_OK);
    CHECK(strcmp(crux_security_event_name(0x7FFF), "event.unknown") == 0);
}

static void project_identifiers(void)
{
    static const crux_security_event_project_id_t table[] = {
        {0x8000, "app.pump_started", CRUX_SECURITY_EVENT_CHANGE},
        {0xFFFF, "abcdefghijklmnopqrstuvwx", CRUX_SECURITY_EVENT_ROUTINE}, /* 24 characters */
    };
    event_test_reset();
    CHECK(crux_security_event_init(table, 2) == CRUX_OK);
    CHECK(strcmp(crux_security_event_name(0x8000), "app.pump_started") == 0);
    CHECK(strcmp(crux_security_event_name(0xFFFF), "abcdefghijklmnopqrstuvwx") == 0);
    CHECK(strcmp(crux_security_event_name(0x8001), "event.unknown") == 0);

    event_test_emit(0x8000, CRUX_SECURITY_EVENT_OK, "alice", "pump/1");
    CHECK(crux_security_event_test_drain_step() == 1);
    CHECK(strcmp(event_test_last_line(),
                 "evt=app.pump_started seq=1 up=1000 clock=none chan=web src=192.0.2.7 user=alice "
                 "subj=pump/1 res=ok") == 0);
    event_test_emit(0xFFFF, CRUX_SECURITY_EVENT_DENIED, NULL, NULL);
    CHECK(crux_security_event_test_drain_step() == 1);
    CHECK(strncmp(event_test_last_line(), "evt=abcdefghijklmnopqrstuvwx seq=2 ", 35) == 0);

    /* An undeclared one is recorded as unknown, and counted. */
    event_test_emit(0x8001, CRUX_SECURITY_EVENT_OK, NULL, NULL);
    CHECK(crux_security_event_test_drain_step() == 1);
    CHECK(strncmp(event_test_last_line(), "evt=event.unknown seq=3 ", 24) == 0);
    CHECK(event_test_stats().invalid_fields == 1);

    /* The table decides the class: the change-class one enters the reserve. */
    for (size_t index = 0; index < (size_t)CRUX_SECURITY_EVENT_RING - CRUX_SECURITY_EVENT_RESERVE;
         ++index) {
        event_test_emit_routine();
    }
    crux_security_event_stats_t before = event_test_stats();
    event_test_emit(0xFFFF, CRUX_SECURITY_EVENT_OK, NULL, NULL);
    crux_security_event_stats_t after = event_test_stats();
    CHECK(after.dropped == before.dropped + 1u);
    CHECK(after.emitted == before.emitted);
    event_test_emit(0x8000, CRUX_SECURITY_EVENT_OK, NULL, NULL);
    after = event_test_stats();
    CHECK(after.emitted == before.emitted + 1u);
}

static void refused_calls(void)
{
    event_test_boot();
    /* A NULL event cannot fail the caller, and it is counted. */
    crux_security_event_emit(NULL);
    crux_security_event_stats_t stats = event_test_stats();
    CHECK(stats.dropped == 1);
    CHECK(stats.emitted == 0);

    /* An ISR call touches nothing and is counted. */
    event_test_in_isr(true);
    unsigned clock_reads = event_test_clock_reads();
    event_test_emit_routine();
    event_test_in_isr(false);
    CHECK(event_test_clock_reads() == clock_reads);
    stats = event_test_stats();
    CHECK(stats.dropped == 2);
    CHECK(stats.emitted == 0);

    /* Both report together. */
    CHECK(crux_security_event_test_drain_step() == 1);
    CHECK(strstr(event_test_last_line(), " count=2 ") != NULL);
    CHECK(strstr(event_test_last_line(), "first=") == NULL);

    /* A stats output is required, and an unready component still answers. */
    CHECK(crux_security_event_get_stats(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    event_test_reset();
    crux_security_event_stats_t empty;
    memset(&empty, 0xA5, sizeof(empty));
    CHECK(crux_security_event_get_stats(&empty) == CRUX_OK);
    CHECK(empty.emitted == 0 && empty.dropped == 0 && empty.delivered == 0 &&
          empty.log_errors == 0 && empty.invalid_fields == 0 && empty.high_water == 0);
}

static void start_failure_leaves_the_ring_usable(void)
{
    event_test_boot();
    event_test_emit_routine();
    event_test_thread_create_fail(CRUX_ERR_NO_RESOURCES);
    CHECK(crux_security_event_start() == CRUX_ERR_NO_RESOURCES);
    /* The thread was asked for at low priority. */
    CHECK(event_test_thread_options().priority == CRUX_THREAD_PRIORITY_LOW);
    event_test_emit_change();
    CHECK(event_test_stats().emitted == 2);
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 1);

    /* It can be retried, and the real thread then drains what waited. */
    event_test_clock_real(true);
    CHECK(crux_security_event_start() == CRUX_OK);
    CHECK(crux_security_event_start() == CRUX_ERR_INVALID_STATE);
    for (unsigned attempt = 0; attempt < 500 && event_test_line_count() < 2; ++attempt) {
        CHECK(crux_thread_sleep_for(crux_duration_from_ms(10)) == CRUX_OK);
    }
    CHECK(event_test_line_count() == 2);
    CHECK(crux_security_event_test_stop_drain() == CRUX_OK);
    CHECK(event_test_stats().delivered == 2);
}

int main(void)
{
    before_init();
    failed_init();
    repeated_init();
    invalid_project_tables();
    project_identifiers();
    refused_calls();
    start_failure_leaves_the_ring_usable();
    event_test_reset();
    return event_test_finish("Security event init");
}
