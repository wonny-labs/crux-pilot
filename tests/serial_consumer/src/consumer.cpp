#include "runtime.h"

#include <crux/serial.h>
#include <cstddef>

extern "C" crux_status_t serial_cpp_transaction(crux_serial_port_t port,
                                                const crux_serial_config_t *config, uint64_t nonce,
                                                uint32_t budget_ms, serial_runtime_result_t *result)
{
    return serial_runtime_transaction(port, config, nonce, budget_ms, result);
}

extern "C" crux_status_t serial_cpp_zero_write(crux_serial_t live_handle)
{
    std::size_t count = 0;
    const crux_status_t status = crux_serial_write(live_handle, nullptr, 0, 0, &count);
    return status == CRUX_OK && count != 0 ? CRUX_ERR_IO : status;
}
