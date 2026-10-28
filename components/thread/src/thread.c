#include "interrupt.h"
#include "thread_internal.h"

#include <limits.h>
#include <stddef.h>

typedef struct {
    thread_native_thread_t native;
    uint32_t generation;
    bool used;
    bool joining;
} thread_slot_t;

static thread_slot_t threads[CRUX_THREAD_COUNT];

static thread_slot_t *find_thread(crux_thread_t handle)
{
    if (handle.slot == 0 || handle.slot > CRUX_THREAD_COUNT) {
        return NULL;
    }
    thread_slot_t *slot = &threads[handle.slot - 1];
    return slot->used && slot->generation == handle.generation ? slot : NULL;
}

crux_status_t crux_thread_create(const crux_thread_options_t *options, crux_thread_entry_t entry,
                                 void *argument, crux_thread_t *out)
{
    if (out != NULL) {
        *out = (crux_thread_t){0};
    }
    if (crux_is_in_isr()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (options == NULL || entry == NULL ||
        (options->priority != CRUX_THREAD_PRIORITY_LOW &&
         options->priority != CRUX_THREAD_PRIORITY_NORMAL &&
         options->priority != CRUX_THREAD_PRIORITY_HIGH)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    thread_native_metadata_lock();
    crux_status_t result = CRUX_ERR_NO_RESOURCES;
    for (uint32_t i = 0; i < CRUX_THREAD_COUNT; ++i) {
        thread_slot_t *slot = &threads[i];
        if (slot->used || slot->generation == UINT32_MAX) {
            continue;
        }
        ++slot->generation;
        slot->used = true;
        slot->joining = false;
        result = crux_thread_impl_create(&slot->native, entry, argument, i, options);
        if (result == CRUX_OK) {
            *out = (crux_thread_t){.slot = i + 1, .generation = slot->generation};
        } else {
            slot->used = false;
        }
        break;
    }
    thread_native_metadata_unlock();
    return result;
}

crux_status_t crux_thread_join(crux_thread_t *thread, uint32_t timeout_ms)
{
    if (crux_is_in_isr()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (thread == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    thread_native_deadline_t deadline;
    crux_status_t result = crux_thread_make_deadline(timeout_ms, &deadline);
    if (result != CRUX_OK) {
        return result;
    }
    result = thread_native_metadata_lock_until(&deadline);
    if (result != CRUX_OK) {
        return result;
    }
    thread_slot_t *slot = find_thread(*thread);
    if (slot == NULL) {
        thread_native_metadata_unlock();
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (crux_thread_is_current(&slot->native)) {
        thread_native_metadata_unlock();
        return CRUX_ERR_INVALID_STATE;
    }
    if (slot->joining) {
        thread_native_metadata_unlock();
        return CRUX_ERR_BUSY;
    }
    slot->joining = true;
    if (deadline.poll) {
        result = crux_thread_impl_join(&slot->native, &deadline);
        slot->joining = false;
        if (result == CRUX_OK) {
            slot->used = false;
            *thread = (crux_thread_t){0};
        }
        thread_native_metadata_unlock();
        return result;
    }
    thread_native_metadata_unlock();
    result = crux_thread_impl_join(&slot->native, &deadline);
    thread_native_metadata_lock();
    slot->joining = false;
    if (result == CRUX_OK) {
        slot->used = false;
        *thread = (crux_thread_t){0};
    }
    thread_native_metadata_unlock();
    return result;
}
