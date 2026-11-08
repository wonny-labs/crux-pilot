/* A recording fake of the public Security event contract, for the Host tests of a consumer. It
 * implements every function in <crux/security_event.h> with no thread, ring, Logging or access
 * control, and passes the same public cases as the real core. Link crux_security_event_fake in
 * place of crux::security_event, and only in a test: it exists only when Crux builds its own tests.
 *
 * Field, identifier and project table rules are the real ones, because the fake compiles the same
 * source for them. It does not model ring overflow, the reserve or the drain: a record is dropped
 * only when a test asks for it. It takes one lock of its own, so it can be used from threads. */
#ifndef CRUX_SECURITY_EVENT_FAKE_H
#define CRUX_SECURITY_EVENT_FAKE_H

#include <crux/security_event.h>

/* The most records it keeps. Later ones are dropped and counted, with a sequence gap. */
#define SECURITY_EVENT_FAKE_RECORDS_MAX 1024u

/* Returns to the state before init, and clears every record and control. */
void security_event_fake_reset(void);
/* Inits with no project identifiers, for a test that does not care. */
void security_event_fake_boot(void);

/* Records kept so far, in order. The pointer is valid until the next reset. */
size_t security_event_fake_count(void);
const crux_security_event_record_t *security_event_fake_record(size_t index);
/* How many records hold this identifier. */
size_t security_event_fake_count_of(crux_security_event_id_t id);
/* The most recent record with this identifier, or NULL. */
const crux_security_event_record_t *security_event_fake_last(crux_security_event_id_t id);
/* Forgets the records and keeps the numbering. */
void security_event_fake_clear(void);

/* The next count emits are dropped, each taking a sequence number, as a full ring does. */
void security_event_fake_drop_next(size_t count);
/* What crux_security_event_read's permission check answers. CRUX_ACCESS_OK by default. */
void security_event_fake_access_answer(crux_access_result_t answer);
/* The uptime and clock a later emit and read report. By default: uptime zero and CLOCK_NONE. */
void security_event_fake_clock(uint64_t uptime_ms, int64_t wall_s,
                               crux_security_event_clock_t clock);
/* Replaces the counts crux_security_event_get_stats reports, until the next reset. */
void security_event_fake_stats_set(const crux_security_event_stats_t *stats);

#endif
