#include "queue_port.h"
#include <crux/queue.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>

#define QUEUE_COUNT 4U
#define QUEUE_CAPACITY 16U
#define QUEUE_RECORD 64U

typedef struct {
    uint32_t generation;
    bool live;
    bool retired;
    size_t capacity, size, head, count, active;
    unsigned char data[QUEUE_CAPACITY][QUEUE_RECORD];
} queue_slot_t;

static queue_slot_t slots[QUEUE_COUNT];

static queue_slot_t *lookup(crux_queue_t handle)
{
    if (handle.slot == 0 || handle.slot > QUEUE_COUNT || handle.generation == 0)
        return NULL;
    queue_slot_t *slot = &slots[handle.slot - 1];
    return slot->live && slot->generation == handle.generation ? slot : NULL;
}

crux_status_t crux_queue_create(size_t capacity, size_t message_size, crux_queue_t *out)
{
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    *out = (crux_queue_t){0};
    if (capacity == 0 || message_size == 0)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (capacity > QUEUE_CAPACITY || message_size > QUEUE_RECORD)
        return CRUX_ERR_OUT_OF_RANGE;
    if (!queue_port_context())
        return CRUX_ERR_NOT_SUPPORTED;
    crux_status_t result = queue_port_lock(NULL);
    if (result != CRUX_OK)
        return result;
    result = CRUX_ERR_NO_RESOURCES;
    for (size_t i = 0; i < QUEUE_COUNT; ++i) {
        queue_slot_t *slot = &slots[i];
        if (slot->live || slot->retired)
            continue;
        if (slot->generation == 0)
            slot->generation = 1;
        slot->capacity = capacity;
        slot->size = message_size;
        slot->head = 0;
        slot->count = 0;
        slot->active = 0;
        result = queue_port_prepare(i);
        if (result != CRUX_OK)
            break;
        slot->live = true;
        *out = (crux_queue_t){(uint32_t)i + 1U, slot->generation};
        break;
    }
    queue_port_unlock();
    return result;
}

static crux_status_t transfer(crux_queue_t handle, uint32_t timeout_ms, const void *source,
                              void *destination, size_t size, bool sending)
{
    if ((sending ? source == NULL : destination == NULL) || size == 0 || handle.slot == 0 ||
        handle.slot > QUEUE_COUNT || handle.generation == 0)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (timeout_ms > INT32_MAX)
        return CRUX_ERR_OUT_OF_RANGE;
    if (!queue_port_context())
        return CRUX_ERR_NOT_SUPPORTED;
    queue_deadline_t deadline;
    crux_status_t result = queue_port_deadline(timeout_ms, &deadline);
    if (result != CRUX_OK)
        return result;
    result = queue_port_lock(&deadline);
    if (result != CRUX_OK)
        return result;
    queue_slot_t *slot = lookup(handle);
    if (slot == NULL || size != slot->size) {
        queue_port_unlock();
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    ++slot->active;
    while (sending ? slot->count == slot->capacity : slot->count == 0) {
        if (timeout_ms == 0) {
            result = CRUX_ERR_BUSY;
            break;
        }
        result = queue_port_wait(handle.slot - 1U, &deadline);
        if (result != CRUX_OK)
            break;
    }
    /* A reported timeout is terminal, including during metadata reacquisition.
     * Retain the admitted reference for cleanup, without a late commit. */
    bool available = sending ? slot->count < slot->capacity : slot->count != 0;
    if (available && result == CRUX_OK) {
        if (sending) {
            size_t tail = (slot->head + slot->count) % slot->capacity;
            memcpy(slot->data[tail], source, size);
            ++slot->count;
        } else {
            memcpy(destination, slot->data[slot->head], size);
            slot->head = (slot->head + 1) % slot->capacity;
            --slot->count;
        }
        queue_port_notify(handle.slot - 1U);
        result = CRUX_OK;
    }
    --slot->active;
    queue_port_unlock();
    return result;
}

crux_status_t crux_queue_send(crux_queue_t queue, const void *record, size_t size,
                              uint32_t timeout_ms)
{
    return transfer(queue, timeout_ms, record, NULL, size, true);
}

crux_status_t crux_queue_receive(crux_queue_t queue, void *record, size_t size, uint32_t timeout_ms)
{
    return transfer(queue, timeout_ms, NULL, record, size, false);
}

crux_status_t crux_queue_destroy(crux_queue_t *queue)
{
    if (queue == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (!queue_port_context())
        return CRUX_ERR_NOT_SUPPORTED;
    crux_status_t result = queue_port_lock(NULL);
    if (result != CRUX_OK)
        return result;
    queue_slot_t *slot = lookup(*queue);
    if (slot == NULL)
        result = CRUX_ERR_INVALID_ARGUMENT;
    else if (slot->active != 0)
        result = CRUX_ERR_BUSY;
    else {
        slot->live = false;
        if (slot->generation == UINT32_MAX)
            slot->retired = true;
        else
            ++slot->generation;
        *queue = (crux_queue_t){0};
    }
    queue_port_unlock();
    return result;
}
