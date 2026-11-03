/**
 * @file
 * @brief Bounded synchronous file operations on a composition-selected volume.
 *
 * Thread context only. Callers serialise use of a handle and conflicting operations
 * on the same file. Buffers are borrowed only for the duration of each call.
 */
#ifndef CRUX_FILE_H
#define CRUX_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque file handle; only crux_file_close() retires it. */
typedef struct crux_file crux_file_t;

/** Opaque directory listing handle; only crux_file_dir_close() retires it. */
typedef struct crux_file_dir crux_file_dir_t;

/** Index of a directory that the composition bound. Root 0 is the one crux_file_open() uses. */
typedef uint8_t crux_file_root_t;
enum { CRUX_FILE_ROOT_DEFAULT = 0 };

typedef struct {
    uint64_t size; /**< Bytes. */
} crux_file_info_t;

typedef enum { CRUX_FILE_READ, CRUX_FILE_READ_WRITE, CRUX_FILE_CREATE_REPLACE } crux_file_mode_t;

/**
 * File-specific results; do not compare or cast to crux_status_t.
 * Positive CRUX_FILE_PARTIAL is an incomplete write, not a negative error.
 */
typedef enum {
    CRUX_FILE_OK = 0,
    CRUX_FILE_PARTIAL = 1,
    CRUX_FILE_ERR_INVALID_ARGUMENT = -1,
    CRUX_FILE_ERR_INVALID_STATE = -2,
    CRUX_FILE_ERR_NOT_READY = -3,
    CRUX_FILE_ERR_NOT_FOUND = -4,
    CRUX_FILE_ERR_NO_SPACE = -5,
    CRUX_FILE_ERR_NO_RESOURCES = -6,
    CRUX_FILE_ERR_ACCESS = -7,
    CRUX_FILE_ERR_IO = -8,
    CRUX_FILE_ERR_NOT_SUPPORTED = -9,
    CRUX_FILE_ERR_OUT_OF_RANGE = -10,
    CRUX_FILE_ERR_EXISTS = -11
} crux_file_result_t;

/**
 * Open a file by flat ASCII name; CREATE_REPLACE creates or truncates.
 * @param name Leaf name, not a path; see the File contract for character/length limits.
 * @param mode Required access mode.
 * @param[in,out] out Pointer to a NULL handle; remains NULL after accepted failure.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 * A failed CREATE_REPLACE may still modify the file; no rollback is promised.
 */
crux_file_result_t crux_file_open(const char *name, crux_file_mode_t mode, crux_file_t **out);

/**
 * Read at most length bytes; a short or zero-byte EOF read returns OK.
 * @param file Valid open handle.
 * @param[out] buffer Caller-owned destination for nonzero length.
 * @param length Requested byte count.
 * @param[out] transferred Mandatory count; may report accepted bytes on error.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_read(crux_file_t *file, void *buffer, size_t length,
                                  size_t *transferred);

/**
 * Write length bytes without implying durable storage.
 * @param file Valid writable handle.
 * @param buffer Caller-owned source for nonzero length.
 * @param length Requested byte count.
 * @param[out] transferred Mandatory count; may report accepted bytes on error.
 * @return CRUX_FILE_OK for a complete write, CRUX_FILE_PARTIAL for a short write,
 * or a CRUX_FILE_ERR_* result. No rollback is promised.
 */
crux_file_result_t crux_file_write(crux_file_t *file, const void *buffer, size_t length,
                                   size_t *transferred);

/**
 * Set an absolute file offset; sparse extension beyond EOF is unsupported.
 * @param file Valid open handle.
 * @param offset Absolute byte offset.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_seek(crux_file_t *file, uint64_t offset);

/**
 * Flush pending file changes through the selected provider.
 * @param file Valid open handle.
 * @return CRUX_FILE_OK acknowledges provider flush, not power-loss durability.
 */
crux_file_result_t crux_file_sync(crux_file_t *file);

/**
 * Close a file, consuming the handle even when cleanup fails.
 * @param[in,out] file Set to NULL after an accepted close; do not retry or use aliases.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result; no implicit flush guarantee.
 */
crux_file_result_t crux_file_close(crux_file_t **file);

/**
 * Remove a file by its flat name.
 * @param name Valid ASCII leaf name.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_remove(const char *name);

/**
 * crux_file_open() in a chosen root. The conflict rule compares the root and the name together.
 * @param root Bound root; any other index returns CRUX_FILE_ERR_NOT_READY.
 * @param name Valid ASCII leaf name.
 * @param mode Required access mode.
 * @param[in,out] out Pointer to a NULL handle; remains NULL after accepted failure.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_open_at(crux_file_root_t root, const char *name, crux_file_mode_t mode,
                                     crux_file_t **out);

/**
 * crux_file_remove() in a chosen root.
 * @param root Bound root.
 * @param name Valid ASCII leaf name.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_remove_at(crux_file_root_t root, const char *name);

/**
 * Read what the provider last recorded about a regular file. A file with an open writer may report
 * an older size, and a directory is CRUX_FILE_ERR_NOT_FOUND.
 * @param root Bound root.
 * @param name Valid ASCII leaf name.
 * @param[out] out Mandatory result.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_stat(crux_file_root_t root, const char *name, crux_file_info_t *out);

/**
 * Read a handle's current size, including unsynced writes, without moving its position.
 * @param file Valid open handle.
 * @param[out] size Mandatory result.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result. It implies no durability.
 */
crux_file_result_t crux_file_size(crux_file_t *file, uint64_t *size);

/**
 * Rename a regular file inside one root, refusing an existing destination. No other File call sees
 * a partial state, but this is not atomic across power loss. The checks run in this order:
 * arguments, CRUX_FILE_ERR_INVALID_STATE for a live handle on either name, CRUX_FILE_ERR_NOT_FOUND
 * for a missing source, and CRUX_FILE_ERR_EXISTS for an existing destination of any type, including
 * from equal to to.
 * @param root Bound root.
 * @param from Valid ASCII leaf name.
 * @param to Valid ASCII leaf name.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_rename(crux_file_root_t root, const char *from, const char *to);

/**
 * Start listing a root's regular files whose names pass the name rule.
 * @param root Bound root.
 * @param[in,out] out Pointer to a NULL handle; remains NULL after accepted failure.
 * @return CRUX_FILE_OK, CRUX_FILE_ERR_NO_RESOURCES for a full pool, or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_dir_open(crux_file_root_t root, crux_file_dir_t **out);

/**
 * Read the next entry in provider order. An entry created or removed during a listing may be missed
 * or repeated, so this is not a snapshot.
 * @param dir Valid listing handle.
 * @param[out] name Mandatory destination; empty at the end and after an error.
 * @param capacity Name bytes; below 64 returns CRUX_FILE_ERR_OUT_OF_RANGE and uses up no entry.
 * @param[out] info Mandatory entry details.
 * @param[out] end Mandatory; true when the listing finished and name holds no entry.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_dir_read(crux_file_dir_t *dir, char *name, size_t capacity,
                                      crux_file_info_t *info, bool *end);

/**
 * Close a listing, consuming the handle even when cleanup fails.
 * @param[in,out] dir Set to NULL after an accepted close; do not retry or use aliases.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_dir_close(crux_file_dir_t **dir);

#ifdef __cplusplus
}
#endif

#endif
