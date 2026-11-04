/* Many emitters, readers and the real drain at once, for the sanitizers. Every event ends as
 * admitted or dropped, and no reader sees numbers out of order or a torn record. */
#include "access_check_stub.h"
#include "event_test.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#define EMITTERS 6u
#define READERS 2u
#define EMITS_EACH 4000u

static atomic_bool readers_stop;
static atomic_uint lines_written;

static void count_line(void *context)
{
    (void)context;
    atomic_fetch_add(&lines_written, 1u);
    /* The capture keeps a bounded tail, and this suite only counts. */
}

static void *emitter(void *argument)
{
    unsigned which = *(unsigned *)argument;
    for (unsigned index = 0; index < EMITS_EACH; ++index) {
        crux_security_event_t event = {
            .id = (index & 7u) == 0
                      ? (crux_security_event_id_t)CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_CREATED
                      : (crux_security_event_id_t)CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED,
            .result = CRUX_SECURITY_EVENT_DENIED,
            .channel = 2,
            .source_length = 4,
            .source = {192, 0, 2, (uint8_t)which},
            .user = (which & 1u) == 0 ? "alice" : "bob",
            .subject = (which & 2u) == 0 ? "config.save" : "file.read",
        };
        crux_security_event_emit(&event);
        /* Pauses let the drain keep up, so that readers and the drain meet many admitted records,
         * rather than a full ring and a stream of refusals. */
        if ((index & 3u) == 3u) {
            const struct timespec pause = {0, 1000000};
            (void)nanosleep(&pause, NULL);
        }
    }
    return NULL;
}

static void *reader(void *argument)
{
    (void)argument;
    const crux_access_session_t session = {1};
    while (!atomic_load(&readers_stop)) {
        uint32_t cursor = 0;
        uint32_t previous = 0;
        crux_security_event_record_t record;
        while (crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK) {
            CHECK(previous == 0 || record.sequence > previous);
            previous = record.sequence;
            /* A torn record would pair the two names and subjects differently. */
            CHECK(record.source[3] < EMITTERS);
            CHECK(strcmp(record.user, (record.source[3] & 1u) == 0 ? "alice" : "bob") == 0);
            CHECK(strcmp(record.subject,
                         (record.source[3] & 2u) == 0 ? "config.save" : "file.read") == 0);
        }
    }
    return NULL;
}

int main(void)
{
    event_test_boot();
    event_test_clock_real(true);
    event_test_log_hook(count_line, NULL);
    CHECK(crux_security_event_start() == CRUX_OK);

    pthread_t emitters[EMITTERS];
    pthread_t readers[READERS];
    unsigned which[EMITTERS];
    for (unsigned index = 0; index < READERS; ++index) {
        CHECK(pthread_create(&readers[index], NULL, reader, NULL) == 0);
    }
    for (unsigned index = 0; index < EMITTERS; ++index) {
        which[index] = index;
        CHECK(pthread_create(&emitters[index], NULL, emitter, &which[index]) == 0);
    }
    for (unsigned index = 0; index < EMITTERS; ++index) {
        CHECK(pthread_join(emitters[index], NULL) == 0);
    }
    atomic_store(&readers_stop, true);
    for (unsigned index = 0; index < READERS; ++index) {
        CHECK(pthread_join(readers[index], NULL) == 0);
    }
    CHECK(crux_security_event_test_stop_drain() == CRUX_OK);
    event_test_drain_all();

    /* Every event is accounted for. */
    crux_security_event_stats_t stats = event_test_stats();
    CHECK(stats.emitted + stats.dropped == EMITTERS * EMITS_EACH);
    /* More than a ring's worth, so the emitters and the drain really ran together. */
    CHECK(stats.emitted > 2u * CRUX_SECURITY_EVENT_RING);
    CHECK(stats.high_water <= CRUX_SECURITY_EVENT_RING);
    CHECK(stats.log_errors == 0);
    CHECK(stats.invalid_fields == 0);
    CHECK(crux_security_event_test_pending() == 0);
    /* Each admitted record became one line, with up to one more for each report of loss. */
    CHECK(stats.delivered == atomic_load(&lines_written));
    CHECK(stats.delivered >= stats.emitted);
    printf("emitted %u, dropped %u, lines %u, high water %u\n", (unsigned)stats.emitted,
           (unsigned)stats.dropped, (unsigned)stats.delivered, (unsigned)stats.high_water);
    event_test_reset();
    return event_test_finish("Security event concurrency");
}
