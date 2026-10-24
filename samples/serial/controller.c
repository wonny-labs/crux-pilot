#include "controller.h"
#include <crux/monotonic.h>
#include <string.h>
static crux_status_t exchange(crux_serial_t handle, int64_t deadline, uint32_t nonce) {
    unsigned char request[8] = {0xc3, 0, 0xff, 0x51, 0, 0, 0, 0};
    for (unsigned i = 0; i < 4; ++i)
        request[4 + i] = (unsigned char)(nonce >> (8 * i));
    unsigned char response[8] = {0};
    for (int direction = 0; direction < 2; ++direction) {
        size_t offset = 0;
        while (offset < sizeof(request)) {
            int64_t now;
            crux_status_t status = crux_monotonic_now(&now);
            if (status != CRUX_OK)
                return status;
            if (now >= deadline)
                return CRUX_ERR_TIMEOUT;
            size_t count;
            uint32_t remaining = (uint32_t)(deadline - now);
            status = direction == 0
                         ? crux_serial_write(handle, request + offset, sizeof(request) - offset,
                                             remaining, &count)
                         : crux_serial_read(handle, response + offset, sizeof(response) - offset,
                                            remaining, &count);
            if (status != CRUX_OK)
                return status;
            if (count == 0)
                return CRUX_ERR_IO;
            offset += count;
        }
    }
    int64_t now;
    crux_status_t status = crux_monotonic_now(&now);
    if (status != CRUX_OK)
        return status;
    if (now >= deadline)
        return CRUX_ERR_TIMEOUT;
    unsigned char expected[8];
    memcpy(expected, request, sizeof(expected));
    expected[3] = 0x41;
    return memcmp(expected, response, sizeof(expected)) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
crux_status_t serial_controller_run(crux_serial_port_t port, uint32_t nonce) {
    int64_t now;
    crux_status_t status = crux_monotonic_now(&now);
    if (status != CRUX_OK)
        return status;
    if (now > INT64_MAX - 200)
        return CRUX_ERR_OUT_OF_RANGE;
    const crux_serial_config_t config = {115200, 8, 1, CRUX_SERIAL_PARITY_NONE};
    crux_serial_t handle;
    status = crux_serial_open(port, &config, &handle);
    if (status != CRUX_OK)
        return status;
    status = exchange(handle, now + 200, nonce);
    crux_status_t cleanup = crux_serial_close(&handle);
    return status == CRUX_OK ? cleanup : status;
}
