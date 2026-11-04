#ifndef CRUX_ACCESS_PORT_H
#define CRUX_ACCESS_PORT_H

#include <crux_access_adapter.h>

#ifndef CRUX_ACCESS_SESSION_MAX
#define CRUX_ACCESS_SESSION_MAX 8
#endif
#ifndef CRUX_ACCESS_SESSIONS_PER_ACCOUNT
#define CRUX_ACCESS_SESSIONS_PER_ACCOUNT 3
#endif
#ifndef CRUX_ACCESS_ACCOUNT_MAX
#define CRUX_ACCESS_ACCOUNT_MAX 8
#endif
#ifndef CRUX_ACCESS_KEYS_PER_ACCOUNT
#define CRUX_ACCESS_KEYS_PER_ACCOUNT 2
#endif
/* Holds the largest accepted SSH public key blob, ECDSA P-521, at 172 bytes. */
#ifndef CRUX_ACCESS_KEY_SIZE_MAX
#define CRUX_ACCESS_KEY_SIZE_MAX 192
#endif
#ifndef CRUX_ACCESS_SOURCE_BUCKETS
#define CRUX_ACCESS_SOURCE_BUCKETS 32
#endif
#ifndef CRUX_ACCESS_NAME_BUCKETS
#define CRUX_ACCESS_NAME_BUCKETS 32
#endif
/* Reference delay values. A product that changes them must keep the quiet period long enough that
 * waiting it out stays slower than staying under the cap. The build checks only that it outlasts
 * the cap. */
#ifndef CRUX_ACCESS_DELAY_FREE_ATTEMPTS
#define CRUX_ACCESS_DELAY_FREE_ATTEMPTS 5u
#endif
#ifndef CRUX_ACCESS_DELAY_BASE_MS
#define CRUX_ACCESS_DELAY_BASE_MS 1000u
#endif
#ifndef CRUX_ACCESS_DELAY_CAP_MS
#define CRUX_ACCESS_DELAY_CAP_MS 60000u
#endif
#ifndef CRUX_ACCESS_DELAY_QUIET_MS
#define CRUX_ACCESS_DELAY_QUIET_MS 900000u
#endif
/* Placeholder until measured on the target, which needs hardware approval. It is not a claim of
 * meeting general PBKDF2 guidance. */
#ifndef CRUX_ACCESS_VERIFIER_ITERATIONS
#define CRUX_ACCESS_VERIFIER_ITERATIONS 10000u
#endif

#define CRUX_ACCESS_HASH_KEY_SIZE 32u
#define CRUX_ACCESS_HASH_SIZE 32u
#define CRUX_ACCESS_SALT_SIZE 16u
#define CRUX_ACCESS_DERIVED_SIZE 32u
#define CRUX_ACCESS_VERIFIER_PBKDF2_HMAC_SHA256 1u

/* Largest record the store must hold: a header, then every account at its fullest. */
#define CRUX_ACCESS_RECORD_HEADER_SIZE 4u
#define CRUX_ACCESS_RECORD_ACCOUNT_SIZE                                                            \
    (CRUX_ACCESS_NAME_SIZE * 2u + 3u + 4u + CRUX_ACCESS_SALT_SIZE + CRUX_ACCESS_DERIVED_SIZE +     \
     (2u + (size_t)CRUX_ACCESS_KEY_SIZE_MAX) * (size_t)CRUX_ACCESS_KEYS_PER_ACCOUNT)
#define CRUX_ACCESS_RECORD_SIZE_MAX                                                                \
    (CRUX_ACCESS_RECORD_HEADER_SIZE +                                                              \
     CRUX_ACCESS_RECORD_ACCOUNT_SIZE * (size_t)CRUX_ACCESS_ACCOUNT_MAX)

/* A provider reports exactly one state, and a missing medium, read error or corrupt record is
 * UNUSABLE. EMPTY means that a started store holds no account record, because device keys can exist
 * first, and only EMPTY opens provisioning. A blank medium reads as EMPTY only if the store can
 * start on it: a blank card that fails at mount is UNUSABLE. */
typedef enum {
    CRUX_ACCESS_STORE_EMPTY,
    CRUX_ACCESS_STORE_VALID,
    CRUX_ACCESS_STORE_UNUSABLE
} crux_access_store_state_t;

typedef enum {
    CRUX_ACCESS_EVENT_LOGIN_SUCCESS,
    CRUX_ACCESS_EVENT_LOGIN_FAILURE,
    CRUX_ACCESS_EVENT_KEY_FAILURE,
    CRUX_ACCESS_EVENT_DELAY_APPLIED,
    CRUX_ACCESS_EVENT_PERMISSION_DENIED,
    CRUX_ACCESS_EVENT_LOGOUT,
    CRUX_ACCESS_EVENT_SESSION_EXPIRED,
    CRUX_ACCESS_EVENT_PROVISIONED,
    CRUX_ACCESS_EVENT_ACCOUNT_CREATED,
    CRUX_ACCESS_EVENT_ACCOUNT_REMOVED,
    CRUX_ACCESS_EVENT_ACCOUNT_DISABLED,
    CRUX_ACCESS_EVENT_ACCOUNT_ENABLED,
    CRUX_ACCESS_EVENT_ACCOUNT_ROLE_CHANGED,
    CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_RESET,
    CRUX_ACCESS_EVENT_ACCOUNT_PASSWORD_CHANGED,
    CRUX_ACCESS_EVENT_ACCOUNT_KEY_ADDED,
    CRUX_ACCESS_EVENT_ACCOUNT_KEY_REMOVED,
    CRUX_ACCESS_EVENT_LOGIN_BUSY,
    CRUX_ACCESS_EVENT_LOGIN_NO_SESSION,
    CRUX_ACCESS_EVENT_LOGIN_REFUSED,
    CRUX_ACCESS_EVENT_NOT_READY,
    CRUX_ACCESS_EVENT_SESSION_INVALID,
    CRUX_ACCESS_EVENT_PROVISION_REFUSED,
    CRUX_ACCESS_EVENT_CHANGE_FAILED,
    CRUX_ACCESS_EVENT_CHANGE_REJECTED
} crux_access_event_type_t;

/* Names only known accounts, so that a password typed into the name field is never reported. No
 * event holds a password, verifier, salt or key. The product provider and the final identifiers are
 * in components/security_event. */
typedef struct {
    crux_access_event_type_t type;
    crux_access_result_t result;
    crux_access_origin_t origin;         /* Channel zero when the channel is not known. */
    char actor[CRUX_ACCESS_NAME_SIZE];   /* The acting session's user, or empty. */
    char account[CRUX_ACCESS_NAME_SIZE]; /* The login's account or the change's target, or empty. */
    char permission[CRUX_ACCESS_PERMISSION_MAX + 1u]; /* Set for a permission denial. */
    bool role_missing; /* The account's role is no longer in the policy. */
} crux_access_event_t;

/* Private provider contract, implemented by a test composition or a product provider. Calls come
 * from thread context with no lock held, with two exceptions: the store runs under the change lock,
 * and the clock is read under the state lock, so that times compared under it are in order. Events
 * always run after both locks are released, and can arrive before a successful init. The clock and
 * the event port must not wait on storage, because permission checks use them. */
bool crux_access_port_derive(uint32_t algorithm, uint32_t iterations,
                             const uint8_t salt[CRUX_ACCESS_SALT_SIZE], const char *password,
                             size_t length, uint8_t derived[CRUX_ACCESS_DERIVED_SIZE]);
bool crux_access_port_random(uint8_t *output, size_t length);
bool crux_access_port_hmac(const uint8_t key[CRUX_ACCESS_HASH_KEY_SIZE], const uint8_t *data,
                           size_t length, uint8_t hash[CRUX_ACCESS_HASH_SIZE]);
uint64_t crux_access_port_now_ms(void);
/* Writes *length bytes into record for VALID, and nothing otherwise. */
crux_access_store_state_t crux_access_port_store_load(uint8_t *record, size_t capacity,
                                                      size_t *length);
/* Completes, or leaves the previous record readable across power loss. False promises only that
 * RAM stays unchanged: stored bytes can change, and a failed save can win at the next boot if the
 * store cannot then invalidate it. */
bool crux_access_port_store_save(const uint8_t *record, size_t length);
void crux_access_port_event(const crux_access_event_t *event);

#endif
