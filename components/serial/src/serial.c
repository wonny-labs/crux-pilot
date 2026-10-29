#include "serial_port.h"
#include <limits.h>

typedef enum { SLOT_FREE, SLOT_OPENING, SLOT_OPEN, SLOT_CLOSING } slot_state_t;
typedef struct {
    uint32_t generation;
    slot_state_t state;
    bool reading, writing;
} serial_slot_t;

static serial_slot_t slots[CRUX_SERIAL_PORT_COUNT];

static crux_status_t deadline_start(uint32_t timeout, serial_deadline_t *deadline)
{
    if (timeout > INT32_MAX)
        return CRUX_ERR_OUT_OF_RANGE;
    uint64_t now;
    crux_status_t result = crux_serial_monotonic_now_ms(&now);
    if (result != CRUX_OK)
        return result;
    if (UINT64_MAX - now < timeout)
        return CRUX_ERR_OUT_OF_RANGE;
    *deadline = (serial_deadline_t){.end_ms = now + timeout, .poll = timeout == 0};
    return CRUX_OK;
}

static serial_slot_t *find(crux_serial_t handle)
{
    if (handle.slot == 0 || handle.slot > CRUX_SERIAL_PORT_COUNT)
        return NULL;
    serial_slot_t *slot = &slots[handle.slot - 1];
    return slot->state == SLOT_OPEN && slot->generation == handle.generation ? slot : NULL;
}

static crux_status_t config_check(const crux_serial_config_t *config)
{
    if (config == NULL || config->baud_rate == 0 ||
        (config->data_bits != 7 && config->data_bits != 8) ||
        (config->stop_bits != 1 && config->stop_bits != 2) ||
        (config->parity != CRUX_SERIAL_PARITY_NONE && config->parity != CRUX_SERIAL_PARITY_EVEN &&
         config->parity != CRUX_SERIAL_PARITY_ODD))
        return CRUX_ERR_INVALID_ARGUMENT;
    switch (config->baud_rate) {
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
        return CRUX_OK;
    default:
        return CRUX_ERR_NOT_SUPPORTED;
    }
}

crux_status_t crux_serial_open(crux_serial_port_t port, const crux_serial_config_t *config,
                               crux_serial_t *out)
{
    if (out == NULL || port >= CRUX_SERIAL_PORT_COUNT)
        return CRUX_ERR_INVALID_ARGUMENT;
    crux_status_t result = config_check(config);
    if (result != CRUX_OK)
        return result;
    if (!crux_serial_context_allowed())
        return CRUX_ERR_NOT_SUPPORTED;
    serial_deadline_t poll = {.poll = true};
    result = crux_serial_lock(&poll);
    if (result != CRUX_OK)
        return result;
    serial_slot_t *slot = &slots[port];
    if (slot->state != SLOT_FREE)
        result = CRUX_ERR_BUSY;
    else if (slot->generation == UINT32_MAX)
        result = CRUX_ERR_NO_RESOURCES;
    else {
        ++slot->generation;
        slot->state = SLOT_OPENING;
    }
    crux_serial_unlock();
    if (result != CRUX_OK)
        return result;
    result = crux_serial_impl_open(port, config);
    (void)crux_serial_lock(NULL);
    slot->state = result == CRUX_OK ? SLOT_OPEN : SLOT_FREE;
    if (result == CRUX_OK)
        *out = (crux_serial_t){port + 1, slot->generation};
    crux_serial_unlock();
    return result;
}

static crux_status_t transfer(crux_serial_t handle, void *read_buffer, const void *write_buffer,
                              size_t size, size_t *count, uint32_t timeout, bool writing)
{
    if (count != NULL)
        *count = 0;
    if (count == NULL || (size != 0 && (writing ? write_buffer == NULL : read_buffer == NULL)))
        return CRUX_ERR_INVALID_ARGUMENT;
    if (!crux_serial_context_allowed())
        return CRUX_ERR_NOT_SUPPORTED;
    serial_deadline_t deadline;
    crux_status_t result = deadline_start(timeout, &deadline);
    if (result != CRUX_OK)
        return result;
    result = crux_serial_lock(&deadline);
    if (result != CRUX_OK)
        return result;
    serial_slot_t *slot = find(handle);
    if (slot == NULL)
        result = CRUX_ERR_INVALID_STATE;
    else if (size == 0)
        result = CRUX_OK;
    else if (writing ? slot->writing : slot->reading)
        result = CRUX_ERR_BUSY;
    else if (writing)
        slot->writing = true;
    else
        slot->reading = true;
    crux_serial_unlock();
    if (result != CRUX_OK || size == 0)
        return result;
    result = writing ? crux_serial_impl_write(handle.slot - 1, write_buffer, size, &deadline, count)
                     : crux_serial_impl_read(handle.slot - 1, read_buffer, size, &deadline, count);
    (void)crux_serial_lock(NULL);
    if (writing)
        slot->writing = false;
    else
        slot->reading = false;
    crux_serial_unlock();
    return result;
}

crux_status_t crux_serial_read(crux_serial_t serial, void *buffer, size_t capacity,
                               uint32_t timeout_ms, size_t *transferred)
{
    return transfer(serial, buffer, NULL, capacity, transferred, timeout_ms, false);
}

crux_status_t crux_serial_write(crux_serial_t serial, const void *buffer, size_t size,
                                uint32_t timeout_ms, size_t *transferred)
{
    return transfer(serial, NULL, buffer, size, transferred, timeout_ms, true);
}

crux_status_t crux_serial_close(crux_serial_t *handle)
{
    if (handle == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (!crux_serial_context_allowed())
        return CRUX_ERR_NOT_SUPPORTED;
    serial_deadline_t poll = {.poll = true};
    crux_status_t result = crux_serial_lock(&poll);
    if (result != CRUX_OK)
        return result;
    serial_slot_t *slot = find(*handle);
    if (slot == NULL)
        result = CRUX_ERR_INVALID_STATE;
    else if (slot->reading || slot->writing)
        result = CRUX_ERR_BUSY;
    else
        slot->state = SLOT_CLOSING;
    crux_serial_unlock();
    if (result != CRUX_OK)
        return result;
    result = crux_serial_impl_close(handle->slot - 1);
    (void)crux_serial_lock(NULL);
    slot->state = SLOT_FREE;
    *handle = (crux_serial_t){0};
    crux_serial_unlock();
    return result;
}
