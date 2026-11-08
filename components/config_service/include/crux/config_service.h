/**
 * @file
 * @brief Permission-checked listing, reading, updating and saving of Configuration parameters.
 *
 * Web and CLI use the same operations, and so the same permissions and text forms. Thread context
 * only. crux_config_service_start() must return before any other operation starts.
 *
 * A successful update changes the accepted value in memory only. It applies nothing to a running
 * service: applying a change safely is application policy, and the activation in each result says
 * what the application declared. Sensitive means write-only through this service. It is not
 * encryption: the value stays plain in RAM and in the record, with no confidentiality claim. Crux
 * accounts and device keys never use it.
 *
 * Once a parameter is in a started table, every thread uses this service for it. After start no
 * thread calls crux_config_read, _update, _load or _save on such a parameter, or crux_config_save
 * or crux_config_load on any other, because the provider is caller-serialised too. A parameter the
 * application keeps private goes in the table under a permission no role holds. See
 * docs/status-configuration-services.md.
 */
#ifndef CRUX_CONFIG_SERVICE_H
#define CRUX_CONFIG_SERVICE_H

#include <crux/access.h>
#include <crux/configuration.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Most entries in a table. At most 32, which also sizes the private record of unsaved updates. */
#ifndef CRUX_CONFIG_SERVICE_MAX
#define CRUX_CONFIG_SERVICE_MAX 32
#endif

/** Permission that crux_config_service_save() requires. */
#define CRUX_CONFIG_SERVICE_PERMISSION_SAVE "config.save"

/** A buffer of this size always holds a value's text and its terminator. */
#define CRUX_CONFIG_SERVICE_TEXT_SIZE (CRUX_CONFIG_STRING_MAX + 1u)

/** When the application means to act on an accepted value. Metadata: the service never acts on it.
 */
typedef enum {
    CRUX_CONFIG_ACTIVATION_APPLICATION = 0, /**< The application picks it up by its own rule. */
    CRUX_CONFIG_ACTIVATION_RESTART = 1,     /**< Takes effect after a restart. */
    CRUX_CONFIG_ACTIVATION_SAFE_STATE = 2   /**< The application applies it only in a safe state. */
} crux_config_activation_t;

/** One parameter a composition exposes. The table and its strings stay borrowed for the program. */
typedef struct {
    crux_config_param_t *parameter; /**< Initialised, and loaded by the application first. */
    const char *read_permission;    /**< Required. */
    const char *write_permission;   /**< NULL for read-only. */
    bool sensitive; /**< Write-only through the service. Needs a write permission. */
    crux_config_activation_t activation;
} crux_config_service_entry_t;

/** Results; zero is success. CRUX_CONFIG_SERVICE_END ends a listing and is not a failure. */
typedef enum {
    CRUX_CONFIG_SERVICE_OK = 0,
    CRUX_CONFIG_SERVICE_END = 1,                   /**< Listing finished. Item unchanged. */
    CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT = -1, /**< Includes text that does not parse. */
    CRUX_CONFIG_SERVICE_ERR_NOT_READY = -2,
    CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID = -3,
    CRUX_CONFIG_SERVICE_ERR_DENIED = -4,
    CRUX_CONFIG_SERVICE_ERR_NOT_FOUND = -5, /**< Missing, or a parameter the session may not see. */
    CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY = -6, /**< Sensitive, so never readable. */
    CRUX_CONFIG_SERVICE_ERR_REJECTED = -7,   /**< Validator or character rule. Old value kept. */
    CRUX_CONFIG_SERVICE_ERR_BUSY = -8,       /**< The lock or Sync metadata is held. Retry later. */
    CRUX_CONFIG_SERVICE_ERR_STORE = -9,      /**< A save failed. See the report. */
    CRUX_CONFIG_SERVICE_ERR_TOO_SMALL = -10  /**< CRUX_CONFIG_SERVICE_TEXT_SIZE is always enough. */
} crux_config_service_result_t;

/** A parameter as this session may know it. It never holds a value. */
typedef struct {
    const char *name; /**< The parameter's key, borrowed for the program. */
    crux_config_type_t type;
    crux_config_activation_t activation;
    bool sensitive;
    bool readable; /**< By this session. Always false when sensitive. */
    bool writable; /**< By this session. */
    bool unsaved;  /**< Accepted update since the last acknowledged save. False unless writable. */
} crux_config_service_item_t;

/**
 * What a save did. Parameters saved before a failure stay saved: there is no atomicity. A session's
 * report tells it only what it may see, and an application's report tells it everything.
 */
typedef struct {
    size_t saved; /**< For a session, only those it can read or write, which can be fewer. */
    /**
     * NULL if every save succeeded, and also if the session cannot read or write the parameter that
     * failed. So test the result, and not this, to learn whether a save failed.
     */
    const char *failed_name;
    crux_config_result_t failed_result; /**< CRUX_CONFIG_OK if every save succeeded. */
} crux_config_service_save_report_t;

/**
 * Validate a table once and start the service. The service never loads a parameter.
 *
 * Refuses more than CRUX_CONFIG_SERVICE_MAX entries, a parameter that is not initialised, a
 * duplicate name, a missing, empty or over-long permission, sensitive with no write permission and
 * an unknown activation. After a refused table every operation returns
 * CRUX_CONFIG_SERVICE_ERR_NOT_READY, and a corrected table can still start.
 *
 * @param entries Table, borrowed for the program's life. May be NULL when count is zero.
 * @param count Number of entries.
 * @retval CRUX_CONFIG_SERVICE_OK Started.
 * @retval CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT The table is refused, or the service has already
 * started, which changes nothing.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_READY The lock could not be created.
 */
crux_config_service_result_t crux_config_service_start(const crux_config_service_entry_t *entries,
                                                       size_t count);

/**
 * List the next parameter this session can read or write, in table order.
 *
 * Hiding the others leaves no event. Polls the lock, so it never waits on a save. On a save it
 * returns CRUX_CONFIG_SERVICE_ERR_BUSY with the cursor unchanged.
 *
 * @param session Session asking.
 * @param[in,out] cursor Starts at zero. Unchanged unless the result is CRUX_CONFIG_SERVICE_OK. It
 * counts entries this session cannot see, so an adapter keeps it and never sends it to a client.
 * @param[out] item Unchanged unless the result is CRUX_CONFIG_SERVICE_OK.
 * @retval CRUX_CONFIG_SERVICE_OK An item was copied.
 * @retval CRUX_CONFIG_SERVICE_END No more parameters.
 * @retval CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT A pointer is missing.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_READY Not started, or a lock failed.
 * @retval CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_CONFIG_SERVICE_ERR_BUSY The lock is held. Retry later.
 */
crux_config_service_result_t crux_config_service_next(crux_access_session_t session, size_t *cursor,
                                                      crux_config_service_item_t *item);

/**
 * Read a parameter as text: true or false, decimal, or a string's bytes. Polls the lock.
 *
 * @param session Session asking.
 * @param name The parameter's key.
 * @param[out] text Receives the text and a terminator. Unchanged on failure.
 * @param capacity Size of text. CRUX_CONFIG_SERVICE_TEXT_SIZE is always enough.
 * @param[out] length Bytes before the terminator. Unchanged on failure.
 * @param[out] type The parameter's type, which a web adapter uses to choose its JSON form.
 * @retval CRUX_CONFIG_SERVICE_OK Copied.
 * @retval CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT A pointer is missing.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_READY Not started, or a lock failed.
 * @retval CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_CONFIG_SERVICE_ERR_DENIED The session may write but not read this parameter.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_FOUND Missing, or a parameter the session cannot see, which
 * records a denial.
 * @retval CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY Sensitive, whichever permissions the session holds.
 * @retval CRUX_CONFIG_SERVICE_ERR_BUSY The lock is held. Retry later.
 * @retval CRUX_CONFIG_SERVICE_ERR_TOO_SMALL The value does not fit in capacity.
 */
crux_config_service_result_t crux_config_service_read(crux_access_session_t session,
                                                      const char *name, char *text, size_t capacity,
                                                      size_t *length, crux_config_type_t *type);

/**
 * Parse text for the parameter's type and update its accepted value. Polls the lock.
 *
 * BOOL takes true or false. U32 takes decimal with no sign, leading zeros or overflow. STRING takes
 * the bytes as given, and text needs no terminator. A successful update applies nothing: see the
 * activation in applied. Neither the text nor the value reaches an event or a result.
 *
 * @param session Session asking.
 * @param name The parameter's key.
 * @param text The new value, borrowed for the call. An empty string needs a non-NULL pointer.
 * @param length Bytes in text.
 * @param[out] applied Metadata only, set when the result is CRUX_CONFIG_SERVICE_OK.
 * @retval CRUX_CONFIG_SERVICE_OK Accepted, and marked as unsaved.
 * @retval CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT A pointer is missing, or the text does not parse
 * for the type. The old value is kept.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_READY Not started, or a lock failed.
 * @retval CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_CONFIG_SERVICE_ERR_DENIED The session can see but not write this parameter.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_FOUND Missing, or a parameter the session cannot see, which
 * records a denial.
 * @retval CRUX_CONFIG_SERVICE_ERR_REJECTED The validator, the character rule or the capacity
 * refused it. The old value is kept.
 * @retval CRUX_CONFIG_SERVICE_ERR_BUSY The lock is held. Retry later.
 */
crux_config_service_result_t crux_config_service_update(crux_access_session_t session,
                                                        const char *name, const char *text,
                                                        size_t length,
                                                        crux_config_service_item_t *applied);

/**
 * Save each unsaved parameter in table order, stopping at the first failure.
 *
 * Needs CRUX_CONFIG_SERVICE_PERMISSION_SAVE. Waits a finite time for the lock and holds it across
 * storage, so it runs on a worker, SSH session or console thread and never on the HTTP server
 * thread. A failed save may already have changed stored bytes, so its mark stays on.
 *
 * It writes every marked parameter, including those this session cannot see and those the
 * application marked with crux_config_service_try_update(). Its events name them all, for the
 * operator. Its report does not: a parameter the session cannot read or write stays out of
 * report.saved and report.failed_name, as it stays out of a listing. The session still learns from
 * CRUX_CONFIG_SERVICE_ERR_STORE and report.failed_result that some save failed.
 *
 * @param session Session asking.
 * @param[out] report Set whenever the session is permitted and the report is present.
 * @retval CRUX_CONFIG_SERVICE_OK Every unsaved parameter was saved, which may be none.
 * @retval CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT The report is missing.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_READY Not started, or a lock failed. Nothing was saved.
 * @retval CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_CONFIG_SERVICE_ERR_DENIED The session lacks the permission.
 * @retval CRUX_CONFIG_SERVICE_ERR_BUSY Another save held the lock throughout the wait.
 * @retval CRUX_CONFIG_SERVICE_ERR_STORE A save failed. The report names it if the session can see
 * it, and with no provider its result is CRUX_CONFIG_ERR_NOT_SUPPORTED.
 */
crux_config_service_result_t crux_config_service_save(crux_access_session_t session,
                                                      crux_config_service_save_report_t *report);

/**
 * Read a value for the owning application, sensitive ones included. No session and no event. Polls
 * the lock and never waits on it or on storage, so a control thread can call it and keep its last
 * snapshot on CRUX_CONFIG_SERVICE_ERR_BUSY.
 *
 * @param name The parameter's key.
 * @param[out] out The typed value. A string's text points at text_buffer. Unchanged on failure.
 * @param text_buffer Receives a string and its terminator. Unused for a scalar, and may be NULL.
 * @param capacity Size of text_buffer.
 * @retval CRUX_CONFIG_SERVICE_OK Copied.
 * @retval CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT A needed pointer is missing.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_READY Not started, or a lock failed.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_FOUND No such parameter.
 * @retval CRUX_CONFIG_SERVICE_ERR_BUSY The lock is held.
 * @retval CRUX_CONFIG_SERVICE_ERR_TOO_SMALL A string does not fit in capacity.
 */
crux_config_service_result_t crux_config_service_try_read(const char *name,
                                                          crux_config_value_t *out,
                                                          char *text_buffer, size_t capacity);

/**
 * Update a value for the owning application, and mark it unsaved. No session and no event, so a
 * control thread never meets the event lock. Polls the lock and never waits on it or on storage.
 *
 * @param name The parameter's key.
 * @param candidate The new typed value. A string needs a terminated text.
 * @retval CRUX_CONFIG_SERVICE_OK Accepted.
 * @retval CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT A pointer is missing.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_READY Not started, or a lock failed.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_FOUND No such parameter.
 * @retval CRUX_CONFIG_SERVICE_ERR_REJECTED The type, the validator, the character rule or the
 * capacity refused it. The old value is kept.
 * @retval CRUX_CONFIG_SERVICE_ERR_BUSY The lock is held.
 */
crux_config_service_result_t crux_config_service_try_update(const char *name,
                                                            const crux_config_value_t *candidate);

/**
 * Save for the owning application, on a thread that may wait on storage. No session, no permission
 * and no event. Otherwise as crux_config_service_save(), so table order decides what is saved last,
 * except that its report counts and names everything, because the application owns the table.
 *
 * @param[out] report Set unless it is missing.
 * @retval CRUX_CONFIG_SERVICE_OK Every unsaved parameter was saved, which may be none.
 * @retval CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT The report is missing.
 * @retval CRUX_CONFIG_SERVICE_ERR_NOT_READY Not started, or a lock failed. Nothing was saved.
 * @retval CRUX_CONFIG_SERVICE_ERR_BUSY Another save held the lock throughout the wait.
 * @retval CRUX_CONFIG_SERVICE_ERR_STORE A save failed. The report names it.
 */
crux_config_service_result_t
crux_config_service_save_local(crux_config_service_save_report_t *report);

#ifdef __cplusplus
}
#endif

#endif
