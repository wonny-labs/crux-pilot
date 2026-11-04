/**
 * @file
 * @brief Declared file areas, with permissions, limits and bounded transfers, for management
 * channels.
 *
 * Thread context only. crux_file_service_start() must return before any other operation starts.
 * Every operation except crux_file_service_resolve() can wait on storage, so a server runs them on
 * a worker or session thread, and no operation promises a time bound. Buffers are borrowed for the
 * call, and the configuration stays borrowed for the program's life.
 *
 * Callers serialise use of one listing or transfer handle. Replacement is never atomic across power
 * loss, in either write mode. See docs/file-service-contract.md.
 */
#ifndef CRUX_FILE_SERVICE_H
#define CRUX_FILE_SERVICE_H

#include <crux/access.h>
#include <crux/file.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A write mode that an area offers. */
enum { CRUX_FILE_SERVICE_MANAGED = 1u << 0, CRUX_FILE_SERVICE_DIRECT = 1u << 1 };

/** One directory that clients may reach. Strings are borrowed for the program's life. */
typedef struct {
    const char *name;             /**< Client-visible, 1 to 15 characters of [a-z0-9_-]. */
    crux_file_root_t root;        /**< A root the composition bound. */
    const char *read_permission;  /**< Required. */
    const char *write_permission; /**< Required for a writable area, otherwise NULL. */
    uint64_t max_file_bytes;      /**< At most INT32_MAX, the provider's seek limit. */
    uint64_t max_area_bytes;      /**< Sum of file sizes, partial files included. */
    uint16_t max_entries;
    const char *partial_suffix; /**< Files the stale rule may remove. NULL means none are. */
    uint8_t write_modes; /**< CRUX_FILE_SERVICE_MANAGED, CRUX_FILE_SERVICE_DIRECT, or both. */
} crux_file_area_t;

typedef struct {
    const crux_file_area_t *areas;
    size_t area_count;
    const crux_file_root_t
        *reserved_roots; /**< Roots no area may use, from the credential store. */
    size_t reserved_root_count;
    bool no_credential_store; /**< Required to allow an empty reserved list. */
} crux_file_service_config_t;

/** File service results; do not compare or cast to another domain's results. */
typedef enum {
    CRUX_FILE_SERVICE_OK = 0,
    CRUX_FILE_SERVICE_END = 1,                   /**< A listing of areas finished. */
    CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT = -1, /**< Includes a name or path the rules refuse. */
    CRUX_FILE_SERVICE_ERR_NOT_READY = -2,
    CRUX_FILE_SERVICE_ERR_SESSION_INVALID = -3,
    CRUX_FILE_SERVICE_ERR_DENIED = -4,
    CRUX_FILE_SERVICE_ERR_NOT_FOUND = -5, /**< Missing, or an area the session may not see. */
    CRUX_FILE_SERVICE_ERR_EXISTS = -6,
    CRUX_FILE_SERVICE_ERR_BUSY = -7,     /**< A conflicting open or reservation. */
    CRUX_FILE_SERVICE_ERR_NO_SPACE = -8, /**< The medium is full. */
    CRUX_FILE_SERVICE_ERR_LIMIT = -9,    /**< A declared limit would be exceeded. */
    CRUX_FILE_SERVICE_ERR_NO_RESOURCES = -10,
    CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE = -11, /**< A sparse write, or an offset past the end. */
    CRUX_FILE_SERVICE_ERR_IO = -12
} crux_file_service_result_t;

/** How a transfer opens its file. */
typedef enum {
    CRUX_FILE_SERVICE_OPEN_READ = 1,
    CRUX_FILE_SERVICE_OPEN_MANAGED = 2,       /**< Write a temporary file, published by commit. */
    CRUX_FILE_SERVICE_OPEN_DIRECT = 3,        /**< The named file must exist. */
    CRUX_FILE_SERVICE_OPEN_DIRECT_CREATE = 4, /**< Open, or create if missing. */
    CRUX_FILE_SERVICE_OPEN_DIRECT_TRUNCATE = 5,
    CRUX_FILE_SERVICE_OPEN_DIRECT_EXCLUSIVE = 6
} crux_file_service_open_t;

/** Opaque listing handle from a bounded pool; only crux_file_service_dir_close() retires it. */
typedef struct crux_file_service_dir crux_file_service_dir_t;
/** Opaque transfer handle from a bounded pool; commit and close retire it. */
typedef struct crux_file_service_transfer crux_file_service_transfer_t;

/**
 * Validate the table, finish or clear what a cut left behind, and count each area.
 *
 * Refuses a duplicate name or root, a reserved root, an empty reserved list unless
 * no_credential_store is set, zero limits, an oversized max_file_bytes, a missing permission, and a
 * writable area with no mode. Until it succeeds every operation returns ERR_NOT_READY. An area
 * whose root is not ready returns ERR_NOT_READY until a reset, an area whose scan failed returns
 * ERR_IO until a reset, and neither stops the others.
 *
 * @param config Borrowed for the program's life.
 * @retval CRUX_FILE_SERVICE_OK Started.
 * @retval CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT The table is refused; nothing changed.
 * @retval CRUX_FILE_SERVICE_ERR_EXISTS Already started; nothing changed.
 * @retval CRUX_FILE_SERVICE_ERR_NO_RESOURCES The service lock could not be created.
 */
crux_file_service_result_t crux_file_service_start(const crux_file_service_config_t *config);

/**
 * Split a client path into an area and a name, touching no storage. This is the only path parser.
 *
 * Accepts /area, /area/ and /area/name, up to 80 bytes, and nothing else. It does not check a
 * session, so its NOT_FOUND must reach a client only through the same mapping as a hidden area.
 *
 * @param[out] area The declared area.
 * @param[out] name Empty for a path to the area itself.
 * @param capacity Size of name. 64 is always enough.
 * @retval CRUX_FILE_SERVICE_OK Resolved.
 * @retval CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT The path, a name or an argument is refused.
 * @retval CRUX_FILE_SERVICE_ERR_NOT_FOUND No such area is declared.
 * @retval CRUX_FILE_SERVICE_ERR_NOT_READY Not started.
 */
crux_file_service_result_t crux_file_service_resolve(const char *path,
                                                     const crux_file_area_t **area, char *name,
                                                     size_t capacity);

/**
 * Walk the areas this session may see, with no denial event for a hidden one.
 *
 * @param[in,out] cursor Starts at zero.
 * @param[out] area The next visible area.
 * @param[out] writable Whether the session may write there.
 * @retval CRUX_FILE_SERVICE_OK An area was returned.
 * @retval CRUX_FILE_SERVICE_END No more areas.
 */
crux_file_service_result_t crux_file_service_area_next(crux_access_session_t session,
                                                       size_t *cursor,
                                                       const crux_file_area_t **area,
                                                       bool *writable);

/** Start listing an area. NOT_FOUND if the session has neither of its permissions. */
crux_file_service_result_t crux_file_service_dir_open(crux_access_session_t session,
                                                      const char *area,
                                                      crux_file_service_dir_t **out);

/**
 * Read the next entry, hiding the service's own temporary files.
 *
 * @param[out] name Empty at the end and after an error.
 * @param capacity Below 64 returns ERR_OUT_OF_RANGE and uses up no entry.
 * @param[out] end True when the listing finished and name holds no entry.
 */
crux_file_service_result_t crux_file_service_dir_read(crux_access_session_t session,
                                                      crux_file_service_dir_t *dir, char *name,
                                                      size_t capacity, crux_file_info_t *info,
                                                      bool *end);

/** Close a listing, needing no session, so that a handle always comes back. */
crux_file_service_result_t crux_file_service_dir_close(crux_file_service_dir_t **dir);

/** Read a regular file's size. Needs the area's read permission. */
crux_file_service_result_t crux_file_service_stat(crux_access_session_t session, const char *area,
                                                  const char *name, crux_file_info_t *out);

/** Remove a file. Needs the area's write permission. ERR_BUSY if it is open. */
crux_file_service_result_t crux_file_service_remove(crux_access_session_t session, const char *area,
                                                    const char *name);

/**
 * Rename inside one area, refusing an existing destination with ERR_EXISTS. Needs the write
 * permission. It is not atomic across power loss.
 */
crux_file_service_result_t crux_file_service_rename(crux_access_session_t session, const char *area,
                                                    const char *from, const char *to);

/**
 * Open a transfer. The transfer belongs to this session.
 *
 * A write mode the area does not offer gives ERR_DENIED. A managed upload reserves its target until
 * commit or close, and any write mode refuses a second writer of that name with ERR_BUSY.
 *
 * @param declared_size Expected final size, so that a managed upload fails before any data. Zero if
 * unknown. A direct mode ignores it, because such a write may replace what is there.
 * @param[in,out] out Pointer to a NULL handle; remains NULL after failure.
 * @retval CRUX_FILE_SERVICE_ERR_LIMIT A declared limit would be exceeded, after the stale partial
 * rule.
 * @retval CRUX_FILE_SERVICE_ERR_NO_RESOURCES The transfer pool, name table or temporary names are
 * full.
 */
crux_file_service_result_t crux_file_service_open(crux_access_session_t session, const char *area,
                                                  const char *name, crux_file_service_open_t mode,
                                                  uint64_t declared_size,
                                                  crux_file_service_transfer_t **out);

/**
 * Read at an offset. A short or zero-byte read at the end returns OK.
 * @retval CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE The offset is past the end.
 * @retval CRUX_FILE_SERVICE_ERR_DENIED Another session's transfer, or a managed upload.
 */
crux_file_service_result_t crux_file_service_read(crux_access_session_t session,
                                                  crux_file_service_transfer_t *transfer,
                                                  uint64_t offset, void *buffer, size_t length,
                                                  size_t *transferred);

/**
 * Write at an offset, at or below the current size. A managed upload must write at its current
 * position. A short write returns OK with a short count, and the caller continues or fails it.
 * @retval CRUX_FILE_SERVICE_ERR_OUT_OF_RANGE A sparse write, or an offset that would overflow.
 * @retval CRUX_FILE_SERVICE_ERR_LIMIT A declared limit would be exceeded; nothing was written.
 * @retval CRUX_FILE_SERVICE_ERR_DENIED Another session's transfer, or one opened to read.
 */
crux_file_service_result_t crux_file_service_write(crux_access_session_t session,
                                                   crux_file_service_transfer_t *transfer,
                                                   uint64_t offset, const void *buffer,
                                                   size_t length, size_t *transferred);

/** Read a transfer's current size. */
crux_file_service_result_t crux_file_service_size(crux_access_session_t session,
                                                  crux_file_service_transfer_t *transfer,
                                                  uint64_t *size);

/**
 * Publish a managed upload under its target name, consuming the handle.
 *
 * It removes an existing target and then renames, which is not atomic across power loss. A refusal
 * of the session or the permission, ERR_BUSY and ERR_NO_RESOURCES change nothing and keep the
 * handle, so that the caller can write on, retry or close. If the temporary file cannot be opened
 * again after such a refusal, the result is ERR_IO and the upload is gone. Any other result
 * consumes the handle.
 *
 * @retval CRUX_FILE_SERVICE_ERR_DENIED Another session's transfer, or not a managed upload.
 * @retval CRUX_FILE_SERVICE_ERR_IO No success. The target may be gone, and the next start finishes
 * it.
 */
crux_file_service_result_t crux_file_service_commit(crux_access_session_t session,
                                                    crux_file_service_transfer_t **transfer);

/**
 * Close a transfer, needing no session, so that a handle always comes back. A managed upload that
 * was not committed is removed, and a direct write leaves its file where it is. The handle is
 * consumed even when this returns an error.
 */
crux_file_service_result_t crux_file_service_close(crux_file_service_transfer_t **transfer);

#ifdef __cplusplus
}
#endif

#endif
