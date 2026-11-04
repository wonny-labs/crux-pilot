/**
 * @file
 * @brief Permission-filtered status items for management channels.
 *
 * Built-in items report the system, the reset cause the boot owner captured, the network interface,
 * versions and, where a composition links them, the credential store and security events. A project
 * adds its own items. Web and CLI read the same items through crux_mgmt_status_next().
 *
 * Thread context only. crux_mgmt_status_start() must return before any other operation starts, and
 * then any number of threads may call the others. No call waits on storage or produces an event.
 * System and clock items answer from a snapshot read at most once per refresh period, so a client's
 * polling rate does not set how often they take locks that control threads also take. See
 * docs/status-configuration-services.md for what remains unproven.
 */
#ifndef CRUX_MANAGEMENT_STATUS_H
#define CRUX_MANAGEMENT_STATUS_H

#include <crux/access.h>
#include <crux/status.h>
#include <crux/system.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Permission every built-in item needs. */
#define CRUX_MGMT_STATUS_PERMISSION_READ "status.read"
/** Size of a text value, including its terminator, so 63 bytes of text. */
#define CRUX_MGMT_STATUS_TEXT_SIZE 64u
/** Most project items in a configuration. */
#define CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX 32u
/** Longest project item name, excluding the terminator. Built-in names can be longer. */
#define CRUX_MGMT_STATUS_NAME_MAX 31u
/** A buffer of this size always holds one item as JSON, with a terminator. */
#define CRUX_MGMT_STATUS_JSON_ENTRY_SIZE 256u

typedef enum {
    CRUX_MGMT_STATUS_BOOL = 1,
    CRUX_MGMT_STATUS_U32,
    CRUX_MGMT_STATUS_U64,
    CRUX_MGMT_STATUS_TEXT
} crux_mgmt_status_type_t;

/** Tagged value. Use the union member selected by type. */
typedef struct {
    crux_mgmt_status_type_t type;
    union {
        bool boolean;
        uint32_t u32;
        uint64_t u64;
        /** NUL-terminated and under the character rule of crux_config_text_valid. */
        char text[CRUX_MGMT_STATUS_TEXT_SIZE];
    } data;
} crux_mgmt_status_value_t;

/**
 * A project item's source. Must not block or touch storage. Nothing enforces this, so review does.
 * Runs on the caller's thread, and may be called concurrently.
 *
 * @param context The item's context.
 * @param[out] out Zeroed before the call. A wrong type or bad text makes the item CRUX_ERR_IO.
 * @return CRUX_OK, or the item's failure, which its entry then carries. Any value that is not a
 * crux_status_t error becomes CRUX_ERR_IO.
 */
typedef crux_status_t (*crux_mgmt_status_read_t)(void *context, crux_mgmt_status_value_t *out);

/** One item a project adds. The table and its strings stay borrowed for the program's life. */
typedef struct {
    /** [a-z0-9_.-], 1 to 31 characters. "system.", "reset.", "net.", "version." and "security."
     * are reserved. */
    const char *name;
    const char *permission; /**< Required. There is no default. */
    crux_mgmt_status_read_t read;
    void *context;
} crux_mgmt_status_item_t;

/** What the composition supplies. The strings and status are copied at start. */
typedef struct {
    const char
        *platform_version;       /**< From the build. At most 63 bytes, under the character rule. */
    const char *project_version; /**< The same. */
    crux_status_t boot_reset_cause_status;        /**< What the boot diagnostic owner captured. */
    crux_reset_cause_t boot_reset_cause;          /**< Meaningful when the status is CRUX_OK. */
    const crux_mgmt_status_item_t *project_items; /**< May be NULL when the count is zero. */
    size_t project_item_count;
} crux_mgmt_status_config_t;

/** One item as a session sees it. */
typedef struct {
    const char *name;               /**< Borrowed for the program's life. */
    crux_status_t status;           /**< Per item, so one failure does not hide the rest. */
    crux_mgmt_status_value_t value; /**< Meaningful when status is CRUX_OK, otherwise zero. */
} crux_mgmt_status_entry_t;

/**
 * Validate the configuration and start the service. Never reads the reset cause itself: the boot
 * diagnostic owner calls crux_system_reset_cause_get() once and passes what it captured, so that a
 * status poll cannot reopen the System contract's one permission to clear.
 *
 * Refuses an over-long or bad version string and an invalid project table: a bad or duplicate name,
 * a reserved prefix, a missing or over-long permission, a missing source or too many items. After a
 * refusal every operation returns CRUX_ERR_NOT_READY, and a corrected configuration can still
 * start.
 *
 * @param config Borrowed for the call, except its project table.
 * @retval CRUX_OK Started.
 * @retval CRUX_ERR_INVALID_ARGUMENT The configuration is refused.
 * @retval CRUX_ERR_INVALID_STATE Already started; nothing changed.
 * @return A Sync error if the snapshot's lock cannot be created.
 */
crux_status_t crux_mgmt_status_start(const crux_mgmt_status_config_t *config);

/**
 * Copy the next item this session may read. An item it may not read is skipped and leaves no event,
 * so a polled page does not fill the event ring. During a forced password change nothing is
 * readable, so the first call ends the list. Does not refresh the session's idle timer: an adapter
 * does that only on user activity.
 *
 * Before the first refresh, a system or clock item the snapshot cannot yet answer carries
 * CRUX_ERR_BUSY. A list spans several calls, so its items are not one instant.
 *
 * @param session Session asking, checked on each call.
 * @param[in,out] cursor Starts at zero. Unchanged unless the result is CRUX_OK or CRUX_ERR_NO_DATA.
 * @param[out] entry Unchanged unless the result is CRUX_OK. Its own status can be a failure.
 * @retval CRUX_OK An entry was copied.
 * @retval CRUX_ERR_NO_DATA No more items.
 * @retval CRUX_ERR_INVALID_ARGUMENT A pointer is missing.
 * @retval CRUX_ERR_NOT_READY Not started, or access control could not answer.
 * @retval CRUX_ERR_INVALID_STATE The session is not valid.
 */
crux_status_t crux_mgmt_status_next(crux_access_session_t session, size_t *cursor,
                                    crux_mgmt_status_entry_t *entry);

/**
 * The word for a result in an item: "ok", or a Base result in lower case without its prefix.
 *
 * @return A name valid for the program's life, or NULL for a value that is not a crux_status_t.
 */
const char *crux_mgmt_status_result_name(crux_status_t status);

/**
 * Write one entry as the StatusItem of web/api/openapi.yaml. A u64 is decimal text, and an entry
 * whose status is a failure has no type or value.
 *
 * @param entry An entry from crux_mgmt_status_next(), which is checked again.
 * @param[out] out Receives the object and a terminator. Its content is unspecified on failure.
 * @param capacity Size of out. CRUX_MGMT_STATUS_JSON_ENTRY_SIZE is always enough.
 * @param[out] length Bytes before the terminator. Unchanged on failure.
 * @retval CRUX_OK Written.
 * @retval CRUX_ERR_INVALID_ARGUMENT A pointer is missing, or the entry could not have come from the
 * service: a bad name, result, type or text.
 * @retval CRUX_ERR_NO_RESOURCES It does not fit in capacity.
 */
crux_status_t crux_mgmt_status_entry_json(const crux_mgmt_status_entry_t *entry, char *out,
                                          size_t capacity, size_t *length);

/**
 * Write every item this session may read as the StatusReport of web/api/openapi.yaml. A session
 * that may see nothing gets {"items":[]}, and never a refusal.
 *
 * @param session Session asking.
 * @param[out] out Receives the document and a terminator. Its content is unspecified on failure.
 * @param capacity Size of out.
 * @param[out] length Bytes before the terminator. Unchanged on failure.
 * @retval CRUX_OK Written.
 * @retval CRUX_ERR_INVALID_ARGUMENT A pointer is missing.
 * @retval CRUX_ERR_NOT_READY Not started, or access control could not answer.
 * @retval CRUX_ERR_INVALID_STATE The session is not valid.
 * @retval CRUX_ERR_NO_RESOURCES It does not fit in capacity. To send more than a buffer holds, an
 * adapter uses crux_mgmt_status_next() and crux_mgmt_status_entry_json().
 */
crux_status_t crux_mgmt_status_report_json(crux_access_session_t session, char *out,
                                           size_t capacity, size_t *length);

#ifdef __cplusplus
}
#endif

#endif
