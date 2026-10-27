/** @file
 * @brief Bounded synchronous file operations on a composition-selected volume.
 *
 * Thread context only. Callers serialise use of a handle and conflicting operations
 * on the same file. Buffers are borrowed only for the duration of each call.
 */
#ifndef CRUX_FILE_H
#define CRUX_FILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque file handle; only crux_file_close() retires it. */
typedef struct crux_file crux_file_t;

typedef enum { CRUX_FILE_READ, CRUX_FILE_READ_WRITE, CRUX_FILE_CREATE_REPLACE } crux_file_mode_t;

/** File-specific results; do not compare or cast to crux_status_t.
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
    CRUX_FILE_ERR_OUT_OF_RANGE = -10
} crux_file_result_t;

/** Open a file by flat ASCII name; CREATE_REPLACE creates or truncates.
 * @param name Leaf name, not a path; see the File contract for character/length limits.
 * @param mode Required access mode.
 * @param[in,out] out Pointer to a NULL handle; remains NULL after accepted failure.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 * A failed CREATE_REPLACE may still modify the file; no rollback is promised.
 */
crux_file_result_t crux_file_open(const char *name, crux_file_mode_t mode, crux_file_t **out);

/** Read at most length bytes; a short or zero-byte EOF read returns OK.
 * @param file Valid open handle.
 * @param[out] buffer Caller-owned destination for nonzero length.
 * @param length Requested byte count.
 * @param[out] transferred Mandatory count; may report accepted bytes on error.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_read(crux_file_t *file, void *buffer, size_t length,
                                  size_t *transferred);

/** Write length bytes without implying durable storage.
 * @param file Valid writable handle.
 * @param buffer Caller-owned source for nonzero length.
 * @param length Requested byte count.
 * @param[out] transferred Mandatory count; may report accepted bytes on error.
 * @return CRUX_FILE_OK for a complete write, CRUX_FILE_PARTIAL for a short write,
 * or a CRUX_FILE_ERR_* result. No rollback is promised.
 */
crux_file_result_t crux_file_write(crux_file_t *file, const void *buffer, size_t length,
                                   size_t *transferred);

/** Set an absolute file offset; sparse extension beyond EOF is unsupported.
 * @param file Valid open handle.
 * @param offset Absolute byte offset.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_seek(crux_file_t *file, uint64_t offset);

/** Flush pending file changes through the selected provider.
 * @param file Valid open handle.
 * @return CRUX_FILE_OK acknowledges provider flush, not power-loss durability.
 */
crux_file_result_t crux_file_sync(crux_file_t *file);

/** Close a file, consuming the handle even when cleanup fails.
 * @param[in,out] file Set to NULL after an accepted close; do not retry or use aliases.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result; no implicit flush guarantee.
 */
crux_file_result_t crux_file_close(crux_file_t **file);

/** Remove a file by its flat name.
 * @param name Valid ASCII leaf name.
 * @return CRUX_FILE_OK or a CRUX_FILE_ERR_* result.
 */
crux_file_result_t crux_file_remove(const char *name);

#ifdef __cplusplus
}
#endif

#endif
