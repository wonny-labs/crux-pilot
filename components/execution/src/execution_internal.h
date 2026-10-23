#ifndef CRUX_EXECUTION_INTERNAL_H
#define CRUX_EXECUTION_INTERNAL_H

#include "execution_native_types.h"
#include <crux/execution.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef CRUX_EXECUTION_THREAD_COUNT
#define CRUX_EXECUTION_THREAD_COUNT 4
#endif
#ifndef CRUX_EXECUTION_STACK_BYTES
#define CRUX_EXECUTION_STACK_BYTES 4096
#endif
#ifndef CRUX_EXECUTION_MUTEX_COUNT
#define CRUX_EXECUTION_MUTEX_COUNT 8
#endif
#ifndef CRUX_EXECUTION_SEMAPHORE_COUNT
#define CRUX_EXECUTION_SEMAPHORE_COUNT 8
#endif

bool exec_native_context_allowed(void);
void exec_native_metadata_lock(void);
crux_status_t exec_native_metadata_lock_until(const exec_native_deadline_t *deadline);
void exec_native_metadata_unlock(void);
crux_status_t exec_native_deadline(uint32_t timeout_ms, exec_native_deadline_t *out);
crux_status_t exec_native_thread_create(exec_native_thread_t *thread, crux_thread_entry_t entry,
                                        void *argument, uint32_t slot,
                                        const crux_thread_options_t *options);
bool exec_native_thread_is_current(const exec_native_thread_t *thread);
crux_status_t exec_native_thread_join(exec_native_thread_t *thread,
                                      const exec_native_deadline_t *deadline);
exec_native_identity_t exec_native_current(void);
bool exec_native_identity_equal(exec_native_identity_t first, exec_native_identity_t second);
crux_status_t exec_native_mutex_create(exec_native_mutex_t *mutex);
crux_status_t exec_native_mutex_lock(exec_native_mutex_t *mutex,
                                     const exec_native_deadline_t *deadline);
crux_status_t exec_native_mutex_unlock(exec_native_mutex_t *mutex);
crux_status_t exec_native_mutex_destroy(exec_native_mutex_t *mutex);
crux_status_t exec_native_semaphore_create(exec_native_semaphore_t *semaphore, uint32_t initial,
                                           uint32_t maximum);
crux_status_t exec_native_semaphore_take(exec_native_semaphore_t *semaphore,
                                         const exec_native_deadline_t *deadline);
crux_status_t exec_native_semaphore_give(exec_native_semaphore_t *semaphore);
crux_status_t exec_native_semaphore_destroy(exec_native_semaphore_t *semaphore);

#endif
