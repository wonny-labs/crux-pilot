#ifndef CRUX_FILE_PORT_H
#define CRUX_FILE_PORT_H
#include <crux/file.h>
#ifndef CRUX_FILE_POOL_SIZE
#define CRUX_FILE_POOL_SIZE 4
#endif
#ifndef CRUX_FILE_NAME_MAX
#define CRUX_FILE_NAME_MAX 63
#endif
/* Sleepable, task-context serialization. An internal lock/ownership failure is
 * fatal: never return with inconsistent pool/native-handle ownership. */
void crux_file_port_lock(void);
void crux_file_port_unlock(void);
/* Private provider contract. Counts must not exceed the requested length;
 * valid accepted counts survive errors. Native handles stay
 * in provider slots; close retires exactly once, even on error. */
crux_file_result_t crux_file_port_open(size_t slot, const char *name, crux_file_mode_t mode);
crux_file_result_t crux_file_port_truncate(size_t slot);
crux_file_result_t crux_file_port_read(size_t slot, void *buffer, size_t length, size_t *count);
crux_file_result_t crux_file_port_write(size_t slot, const void *buffer, size_t length,
                                        size_t *count);
crux_file_result_t crux_file_port_seek(size_t slot, uint64_t offset);
crux_file_result_t crux_file_port_sync(size_t slot);
crux_file_result_t crux_file_port_close(size_t slot);
crux_file_result_t crux_file_port_remove(const char *name);
/* Record secondary cleanup failure without replacing primary truncate error. */
void crux_file_port_cleanup_error(crux_file_result_t error);
#endif
