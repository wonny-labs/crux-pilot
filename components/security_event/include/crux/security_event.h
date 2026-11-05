/**
 * @file
 * @brief Bounded records of management activity, with stable identifiers and no secrets.
 *
 * Thread context only. Strings are copied and not retained. An emit never waits on storage: it
 * copies a record into a RAM ring under a short leaf lock, and a low-priority thread hands the ring
 * to Logging. It is not free of every wait, because Sync's own bookkeeping has no time bound. The
 * composition calls crux_security_event_init() and crux_security_event_start() from one thread.
 *
 * Records are a record for an operator, not tamper-proof and not guaranteed complete. Code in the
 * image can emit false records or none. The ring is RAM only, so a reset loses it. See
 * docs/security-event-contract.md.
 */
#ifndef CRUX_SECURITY_EVENT_H
#define CRUX_SECURITY_EVENT_H

#include <crux/access.h>
#include <crux/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Buffer size for a record's user, including the terminator. */
#define CRUX_SECURITY_EVENT_USER_SIZE 32u
/** Buffer size for a record's subject, including the terminator. */
#define CRUX_SECURITY_EVENT_SUBJECT_SIZE 49u
/** Longest identifier name, excluding the terminator. */
#define CRUX_SECURITY_EVENT_NAME_MAX 24u
/** Permission that crux_security_event_read() requires. */
#define CRUX_SECURITY_EVENT_PERMISSION_READ "events.read"

/** Stable event identifier. A value is never reused. Projects use 0x8000 and above. */
typedef uint16_t crux_security_event_id_t;

/** Identifiers owned by Crux, listed with their names and classes in the contract. */
enum {
    CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_OK = 0x0100,
    CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED = 0x0101,
    CRUX_SECURITY_EVENT_ID_ACCESS_KEY_FAILED = 0x0102,
    CRUX_SECURITY_EVENT_ID_ACCESS_DELAYED = 0x0103,
    CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_BUSY = 0x0104,
    CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_NO_SESSION = 0x0105,
    CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_REFUSED = 0x0106,
    CRUX_SECURITY_EVENT_ID_ACCESS_NOT_READY = 0x0107,
    CRUX_SECURITY_EVENT_ID_ACCESS_PERMISSION_DENIED = 0x0108,
    CRUX_SECURITY_EVENT_ID_ACCESS_SESSION_INVALID = 0x0109,
    CRUX_SECURITY_EVENT_ID_ACCESS_LOGOUT = 0x010A,
    CRUX_SECURITY_EVENT_ID_ACCESS_SESSION_EXPIRED = 0x010B,
    CRUX_SECURITY_EVENT_ID_ACCESS_PROVISIONED = 0x010C,
    CRUX_SECURITY_EVENT_ID_ACCESS_PROVISION_REFUSED = 0x010D,
    CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_CREATED = 0x0110,
    CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_REMOVED = 0x0111,
    CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_DISABLED = 0x0112,
    CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_ENABLED = 0x0113,
    CRUX_SECURITY_EVENT_ID_ACCESS_ROLE_CHANGED = 0x0114,
    CRUX_SECURITY_EVENT_ID_ACCESS_PASSWORD_RESET = 0x0115,
    CRUX_SECURITY_EVENT_ID_ACCESS_PASSWORD_CHANGED = 0x0116,
    CRUX_SECURITY_EVENT_ID_ACCESS_KEY_ADDED = 0x0117,
    CRUX_SECURITY_EVENT_ID_ACCESS_KEY_REMOVED = 0x0118,
    CRUX_SECURITY_EVENT_ID_ACCESS_CHANGE_FAILED = 0x0119,
    CRUX_SECURITY_EVENT_ID_ACCESS_CHANGE_REJECTED = 0x011A,
    CRUX_SECURITY_EVENT_ID_CREDENTIAL_STATE = 0x0200,
    CRUX_SECURITY_EVENT_ID_CREDENTIAL_SLOT_INVALID = 0x0201,
    CRUX_SECURITY_EVENT_ID_CREDENTIAL_SAVE_FAILED = 0x0202,
    CRUX_SECURITY_EVENT_ID_CREDENTIAL_KEY_GENERATED = 0x0203,
    CRUX_SECURITY_EVENT_ID_CREDENTIAL_KEY_REPLACED = 0x0204,
    CRUX_SECURITY_EVENT_ID_CREDENTIAL_ERASED = 0x0205,
    CRUX_SECURITY_EVENT_ID_FILE_CREATED = 0x0300,
    CRUX_SECURITY_EVENT_ID_FILE_REMOVED = 0x0301,
    CRUX_SECURITY_EVENT_ID_FILE_RENAMED = 0x0302,
    CRUX_SECURITY_EVENT_ID_FILE_PARTIAL_REMOVED = 0x0303,
    CRUX_SECURITY_EVENT_ID_FILE_RECOVERY_NEEDED = 0x0304,
    CRUX_SECURITY_EVENT_ID_FILE_NAME_REJECTED = 0x0305,
    CRUX_SECURITY_EVENT_ID_FILE_COMMIT_FAILED = 0x0306,
    CRUX_SECURITY_EVENT_ID_FILE_STORAGE_FAILED = 0x0307,
    CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED = 0x0400,
    CRUX_SECURITY_EVENT_ID_CONFIG_REJECTED = 0x0401,
    CRUX_SECURITY_EVENT_ID_CONFIG_SAVED = 0x0402,
    CRUX_SECURITY_EVENT_ID_CONFIG_SAVE_FAILED = 0x0403,
    CRUX_SECURITY_EVENT_ID_CLI_LINE_REJECTED = 0x0500,
    CRUX_SECURITY_EVENT_ID_CLI_CONSOLE_ERROR = 0x0501,
    CRUX_SECURITY_EVENT_ID_WEB_TLS_FAILED = 0x0600,
    CRUX_SECURITY_EVENT_ID_WEB_CSRF_REJECTED = 0x0601,
    CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REPLACED = 0x0602,
    CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_REJECTED = 0x0603,
    CRUX_SECURITY_EVENT_ID_WEB_CERTIFICATE_UNUSABLE = 0x0604,
    CRUX_SECURITY_EVENT_ID_SSH_AUTH_LIMIT = 0x0700,
    CRUX_SECURITY_EVENT_ID_SSH_HOST_KEY_MISSING = 0x0701,
    CRUX_SECURITY_EVENT_ID_NETWORK_CHANGED = 0x0800,
    CRUX_SECURITY_EVENT_ID_NETWORK_SAVE_FAILED = 0x0801,
    CRUX_SECURITY_EVENT_ID_NETWORK_SAVED_IGNORED = 0x0802,
    CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_REQUESTED = 0x0900,
    CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_REFUSED = 0x0901,
    CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_FAILED = 0x0902,
    CRUX_SECURITY_EVENT_ID_EVENT_UNKNOWN = 0x0F00,
    CRUX_SECURITY_EVENT_ID_EVENT_LOST = 0x0F01, /**< A line only. An emit cannot use it. */
    CRUX_SECURITY_EVENT_ID_PROJECT_FIRST = 0x8000
};

/** What happened, in a record's terms. */
typedef enum {
    CRUX_SECURITY_EVENT_OK = 0,
    CRUX_SECURITY_EVENT_DENIED = 1,  /**< Credential or permission refused. */
    CRUX_SECURITY_EVENT_DELAYED = 2, /**< Inside a delay window. */
    CRUX_SECURITY_EVENT_BUSY = 3,
    CRUX_SECURITY_EVENT_LIMIT = 4,   /**< A table or declared bound. */
    CRUX_SECURITY_EVENT_INVALID = 5, /**< Input that the rules refused. */
    CRUX_SECURITY_EVENT_FAILED = 6   /**< Storage, resource or internal failure. */
} crux_security_event_result_t;

/** Admission class. It comes from a fixed table per identifier, and an emit cannot choose it. */
typedef enum {
    CRUX_SECURITY_EVENT_ROUTINE = 0,
    CRUX_SECURITY_EVENT_CHANGE = 1 /**< Still admitted when the ring is nearly full. */
} crux_security_event_class_t;

/**
 * One event to record. Client input that failed validation must never be a field.
 *
 * A field that fails its rule is omitted and counted, and the event is still recorded.
 */
typedef struct {
    crux_security_event_id_t id; /**< Unknown or undeclared values record as event.unknown. */
    crux_security_event_result_t result;
    uint8_t channel;       /**< 1 for serial, 2 for web, 3 for SSH, or 0 for none. */
    uint8_t source_length; /**< 0, 4 or 8. */
    uint8_t source[16];    /**< As given by the adapter. For IPv6 this is the /64 prefix. */
    const char *user;      /**< NULL or a known account's name, of 1 to 31 [a-z0-9_.-]. Copied. */
    /**
     * NULL, or a permission, parameter, area or command of [A-Za-z0-9_./:-]. Copied. Text over 48
     * bytes keeps 47 and ends in ~, which is outside the rule, so it cannot pass for a whole one.
     */
    const char *subject;
} crux_security_event_t;

/** Where a record's wall time came from. It states the clock's claim, not its accuracy. */
typedef enum {
    CRUX_SECURITY_EVENT_CLOCK_NONE = 0, /**< Not set, or unusable at the time of reading. */
    CRUX_SECURITY_EVENT_CLOCK_UNKNOWN = 1,
    CRUX_SECURITY_EVENT_CLOCK_RTC = 2,
    CRUX_SECURITY_EVENT_CLOCK_MANUAL = 3,
    CRUX_SECURITY_EVENT_CLOCK_EXTERNAL = 4
} crux_security_event_clock_t;

/** One record as read back. */
typedef struct {
    crux_security_event_id_t id;
    crux_security_event_result_t result;
    uint8_t channel;
    uint8_t source_length;
    uint8_t source[16];
    char user[CRUX_SECURITY_EVENT_USER_SIZE];       /**< Empty if none. */
    char subject[CRUX_SECURITY_EVENT_SUBJECT_SIZE]; /**< Empty if none. */
    uint32_t sequence;                              /**< From 1 each boot. Skips 0 on a wrap. */
    uint64_t uptime_ms; /**< Monotonic. Zero with no_uptime set if the clock read failed. */
    int64_t wall_s;     /**< Unix seconds, computed at read. Meaningful unless clock is NONE. */
    crux_security_event_clock_t clock; /**< Filled at read. NONE if no_uptime. */
    bool no_uptime;
    bool subject_truncated;
} crux_security_event_record_t;

/** A project's identifier, for its name in a line and its admission class. */
typedef struct {
    crux_security_event_id_t id; /**< 0x8000 and above. */
    /** 1 to 24 of [a-z0-9_.-], and no name Crux uses. Borrowed for the program's life. */
    const char *name;
    crux_security_event_class_t event_class;
} crux_security_event_project_id_t;

/** Results of crux_security_event_read(). END is not a failure. */
typedef enum {
    CRUX_SECURITY_READ_OK = 0,
    CRUX_SECURITY_READ_END = 1,
    CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT = -1,
    CRUX_SECURITY_READ_ERR_NOT_READY = -2,
    CRUX_SECURITY_READ_ERR_SESSION_INVALID = -3,
    CRUX_SECURITY_READ_ERR_DENIED = -4
} crux_security_event_read_result_t;

/** Boot-local counts, each wrapping at 2^32. Read one by one, so not one consistent instant. */
typedef struct {
    uint32_t emitted;    /**< Admitted to the ring. */
    uint32_t dropped;    /**< Refused at the ring, or never reached it. */
    uint32_t delivered;  /**< Lines for which crux_log_write returned CRUX_OK. Not on the wire. */
    uint32_t log_errors; /**< Lines for which crux_log_write returned an error. */
    uint32_t invalid_fields;
    uint32_t high_water; /**< Most records ever pending. */
} crux_security_event_stats_t;

/**
 * Prepare the ring. Call it first, before any component that emits.
 *
 * Creates the ring lock and the drain signal, and validates the project table. Until it succeeds,
 * every emit counts as dropped. A failure can be retried, and a composition treats it as a
 * management start failure.
 *
 * @param project_ids Project identifiers, borrowed for the program's life. May be NULL when count
 * is zero.
 * @param count Number of entries.
 * @retval CRUX_OK Ready.
 * @retval CRUX_ERR_INVALID_ARGUMENT The table is missing, or holds an identifier below 0x8000, a
 * repeated identifier or name, a bad name, a name Crux uses, or an unknown class.
 * @retval CRUX_ERR_INVALID_STATE Already initialised; nothing changed.
 * @return A Sync error if the lock or signal cannot be created, such as a full pool.
 */
crux_status_t crux_security_event_init(const crux_security_event_project_id_t *project_ids,
                                       size_t count);

/**
 * Record an event. Returns nothing, and a security decision never depends on whether it was kept.
 *
 * Takes the ring lock for up to 10 ms, by default. It does no file, Settings or credential access,
 * no verifier work and no log output. It can run before crux_security_event_start(), and records
 * wait in the ring until then. A call from an ISR, a NULL event or a call before a successful init
 * is refused and counted as dropped.
 *
 * @param event Borrowed for the call.
 */
void crux_security_event_emit(const crux_security_event_t *event);

/**
 * Start the low-priority thread that writes records through Logging.
 *
 * If it fails, the ring still fills and can be read. It can be retried, and a composition treats it
 * as a management start failure.
 *
 * @retval CRUX_OK Started.
 * @retval CRUX_ERR_NOT_READY crux_security_event_init() has not succeeded.
 * @retval CRUX_ERR_INVALID_STATE Already started.
 * @return A Thread error if the thread cannot be created.
 */
crux_status_t crux_security_event_start(void);

/**
 * Copy the counts. Takes no session, because it returns counts only. Needs no initialisation, takes
 * no lock and does not wait, so the status service can call it from any thread.
 *
 * @param[out] out Unchanged on failure.
 * @retval CRUX_OK Copied.
 * @retval CRUX_ERR_INVALID_ARGUMENT out is NULL.
 */
crux_status_t crux_security_event_get_stats(crux_security_event_stats_t *out);

/**
 * Read the next retained record at or after the cursor, without removing it.
 *
 * Needs the permission events.read, checked before the ring lock is taken, so a denial leaves its
 * own event. If the record the cursor names is already gone, the next retained one is returned with
 * a higher sequence, so that the caller sees the gap. Numbers wrap, so compare them by difference.
 *
 * @param session Reading session.
 * @param[in,out] cursor Starts at zero, and holds the next sequence to read. Unchanged unless the
 * result is CRUX_SECURITY_READ_OK. A cursor ahead of the numbering restarts at the oldest record,
 * so the caller sees the sequence fall. A cursor from before a reset is ahead only until the
 * numbering passes it, and after that it reads as an ordinary cursor, so this does not detect every
 * reset.
 * @param[out] out Unchanged unless the result is CRUX_SECURITY_READ_OK.
 * @retval CRUX_SECURITY_READ_OK A record was copied.
 * @retval CRUX_SECURITY_READ_END No record at or after the cursor.
 * @retval CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT The cursor or the output is NULL.
 * @retval CRUX_SECURITY_READ_ERR_NOT_READY Not initialised, or the ring or access control could not
 * answer in time.
 * @retval CRUX_SECURITY_READ_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_SECURITY_READ_ERR_DENIED The session lacks events.read.
 */
crux_security_event_read_result_t crux_security_event_read(crux_access_session_t session,
                                                           uint32_t *cursor,
                                                           crux_security_event_record_t *out);

/**
 * Name an identifier, for a management client that shows a record.
 *
 * Takes no lock and does not wait.
 *
 * @param id A Crux identifier, or one declared to crux_security_event_init().
 * @return The name, valid for the program's life, or "event.unknown" for any other value.
 */
const char *crux_security_event_name(crux_security_event_id_t id);

#ifdef __cplusplus
}
#endif

#endif
