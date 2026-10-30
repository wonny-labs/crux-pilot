#include "sync_internal.h"
#include <limits.h>
#include <stddef.h>

typedef struct {
    uint32_t generation;
    unsigned int users;
    bool used;
} lifetime_t;

typedef struct {
    sync_native_identity_t owner;
    sync_native_mutex_t native;
    lifetime_t life;
    bool held;
} mutex_slot_t;

typedef struct {
    lifetime_t life;
    sync_native_semaphore_t native;
} semaphore_slot_t;

static mutex_slot_t mutexes[CRUX_SYNC_MUTEX_COUNT];
static semaphore_slot_t semaphores[CRUX_SYNC_SEMAPHORE_COUNT];

static bool matches(const lifetime_t *life, uint32_t generation)
{
    return life->used && life->generation == generation;
}

static mutex_slot_t *find_mutex(crux_mutex_t handle)
{
    if (handle.slot == 0 || handle.slot > CRUX_SYNC_MUTEX_COUNT) {
        return NULL;
    }
    mutex_slot_t *slot = &mutexes[handle.slot - 1];
    return matches(&slot->life, handle.generation) ? slot : NULL;
}

static semaphore_slot_t *find_semaphore(crux_semaphore_t handle)
{
    if (handle.slot == 0 || handle.slot > CRUX_SYNC_SEMAPHORE_COUNT) {
        return NULL;
    }
    semaphore_slot_t *slot = &semaphores[handle.slot - 1];
    return matches(&slot->life, handle.generation) ? slot : NULL;
}

crux_status_t crux_mutex_create(crux_mutex_t *out)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }

    sync_native_metadata_lock();
    crux_status_t result = CRUX_ERR_NO_RESOURCES;
    for (uint32_t i = 0; i < CRUX_SYNC_MUTEX_COUNT; ++i) {
        mutex_slot_t *slot = &mutexes[i];
        if (slot->life.used || slot->life.generation == UINT32_MAX) {
            continue;
        }
        ++slot->life.generation;
        result = crux_mutex_impl_create(&slot->native);
        if (result == CRUX_OK) {
            slot->life.used = true;
            slot->life.users = 0;
            slot->held = false;
            *out = (crux_mutex_t){.slot = i + 1, .generation = slot->life.generation};
        }
        break;
    }
    sync_native_metadata_unlock();
    return result;
}

crux_status_t crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }

    sync_native_deadline_t deadline;
    crux_status_t result = crux_sync_make_deadline(timeout_ms, &deadline);
    if (result != CRUX_OK) {
        return result;
    }

    result = sync_native_metadata_lock_until(&deadline);
    if (result != CRUX_OK) {
        return result;
    }

    mutex_slot_t *slot = find_mutex(mutex);
    if (slot == NULL) {
        sync_native_metadata_unlock();
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (slot->held && crux_sync_identity_equal(slot->owner, crux_sync_current_identity())) {
        sync_native_metadata_unlock();
        return CRUX_ERR_INVALID_STATE;
    }
    if (slot->life.users == UINT_MAX) {
        sync_native_metadata_unlock();
        return CRUX_ERR_NO_RESOURCES;
    }

    ++slot->life.users;
    if (deadline.poll) {
        result = crux_mutex_impl_lock(&slot->native, &deadline);
        if (result == CRUX_OK) {
            slot->held = true;
            slot->owner = crux_sync_current_identity();
        }
        --slot->life.users;
        sync_native_metadata_unlock();
        return result;
    }
    /* The user count pins this slot while the finite native wait runs unlocked. */
    sync_native_metadata_unlock();
    result = crux_mutex_impl_lock(&slot->native, &deadline);
    sync_native_metadata_lock();
    if (result == CRUX_OK) {
        slot->held = true;
        slot->owner = crux_sync_current_identity();
    }
    --slot->life.users;
    sync_native_metadata_unlock();
    return result;
}

crux_status_t crux_mutex_unlock(crux_mutex_t mutex)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    sync_native_metadata_lock();
    mutex_slot_t *slot = find_mutex(mutex);
    if (slot == NULL) {
        sync_native_metadata_unlock();
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!slot->held || !crux_sync_identity_equal(slot->owner, crux_sync_current_identity())) {
        sync_native_metadata_unlock();
        return CRUX_ERR_INVALID_STATE;
    }
    crux_status_t result = crux_mutex_impl_unlock(&slot->native);
    if (result == CRUX_OK) {
        slot->held = false;
    }
    sync_native_metadata_unlock();
    return result;
}

crux_status_t crux_mutex_destroy(crux_mutex_t *mutex)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (mutex == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    sync_native_metadata_lock();
    mutex_slot_t *slot = find_mutex(*mutex);
    if (slot == NULL) {
        sync_native_metadata_unlock();
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (slot->held || slot->life.users != 0) {
        sync_native_metadata_unlock();
        return CRUX_ERR_BUSY;
    }
    crux_status_t result = crux_mutex_impl_destroy(&slot->native);
    if (result == CRUX_OK) {
        slot->life.used = false;
        *mutex = (crux_mutex_t){0};
    }
    sync_native_metadata_unlock();
    return result;
}

crux_status_t crux_semaphore_create(uint32_t initial, uint32_t maximum, crux_semaphore_t *out)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (maximum == 0 || maximum > INT32_MAX || initial > maximum) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    sync_native_metadata_lock();
    crux_status_t result = CRUX_ERR_NO_RESOURCES;
    for (uint32_t i = 0; i < CRUX_SYNC_SEMAPHORE_COUNT; ++i) {
        semaphore_slot_t *slot = &semaphores[i];
        if (slot->life.used || slot->life.generation == UINT32_MAX) {
            continue;
        }
        ++slot->life.generation;
        result = crux_semaphore_impl_create(&slot->native, initial, maximum);
        if (result == CRUX_OK) {
            slot->life.used = true;
            slot->life.users = 0;
            *out = (crux_semaphore_t){.slot = i + 1, .generation = slot->life.generation};
        }
        break;
    }
    sync_native_metadata_unlock();
    return result;
}

crux_status_t crux_semaphore_take(crux_semaphore_t semaphore, uint32_t timeout_ms)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    sync_native_deadline_t deadline;
    crux_status_t result = crux_sync_make_deadline(timeout_ms, &deadline);
    if (result != CRUX_OK) {
        return result;
    }
    result = sync_native_metadata_lock_until(&deadline);
    if (result != CRUX_OK) {
        return result;
    }
    semaphore_slot_t *slot = find_semaphore(semaphore);
    if (slot == NULL) {
        sync_native_metadata_unlock();
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (slot->life.users == UINT_MAX) {
        sync_native_metadata_unlock();
        return CRUX_ERR_NO_RESOURCES;
    }
    ++slot->life.users;
    if (deadline.poll) {
        result = crux_semaphore_impl_take(&slot->native, &deadline);
        --slot->life.users;
        sync_native_metadata_unlock();
        return result;
    }
    /* The user count pins this slot while the finite native wait runs unlocked. */
    sync_native_metadata_unlock();
    result = crux_semaphore_impl_take(&slot->native, &deadline);
    sync_native_metadata_lock();
    --slot->life.users;
    sync_native_metadata_unlock();
    return result;
}

crux_status_t crux_semaphore_give(crux_semaphore_t semaphore)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    sync_native_metadata_lock();
    semaphore_slot_t *slot = find_semaphore(semaphore);
    if (slot == NULL) {
        sync_native_metadata_unlock();
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    crux_status_t result = crux_semaphore_impl_give(&slot->native);
    sync_native_metadata_unlock();
    return result;
}

crux_status_t crux_semaphore_destroy(crux_semaphore_t *semaphore)
{
    if (!crux_sync_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (semaphore == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    sync_native_metadata_lock();
    semaphore_slot_t *slot = find_semaphore(*semaphore);
    if (slot == NULL) {
        sync_native_metadata_unlock();
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (slot->life.users != 0) {
        sync_native_metadata_unlock();
        return CRUX_ERR_BUSY;
    }
    crux_status_t result = crux_semaphore_impl_destroy(&slot->native);
    if (result == CRUX_OK) {
        slot->life.used = false;
        *semaphore = (crux_semaphore_t){0};
    }
    sync_native_metadata_unlock();
    return result;
}
