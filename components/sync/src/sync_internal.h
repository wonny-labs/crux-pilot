#ifndef CRUX_SYNC_INTERNAL_H
#define CRUX_SYNC_INTERNAL_H

#include "sync_native_types.h"
#include <crux/sync.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef CRUX_SYNC_MUTEX_COUNT
#define CRUX_SYNC_MUTEX_COUNT 8
#endif
#ifndef CRUX_SYNC_SEMAPHORE_COUNT
#define CRUX_SYNC_SEMAPHORE_COUNT 8
#endif

bool sync_native_context_allowed(void);
void sync_native_metadata_lock(void);
crux_status_t sync_native_metadata_lock_until(const sync_native_deadline_t *deadline);
void sync_native_metadata_unlock(void);
crux_status_t sync_native_deadline(uint32_t timeout_ms, sync_native_deadline_t *out);
sync_native_identity_t sync_native_current(void);
bool sync_native_identity_equal(sync_native_identity_t first, sync_native_identity_t second);
crux_status_t sync_native_mutex_create(sync_native_mutex_t *mutex);
crux_status_t sync_native_mutex_lock(sync_native_mutex_t *mutex,
                                     const sync_native_deadline_t *deadline);
crux_status_t sync_native_mutex_unlock(sync_native_mutex_t *mutex);
crux_status_t sync_native_mutex_destroy(sync_native_mutex_t *mutex);
crux_status_t sync_native_semaphore_create(sync_native_semaphore_t *semaphore, uint32_t initial,
                                           uint32_t maximum);
crux_status_t sync_native_semaphore_take(sync_native_semaphore_t *semaphore,
                                         const sync_native_deadline_t *deadline);
crux_status_t sync_native_semaphore_give(sync_native_semaphore_t *semaphore);
crux_status_t sync_native_semaphore_destroy(sync_native_semaphore_t *semaphore);

#endif
