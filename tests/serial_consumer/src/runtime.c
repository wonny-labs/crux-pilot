#include "runtime.h"

#include <crux/time.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

static crux_status_t remaining(crux_monotonic_time_t deadline, uint32_t *out_ms) {
    crux_monotonic_time_t now;
    crux_status_t status = crux_time_get_monotonic(&now);
    if (status != CRUX_OK)
        return status;
    if (crux_monotonic_is_at_or_before(deadline, now))
        return CRUX_ERR_TIMEOUT;
    int64_t milliseconds = crux_duration_to_ms_ceil(crux_monotonic_diff(deadline, now));
    *out_ms = (uint32_t)(milliseconds > INT32_MAX ? INT32_MAX : milliseconds);
    return CRUX_OK;
}

crux_status_t serial_runtime_transaction(crux_serial_port_t port,
                                         const crux_serial_config_t *config, uint64_t nonce,
                                         uint32_t budget_ms, serial_runtime_result_t *result) {
    uint8_t request[16] = {0xc3, 0, 0xff, 0x51, 0, 0, 0, 0, 0, 0, 0, 0, 0x39, 0, 0xff, 0x5a};
    uint8_t reply[sizeof(request)] = {0};
    crux_monotonic_time_t deadline;
    uint32_t timeout = 0;
    crux_status_t status;
    size_t offset;

    if (result == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    *result = (serial_runtime_result_t){CRUX_ERR_INVALID_ARGUMENT, CRUX_OK, {0, 0}};
    if (config == NULL)
        return result->transaction_status;
    if (budget_ms == 0 || budget_ms > INT32_MAX) {
        result->transaction_status = CRUX_ERR_OUT_OF_RANGE;
        return result->transaction_status;
    }
    status = crux_time_make_deadline_after(crux_duration_from_ms((int32_t)budget_ms), &deadline);
    if (status != CRUX_OK)
        goto unopened;
    status = crux_serial_open(port, config, &result->retained_handle);
    if (status != CRUX_OK)
        goto unopened;

    for (size_t index = 0; index < 8; ++index) {
        request[4 + index] = (uint8_t)(nonce >> (index * 8));
    }
    offset = 0;
    while (offset < sizeof(request)) {
        size_t count = 0;
        status = remaining(deadline, &timeout);
        if (status != CRUX_OK)
            goto cleanup;
        status = crux_serial_write(result->retained_handle, request + offset,
                                   sizeof(request) - offset, timeout, &count);
        if (status != CRUX_OK)
            goto cleanup;
        if (count == 0 || count > sizeof(request) - offset) {
            status = CRUX_ERR_IO;
            goto cleanup;
        }
        offset += count;
    }
    offset = 0;
    while (offset < sizeof(reply)) {
        size_t count = 0;
        status = remaining(deadline, &timeout);
        if (status != CRUX_OK)
            goto cleanup;
        status = crux_serial_read(result->retained_handle, reply + offset, sizeof(reply) - offset,
                                  timeout, &count);
        if (status != CRUX_OK)
            goto cleanup;
        if (count == 0 || count > sizeof(reply) - offset) {
            status = CRUX_ERR_IO;
            goto cleanup;
        }
        offset += count;
    }
    status = remaining(deadline, &timeout);
    if (status != CRUX_OK)
        goto cleanup;
    request[3] = 0x41;
    if (memcmp(request, reply, sizeof(reply)) != 0)
        status = CRUX_ERR_IO;

cleanup:
    /* Every transfer has returned: close cannot race an operation of ours.
     * Never retry close: IO consumes ownership; BUSY retains it for the caller. */
    result->cleanup_status = crux_serial_close(&result->retained_handle);
unopened:
    result->transaction_status = status;
    return status == CRUX_OK ? result->cleanup_status : status;
}
