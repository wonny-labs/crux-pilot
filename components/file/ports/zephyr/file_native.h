#ifndef CRUX_FILE_NATIVE_H
#define CRUX_FILE_NATIVE_H
#include <crux/file.h>
/* Composition binds once, before concurrent calls, to an already mounted root.
 * Never mounts, formats or selects media; no rebind/unmount with live calls. */
crux_file_result_t crux_file_zephyr_bind(const char *root);
/* The same for one root index. A path that is equal to, inside or a parent of another bound root's
 * path returns CRUX_FILE_ERR_INVALID_STATE, comparing path text only. crux_file_zephyr_bind is root
 * 0. */
crux_file_result_t crux_file_zephyr_bind_root(crux_file_root_t root, const char *path);
crux_file_result_t crux_file_zephyr_last_cleanup_error(void);
#endif
