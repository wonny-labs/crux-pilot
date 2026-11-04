/* The fake's own controls, which a consumer's tests rely on. The contract it shares with the core
 * is in public_cases.c. */
#include "security_event_fake.h"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(expression)) {                                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static const crux_access_session_t session = {1};

static void emit(crux_security_event_id_t id, const char *user)
{
    crux_security_event_t event = {.id = id, .result = CRUX_SECURITY_EVENT_OK, .user = user};
    crux_security_event_emit(&event);
}

static void records_are_kept_in_order(void)
{
    security_event_fake_boot();
    CHECK(security_event_fake_count() == 0);
    CHECK(security_event_fake_record(0) == NULL);
    CHECK(security_event_fake_last(CRUX_SECURITY_EVENT_ID_CONFIG_SAVED) == NULL);

    emit(CRUX_SECURITY_EVENT_ID_CONFIG_SAVED, "alice");
    emit(CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED, "bob");
    emit(CRUX_SECURITY_EVENT_ID_CONFIG_SAVED, "carol");
    CHECK(security_event_fake_count() == 3);
    CHECK(security_event_fake_record(1)->id == CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED);
    CHECK(security_event_fake_record(1)->sequence == 2);
    CHECK(security_event_fake_record(3) == NULL);
    CHECK(security_event_fake_count_of(CRUX_SECURITY_EVENT_ID_CONFIG_SAVED) == 2);
    CHECK(security_event_fake_count_of(CRUX_SECURITY_EVENT_ID_CLI_LINE_REJECTED) == 0);
    CHECK(strcmp(security_event_fake_last(CRUX_SECURITY_EVENT_ID_CONFIG_SAVED)->user, "carol") ==
          0);

    security_event_fake_clear();
    CHECK(security_event_fake_count() == 0);
    emit(CRUX_SECURITY_EVENT_ID_CONFIG_SAVED, NULL);
    CHECK(security_event_fake_record(0)->sequence == 4);
}

static void drops_leave_a_gap(void)
{
    security_event_fake_boot();
    emit(CRUX_SECURITY_EVENT_ID_FILE_CREATED, NULL);
    security_event_fake_drop_next(2);
    emit(CRUX_SECURITY_EVENT_ID_FILE_CREATED, NULL);
    emit(CRUX_SECURITY_EVENT_ID_FILE_CREATED, NULL);
    emit(CRUX_SECURITY_EVENT_ID_FILE_REMOVED, NULL);

    crux_security_event_stats_t stats;
    CHECK(crux_security_event_get_stats(&stats) == CRUX_OK);
    CHECK(stats.emitted == 2);
    CHECK(stats.dropped == 2);

    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 1);
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 4);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_FILE_REMOVED);
}

static void capacity_is_counted(void)
{
    security_event_fake_boot();
    for (size_t index = 0; index < SECURITY_EVENT_FAKE_RECORDS_MAX + 2u; ++index) {
        emit(CRUX_SECURITY_EVENT_ID_FILE_CREATED, NULL);
    }
    crux_security_event_stats_t stats;
    CHECK(crux_security_event_get_stats(&stats) == CRUX_OK);
    CHECK(stats.emitted == SECURITY_EVENT_FAKE_RECORDS_MAX);
    CHECK(stats.dropped == 2);
    CHECK(security_event_fake_count() == SECURITY_EVENT_FAKE_RECORDS_MAX);
}

static void clock_is_set_by_the_test(void)
{
    security_event_fake_boot();
    emit(CRUX_SECURITY_EVENT_ID_FILE_CREATED, NULL);
    security_event_fake_clock(5000, 1793750400, CRUX_SECURITY_EVENT_CLOCK_MANUAL);
    emit(CRUX_SECURITY_EVENT_ID_FILE_CREATED, NULL);

    CHECK(security_event_fake_record(0)->clock == CRUX_SECURITY_EVENT_CLOCK_NONE);
    CHECK(security_event_fake_record(0)->uptime_ms == 0);
    CHECK(security_event_fake_record(1)->uptime_ms == 5000);
    CHECK(security_event_fake_record(1)->wall_s == 1793750400);
    CHECK(security_event_fake_record(1)->clock == CRUX_SECURITY_EVENT_CLOCK_MANUAL);
}

static void stats_can_be_set(void)
{
    /* What a status service test needs: any counts, with no ring to fill. */
    security_event_fake_reset();
    const crux_security_event_stats_t wanted = {.emitted = 11,
                                                .dropped = 22,
                                                .delivered = 33,
                                                .log_errors = 44,
                                                .invalid_fields = 55,
                                                .high_water = 66};
    security_event_fake_stats_set(&wanted);
    crux_security_event_stats_t stats;
    CHECK(crux_security_event_get_stats(&stats) == CRUX_OK);
    CHECK(memcmp(&stats, &wanted, sizeof(stats)) == 0);

    security_event_fake_reset();
    CHECK(crux_security_event_get_stats(&stats) == CRUX_OK);
    CHECK(stats.emitted == 0 && stats.dropped == 0);
}

int main(void)
{
    records_are_kept_in_order();
    drops_leave_a_gap();
    capacity_is_counted();
    clock_is_set_by_the_test();
    stats_can_be_set();
    security_event_fake_reset();
    printf("Security event fake: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
