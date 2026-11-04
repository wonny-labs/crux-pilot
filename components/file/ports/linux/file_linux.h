#ifndef CRUX_FILE_LINUX_H
#define CRUX_FILE_LINUX_H
#include <crux/file.h>
/* Trusted composition only: existing directory, immutable process-lifetime root.
 * Bind once before concurrent use; provider never creates/mounts/formats it.
 * The retained directory descriptor is released by process termination. */
crux_file_result_t crux_file_linux_bind(const char *root);
/* The same for one root index. A path that is equal to, inside or a parent of another bound root's
 * path returns CRUX_FILE_ERR_INVALID_STATE, comparing path text only. crux_file_linux_bind is root
 * 0. */
crux_file_result_t crux_file_linux_bind_root(crux_file_root_t root, const char *path);
crux_file_result_t crux_file_linux_error(int error);
crux_file_result_t crux_file_linux_last_cleanup_error(void);
#endif
