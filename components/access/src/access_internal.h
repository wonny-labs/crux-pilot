#ifndef CRUX_ACCESS_INTERNAL_H
#define CRUX_ACCESS_INTERNAL_H

#include "access_port.h"

#include <crux/sync.h>

_Static_assert(CRUX_ACCESS_SESSION_MAX >= 2 && CRUX_ACCESS_SESSION_MAX <= 256,
               "the handle holds an 8-bit slot, and slot 0 is reserved for serial");
_Static_assert(CRUX_ACCESS_ACCOUNT_MAX >= 1 && CRUX_ACCESS_ACCOUNT_MAX <= 255,
               "the record holds an 8-bit account count");
_Static_assert(CRUX_ACCESS_SESSIONS_PER_ACCOUNT >= 1, "an account must be able to hold a session");
_Static_assert(CRUX_ACCESS_VERIFIER_ITERATIONS >= 1, "a verifier needs at least one iteration");
_Static_assert(CRUX_ACCESS_KEYS_PER_ACCOUNT >= 1 && CRUX_ACCESS_KEYS_PER_ACCOUNT <= 255,
               "the record holds an 8-bit key count");
_Static_assert(CRUX_ACCESS_KEY_SIZE_MAX >= 1 && CRUX_ACCESS_KEY_SIZE_MAX <= 65535,
               "the record holds a 16-bit key length");
_Static_assert(CRUX_ACCESS_SOURCE_BUCKETS >= 1 && CRUX_ACCESS_NAME_BUCKETS >= 1,
               "delay state needs at least one bucket of each kind");
_Static_assert(CRUX_ACCESS_DELAY_QUIET_MS > CRUX_ACCESS_DELAY_CAP_MS,
               "decay must never run inside a window at the cap");

#define ACCESS_GENERATION_MASK 0xFFFFFFu
#define ACCESS_SLOT_SHIFT 24u
/* Reclaiming can end every session at one login, each with an event. */
#define ACCESS_EXPIRED_EVENTS_MAX ((size_t)CRUX_ACCESS_SESSION_MAX)

typedef struct {
    size_t length;
    uint8_t bytes[CRUX_ACCESS_KEY_SIZE_MAX];
} access_key_t;

/* One account as saved. The change number is not saved: it only detects a change racing a login,
 * and a reset ends every login. */
typedef struct {
    char user[CRUX_ACCESS_NAME_SIZE];
    char role[CRUX_ACCESS_NAME_SIZE];
    bool enabled;
    bool must_change_password;
    uint32_t algorithm;
    uint32_t iterations;
    uint8_t salt[CRUX_ACCESS_SALT_SIZE];
    uint8_t derived[CRUX_ACCESS_DERIVED_SIZE];
    size_t key_count;
    access_key_t keys[CRUX_ACCESS_KEYS_PER_ACCOUNT];
} access_account_t;

typedef struct {
    size_t count;
    access_account_t accounts[CRUX_ACCESS_ACCOUNT_MAX];
} access_table_t;

typedef enum { ACCESS_SLOT_FREE, ACCESS_SLOT_RESERVED, ACCESS_SLOT_ACTIVE } access_slot_state_t;

typedef struct {
    access_slot_state_t state;
    uint32_t generation;
    char user[CRUX_ACCESS_NAME_SIZE];
    size_t role_index;
    bool must_change_password;
    crux_access_origin_t origin;
    uint64_t created_ms;
    uint64_t activity_ms;
    uint64_t sequence; /* Orders sessions created while the clock did not advance. */
} access_slot_t;

typedef struct {
    uint32_t failures;
    uint64_t window_end_ms;
    uint64_t last_failure_ms;
} access_bucket_t;

/* The buckets an attempt from one origin uses. Serial has one bucket and no name state. */
typedef struct {
    access_bucket_t *source;
    access_bucket_t *name;
} access_delay_buckets_t;

typedef struct {
    size_t count;
    crux_access_origin_t origins[ACCESS_EXPIRED_EVENTS_MAX];
    char users[ACCESS_EXPIRED_EVENTS_MAX][CRUX_ACCESS_NAME_SIZE];
} access_expired_t;

typedef struct {
    const crux_access_policy_t *policy;
    bool ready;
    bool store_unusable;
    bool provisioning_open;
    bool locks_created;
    crux_mutex_t state_lock;  /* Guards the RAM tables. Never held across storage or derivation. */
    crux_mutex_t change_lock; /* Serialises account changes. Writers of table hold both locks. */
    access_table_t table;
    uint64_t change_numbers[CRUX_ACCESS_ACCOUNT_MAX];
    uint64_t next_change_number;
    uint64_t next_session_sequence;
    access_slot_t slots[CRUX_ACCESS_SESSION_MAX];
    bool network_verifying;
    bool serial_verifying;
    uint8_t hash_key[CRUX_ACCESS_HASH_KEY_SIZE];
    access_bucket_t source_buckets[CRUX_ACCESS_SOURCE_BUCKETS];
    access_bucket_t name_buckets[CRUX_ACCESS_NAME_BUCKETS];
    access_bucket_t serial_bucket;
    /* Guarded by the change lock, and by init before it returns. Static because they hold the whole
     * table, which a management worker's stack should not have to. Cleared after each use. */
    access_table_t pending;
    uint8_t record[CRUX_ACCESS_RECORD_SIZE_MAX];
} access_state_t;

extern access_state_t crux_access_state;
extern const uint8_t crux_access_dummy_derived[CRUX_ACCESS_DERIVED_SIZE];

/* Shared helpers. */
bool crux_access_name_valid(const char *name);
size_t crux_access_bounded_length(const char *text, size_t limit);
void crux_access_copy_name(char destination[CRUX_ACCESS_NAME_SIZE], const char *source);
void crux_access_secure_zero(void *memory, size_t length);
bool crux_access_equal_constant_time(const uint8_t *left, const uint8_t *right, size_t length);
bool crux_access_lock_state(void);
void crux_access_unlock_state(void);
bool crux_access_lock_change(void);
void crux_access_unlock_change(void);
void crux_access_emit(crux_access_event_type_t type, crux_access_result_t result,
                      const crux_access_origin_t *origin, const char *account, bool role_missing);
bool crux_access_origin_valid(const crux_access_origin_t *origin);
bool crux_access_role_find(const char *role, size_t *index);
bool crux_access_role_is_administrator(size_t role_index);
bool crux_access_slot_permits(const access_slot_t *slot, const char *permission);
access_account_t *crux_access_table_find(access_table_t *table, const char *user, size_t *index);
bool crux_access_password_acceptable(const char *password, size_t length);

/* Session table. The state lock is held for every function here. */
bool crux_access_slot_reserve(const crux_access_origin_t *origin, uint64_t now_ms, size_t *slot,
                              access_expired_t *expired);
void crux_access_slot_cancel(size_t slot);
crux_access_session_t crux_access_slot_activate(size_t slot, const char *user, size_t role_index,
                                                bool must_change_password,
                                                const crux_access_origin_t *origin,
                                                uint64_t now_ms);
void crux_access_slot_release(size_t slot);
/* Finds a live session. An expired one is released, and its details are written to expired. */
access_slot_t *crux_access_slot_find(crux_access_session_t session, uint64_t now_ms,
                                     access_expired_t *expired);
/* Ends an account's sessions. Pass the current session value to keep it, or zero to keep none. */
void crux_access_slots_end_account(const char *user, uint32_t keep_value);
/* Emits the collected events. Call it with no lock held. */
void crux_access_expired_emit(const access_expired_t *expired);

/* Delay state. The state lock is held for every function here except the hashing. */
bool crux_access_delay_buckets(const crux_access_origin_t *origin, const char *user,
                               access_delay_buckets_t *buckets);
uint32_t crux_access_delay_remaining_ms(access_delay_buckets_t buckets, uint64_t now_ms);
void crux_access_delay_record_failure(access_delay_buckets_t buckets, uint64_t now_ms);

/* Account table encoding. Decoding fails closed on anything malformed. */
size_t crux_access_record_encode(const access_table_t *table, uint8_t *record, size_t capacity);
bool crux_access_record_decode(const uint8_t *record, size_t length, access_table_t *table);

/* Verifies an account's password under the login delay and the verification slot, with no session
 * involved. Takes the state lock itself, and needs no lock held. */
crux_access_result_t crux_access_verify_current(const char *user,
                                                const crux_access_origin_t *origin,
                                                const char *password, size_t length,
                                                uint32_t *retry_after_ms, uint64_t *change_number);
/* Step 9 of the login sequence. Takes the change lock itself, and leaves sessions alone. */
void crux_access_change_upgrade(const char *user, uint64_t change_number, const char *password,
                                size_t length);

#endif
