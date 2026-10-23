#ifndef CRUX_FILE_H
#define CRUX_FILE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct crux_file crux_file_t;
typedef enum { CRUX_FILE_READ, CRUX_FILE_READ_WRITE, CRUX_FILE_CREATE_REPLACE } crux_file_mode_t;
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
/* See docs/file-contract.md for bounds, ownership and synchronization rules.
 * This is a separate result domain from crux_status_t; compare named values.
 * Names: flat ASCII [A-Za-z0-9_.-], default 1..63 bytes, excluding dot and dot-dot.
 * Caller serializes handle and conflicting same-file operations. Task context only.
 * Buffers are borrowed for the call. transferred is mandatory, including zero I/O.
 * Short reads (EOF) are OK; short writes are PARTIAL, including zero progress.
 * Write OK does not acknowledge persistence; sync acknowledges provider flush only.
 * close consumes and clears *file even on error; never retry/use aliases afterwards.
 * Failed create/replace may change a file; no rollback or atomicity is promised.
 */
crux_file_result_t crux_file_open(const char *name, crux_file_mode_t mode, crux_file_t **out);
crux_file_result_t crux_file_read(crux_file_t *file, void *buffer, size_t length,
                                  size_t *transferred);
crux_file_result_t crux_file_write(crux_file_t *file, const void *buffer, size_t length,
                                   size_t *transferred);
crux_file_result_t crux_file_seek(crux_file_t *file, uint64_t offset);
crux_file_result_t crux_file_sync(crux_file_t *file);
crux_file_result_t crux_file_close(crux_file_t **file);
crux_file_result_t crux_file_remove(const char *name);
#ifdef __cplusplus
}
#endif
#endif
