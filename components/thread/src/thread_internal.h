#ifndef CRUX_THREAD_INTERNAL_H
#define CRUX_THREAD_INTERNAL_H

#include "thread_native_types.h"
#include "thread_sleep_port.h"
#include <crux/thread.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef CRUX_THREAD_COUNT
#define CRUX_THREAD_COUNT 4
#endif
#ifndef CRUX_THREAD_STACK_BYTES
#define CRUX_THREAD_STACK_BYTES 4096
#endif

void thread_native_metadata_lock(void);
crux_status_t thread_native_metadata_lock_until(const thread_native_deadline_t *deadline);
void thread_native_metadata_unlock(void);
crux_status_t thread_native_deadline(uint32_t timeout_ms, thread_native_deadline_t *out);
crux_status_t thread_native_thread_create(thread_native_thread_t *thread, crux_thread_entry_t entry,
                                          void *argument, uint32_t slot,
                                          const crux_thread_options_t *options);
bool thread_native_thread_is_current(const thread_native_thread_t *thread);
crux_status_t thread_native_thread_join(thread_native_thread_t *thread,
                                        const thread_native_deadline_t *deadline);

#endif
