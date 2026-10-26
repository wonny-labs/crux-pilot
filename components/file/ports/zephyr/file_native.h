#ifndef CRUX_FILE_NATIVE_H
#define CRUX_FILE_NATIVE_H
#include <crux/file.h>
/* Composition binds once, before concurrent calls, to an already mounted root.
 * Never mounts, formats or selects media; no rebind/unmount with live calls. */
crux_file_result_t crux_file_zephyr_bind(const char *root);
crux_file_result_t crux_file_zephyr_last_cleanup_error(void);
#endif
