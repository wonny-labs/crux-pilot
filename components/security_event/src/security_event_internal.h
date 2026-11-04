#ifndef CRUX_SECURITY_EVENT_INTERNAL_H
#define CRUX_SECURITY_EVENT_INTERNAL_H

#include <crux/logging.h>
#include <crux/security_event.h>
#include <crux/sync.h>

#include <stdatomic.h>

/* Build settings. A composition sets them through CMake, and matches the burst and its pause to its
 * log buffer: a full ring can come to about 8 KB of text. */
#ifndef CRUX_SECURITY_EVENT_RING
#define CRUX_SECURITY_EVENT_RING 32
#endif
#ifndef CRUX_SECURITY_EVENT_RESERVE
#define CRUX_SECURITY_EVENT_RESERVE 8
#endif
#ifndef CRUX_SECURITY_EVENT_LOCK_MS
#define CRUX_SECURITY_EVENT_LOCK_MS 10
#endif
#ifndef CRUX_SECURITY_EVENT_DRAIN_BURST
#define CRUX_SECURITY_EVENT_DRAIN_BURST 4
#endif
#ifndef CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS
#define CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS 100
#endif
/* How long the drain sleeps with nothing signalled. A loss with no ring record, such as a lock wait
 * that expired, sends no signal, so this bounds how late its report can be. */
#ifndef CRUX_SECURITY_EVENT_DRAIN_WAIT_MS
#define CRUX_SECURITY_EVENT_DRAIN_WAIT_MS 1000
#endif
/* Zero selects the Thread provider's default. */
#ifndef CRUX_SECURITY_EVENT_DRAIN_STACK_BYTES
#define CRUX_SECURITY_EVENT_DRAIN_STACK_BYTES 0
#endif

_Static_assert(CRUX_SECURITY_EVENT_RING >= 2, "the ring must hold more than its reserve");
_Static_assert(CRUX_SECURITY_EVENT_RESERVE >= 1 &&
                   CRUX_SECURITY_EVENT_RESERVE < CRUX_SECURITY_EVENT_RING,
               "the reserve must leave routine events some of the ring");
_Static_assert(CRUX_SECURITY_EVENT_LOCK_MS >= 1, "an emit that never waits drops under contention");
_Static_assert(CRUX_SECURITY_EVENT_DRAIN_BURST >= 1, "the drain must write");
_Static_assert(CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS >= 1, "the pause paces a full ring");
_Static_assert(CRUX_SECURITY_EVENT_DRAIN_WAIT_MS >= 1, "the drain needs a finite wait");

/* A record in the ring. Wall time and clock are computed at drain or read, so they are not kept. */
typedef struct {
    uint64_t uptime_ms;
    uint32_t sequence;
    crux_security_event_id_t id;
    uint8_t result; /* A crux_security_event_result_t. */
    uint8_t channel;
    uint8_t source_length;
    bool no_uptime;
    bool subject_truncated;
    uint8_t source[16];
    char user[CRUX_SECURITY_EVENT_USER_SIZE];
    char subject[CRUX_SECURITY_EVENT_SUBJECT_SIZE];
} security_event_stored_t;

_Static_assert(sizeof(security_event_stored_t) <= 128, "the contract declares about 4.1 KB");

/* Worst-case line, including field names: evt 24, seq 10, up 13 digits, wall 10, clock 3, chan 6,
 * src 39, user 31, subj 48 and res 7 characters, with their keys and spaces. The printed source is
 * shorter than the 39 the contract allows for, at 24 characters for a /64. */
#define SECURITY_EVENT_LINE_WORST_CASE 245
_Static_assert(SECURITY_EVENT_LINE_WORST_CASE <= CRUX_LOG_TEXT_MAX,
               "a line must fit the Logging contract");
#define SECURITY_EVENT_LINE_SIZE ((size_t)CRUX_LOG_TEXT_MAX + 1u)
#define SECURITY_EVENT_LOG_TAG "crux.security"

/* Identifiers. The project table is set once, by init, before any emit is admitted. */
const char *crux_security_event_id_name(crux_security_event_id_t id, bool *known);
crux_security_event_class_t crux_security_event_id_class(crux_security_event_id_t id);
bool crux_security_event_project_table_valid(const crux_security_event_project_id_t *project_ids,
                                             size_t count);
void crux_security_event_project_table_set(const crux_security_event_project_id_t *project_ids,
                                           size_t count);

/* Applies the character rules, copying into a record whose sequence and time are the caller's.
 * Returns how many fields failed a rule. */
unsigned crux_security_event_fields_copy(const crux_security_event_t *event,
                                         security_event_stored_t *record);
/* Copies what a read shows. Wall time and clock are the caller's. */
void crux_security_event_record_fill(const security_event_stored_t *stored,
                                     crux_security_event_record_t *record);

/* Compares sequence numbers across a wrap. */
static inline bool crux_security_event_sequence_before(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) < 0;
}

/* The number after sequence. Zero is never a sequence, because it is a cursor's start. */
static inline uint32_t crux_security_event_sequence_next(uint32_t sequence)
{
    uint32_t next = sequence + 1u;
    return next == 0u ? 1u : next;
}

/* Each writes a terminated line and returns its length. */
size_t crux_security_event_line_format(const security_event_stored_t *record, int64_t wall_s,
                                       crux_security_event_clock_t clock, char *line,
                                       size_t capacity);
size_t crux_security_event_lost_line_format(uint32_t sequence, uint64_t uptime_ms, bool no_uptime,
                                            int64_t wall_s, crux_security_event_clock_t clock,
                                            bool has_first, uint32_t first, uint32_t count,
                                            char *line, size_t capacity);

/* Ring state, shared by emit, read and drain. Guarded by lock unless atomic. */
typedef struct {
    atomic_bool ready;
    crux_mutex_t lock;
    crux_semaphore_t signal;
    security_event_stored_t slots[CRUX_SECURITY_EVENT_RING];
    size_t head;     /* Next slot to write. */
    size_t pending;  /* Not yet handled by the drain, the newest before head. */
    size_t retained; /* Readable, the newest before head, counting pending ones. */
    uint32_t next_sequence;
    bool lost_numbered;
    uint32_t lost_first;
    uint32_t lost_count;
    atomic_uint_least32_t lost_unnumbered; /* Refused without the lock, so with no number. */
    atomic_uint_least32_t emitted;
    atomic_uint_least32_t dropped;
    atomic_uint_least32_t delivered;
    atomic_uint_least32_t log_errors;
    atomic_uint_least32_t invalid_fields;
    atomic_uint_least32_t high_water;
} security_event_state_t;

extern security_event_state_t crux_security_event_state;

/* Fills wall time and clock for a record of this age. Reads the Time authority, so no ring lock may
 * be held. */
void crux_security_event_clock_fill(uint64_t uptime_ms, bool no_uptime, int64_t *wall_s,
                                    crux_security_event_clock_t *clock);
/* Writes at most limit lines and returns how many. Holds no lock across a log write. */
unsigned crux_security_event_drain_lines(unsigned limit);

#endif
