/* Standalone deterministic consumer tests. Public API doubles isolate protocol
 * policy; native-provider/PTY coverage belongs to the separate integration. */
#include "runtime.h"
#include <assert.h>
#include <crux/time.h>
#include <limits.h>
#include <string.h>

static const crux_serial_config_t config = {115200, 8, 1, CRUX_SERIAL_PARITY_NONE};
static const uint8_t request[16] = {0xc3, 0, 0xff, 0x51, 8,    7, 6,    5,
                                    4,    3, 2,    1,    0x39, 0, 0xff, 0x5a};
static uint8_t reply[16];
static uint8_t sent[16];
static size_t written;
static size_t received;
static int64_t now_ms;
static unsigned closes;
static int stale;
static int silent;
static crux_status_t close_status;
static int64_t open_delay;
static int64_t phase_ns;
static unsigned deadlines;

static void reset(void) {
    memcpy(reply, request, sizeof(reply));
    reply[3] = 0x41;
    memset(sent, 0, sizeof(sent));
    written = received = closes = 0;
    now_ms = 100;
    stale = silent = 0;
    close_status = CRUX_OK;
    open_delay = 0;
    phase_ns = 0;
    deadlines = 0;
}

crux_status_t crux_time_get_monotonic(crux_monotonic_time_t *out) {
    *out = (crux_monotonic_time_t){now_ms * INT64_C(1000000) + phase_ns};
    return CRUX_OK;
}
crux_status_t crux_time_make_deadline_after(crux_duration_t duration, crux_monotonic_time_t *out) {
    ++deadlines;
    int64_t now = now_ms * INT64_C(1000000) + phase_ns;
    int64_t delta = crux_duration_to_ns(duration);
    if (now > INT64_MAX - delta)
        return CRUX_ERR_OUT_OF_RANGE;
    *out = (crux_monotonic_time_t){now + delta};
    return CRUX_OK;
}
crux_status_t crux_serial_open(crux_serial_port_t port, const crux_serial_config_t *settings,
                               crux_serial_t *out) {
    assert(port == 7 && settings->baud_rate == 115200);
    now_ms += open_delay;
    *out = (crux_serial_t){1, 1};
    return CRUX_OK;
}
crux_status_t crux_serial_write(crux_serial_t handle, const void *data, size_t size,
                                uint32_t timeout, size_t *count) {
    assert(handle.slot == 1 && size > 0);
    assert(timeout == (uint32_t)(120 - now_ms));
    *count = size < 3 ? size : 3;
    memcpy(sent + written, data, *count);
    written += *count;
    ++now_ms;
    phase_ns = 1;
    return CRUX_OK;
}
crux_status_t crux_serial_read(crux_serial_t handle, void *data, size_t size, uint32_t timeout,
                               size_t *count) {
    assert(handle.slot == 1 && written == sizeof(request));
    assert(memcmp(sent, request, sizeof(request)) == 0);
    assert(timeout == (uint32_t)(120 - now_ms));
    if (silent) {
        now_ms += timeout;
        *count = 0;
        return CRUX_ERR_TIMEOUT;
    }
    if (stale)
        reply[4] = 9;
    *count = size < 2 ? size : 2;
    memcpy(data, reply + received, *count);
    received += *count;
    ++now_ms;
    phase_ns = 1;
    return CRUX_OK;
}
crux_status_t crux_serial_close(crux_serial_t *handle) {
    ++closes;
    if (close_status != CRUX_ERR_BUSY)
        *handle = (crux_serial_t){0, 0};
    return close_status;
}

int main(void) {
    serial_runtime_result_t result;
    reset();
    assert(serial_runtime_transaction(7, &config, UINT64_C(0x0102030405060708), 20, &result) ==
           CRUX_OK);
    assert(closes == 1 && received == 16 && result.retained_handle.slot == 0);
    assert(deadlines == 1);
    reset();
    stale = 1;
    assert(serial_runtime_transaction(7, &config, UINT64_C(0x0102030405060708), 20, &result) ==
           CRUX_ERR_IO);
    assert(closes == 1 && result.cleanup_status == CRUX_OK);
    reset();
    silent = 1;
    close_status = CRUX_ERR_IO;
    assert(serial_runtime_transaction(7, &config, UINT64_C(0x0102030405060708), 20, &result) ==
           CRUX_ERR_TIMEOUT);
    assert(now_ms == 120 && closes == 1 && result.cleanup_status == CRUX_ERR_IO);
    assert(result.retained_handle.slot == 0);
    reset();
    close_status = CRUX_ERR_BUSY;
    assert(serial_runtime_transaction(7, &config, UINT64_C(0x0102030405060708), 20, &result) ==
           CRUX_ERR_BUSY);
    assert(result.transaction_status == CRUX_OK && result.retained_handle.slot == 1);
    reset();
    assert(serial_runtime_transaction(7, &config, 1, 0, &result) == CRUX_ERR_OUT_OF_RANGE);
    assert(closes == 0);
    reset();
    open_delay = 20;
    assert(serial_runtime_transaction(7, &config, 1, 20, &result) == CRUX_ERR_TIMEOUT);
    assert(closes == 1 && written == 0 && received == 0);
    reset();
    now_ms = INT64_MAX / INT64_C(1000000) - 5;
    assert(serial_runtime_transaction(7, &config, 1, 20, &result) == CRUX_ERR_OUT_OF_RANGE);
    assert(closes == 0 && written == 0);
    reset();
    close_status = CRUX_ERR_IO;
    assert(serial_runtime_transaction(7, &config, UINT64_C(0x0102030405060708), 20, &result) ==
           CRUX_ERR_IO);
    assert(result.transaction_status == CRUX_OK && result.cleanup_status == CRUX_ERR_IO);
    assert(result.retained_handle.slot == 0);
    return 0;
}
