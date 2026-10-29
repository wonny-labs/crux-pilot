#include "serial_zephyr.h"
#include "serial_port.h"
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#define CAPACITY 256
/* Permanent context: a driver may retain a cached callback after detach. No
 * callback ever retains caller storage or a handle/session allocation. */
static struct {
    struct k_spinlock gate;
    const struct device *dev;
    bool active, failed, quarantine;
    struct uart_config saved;
    struct ring_buf rx, tx;
    uint8_t rx_storage[CAPACITY], tx_storage[CAPACITY];
} native;

K_MUTEX_DEFINE(metadata);
K_SEM_DEFINE(rx_event, 0, 1);
K_SEM_DEFINE(tx_event, 0, 1);

bool crux_serial_context_allowed(void)
{
    return !k_is_in_isr();
}

crux_status_t crux_serial_monotonic_now_ms(uint64_t *now)
{
    int64_t value = k_uptime_get();
    if (value < 0) {
        return CRUX_ERR_IO;
    }
    *now = (uint64_t)value;
    return CRUX_OK;
}

static int64_t remaining(const serial_deadline_t *deadline)
{
    int64_t now = k_uptime_get();
    return now < 0 || (uint64_t)now >= deadline->end_ms
               ? 0
               : (int64_t)(deadline->end_ms - (uint64_t)now);
}

crux_status_t crux_serial_lock(const serial_deadline_t *deadline)
{
    if (deadline == NULL) {
        return k_mutex_lock(&metadata, K_FOREVER) == 0 ? CRUX_OK : CRUX_ERR_IO;
    }
    if (deadline->poll) {
        return k_mutex_lock(&metadata, K_NO_WAIT) == 0 ? CRUX_OK : CRUX_ERR_BUSY;
    }
    int64_t left = remaining(deadline);
    if (left == 0) {
        return CRUX_ERR_TIMEOUT;
    }
    return k_mutex_lock(&metadata, K_MSEC(left)) == 0 ? CRUX_OK : CRUX_ERR_TIMEOUT;
}

void crux_serial_unlock(void)
{
    (void)k_mutex_unlock(&metadata);
}

static void disable(const struct device *dev)
{
    uart_irq_rx_disable(dev);
    uart_irq_tx_disable(dev);
    uart_irq_err_disable(dev);
}

static void fault(void)
{
    native.failed = true;
    disable(native.dev);
    k_sem_give(&rx_event);
    k_sem_give(&tx_event);
}

static bool observed_error(void)
{
    int error = uart_err_check(native.dev);
    if (error != 0 && error != -ENOSYS && error != -ENOTSUP) {
        fault();
    }
    return native.failed;
}

static void callback(const struct device *unused, void *context)
{
    ARG_UNUSED(unused);
    ARG_UNUSED(context);
    k_spinlock_key_t key = k_spin_lock(&native.gate);
    if (!native.active || native.failed) {
        goto done;
    }
    const struct device *dev = native.dev;
    if (observed_error()) {
        goto done;
    }
    if (uart_irq_update(dev) < 0) {
        fault();
        goto done;
    }
    /* Bounded per invocation; native IRQ is level-triggered or the emulator
     * repeats while pending. A provider overflow is a latched visible fault. */
    for (int i = 0; i < CAPACITY && uart_irq_rx_ready(dev) > 0; ++i) {
        uint8_t byte;
        int count = uart_fifo_read(dev, &byte, 1);
        if (count < 0) {
            fault();
            break;
        }
        if (count == 0) {
            break;
        }
        if (ring_buf_put(&native.rx, &byte, 1) != 1) {
            fault();
            break;
        }
        k_sem_give(&rx_event);
    }
    if (!native.failed && uart_irq_tx_ready(dev) > 0) {
        uint8_t *bytes;
        uint32_t size = ring_buf_get_claim(&native.tx, &bytes, CAPACITY);
        if (size != 0) {
            int count = uart_fifo_fill(dev, bytes, (int)size);
            if (count < 0 || (uint32_t)count > size) {
                ring_buf_get_finish(&native.tx, 0);
                fault();
            } else {
                ring_buf_get_finish(&native.tx, (uint32_t)count);
                if (count > 0) {
                    k_sem_give(&tx_event);
                }
            }
        }
        if (ring_buf_is_empty(&native.tx)) {
            uart_irq_tx_disable(dev);
        }
    }
done:
    k_spin_unlock(&native.gate, key);
}

crux_status_t crux_serial_impl_open(uint32_t port, const crux_serial_config_t *config)
{
    const struct device *dev = crux_serial_zephyr_device(port);
    if (dev == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!device_is_ready(dev)) {
        return CRUX_ERR_NOT_READY;
    }
    if (native.quarantine) {
        return CRUX_ERR_IO;
    }

    const struct uart_driver_api *api = dev->api;
    if (!api->configure || !api->config_get || !api->irq_callback_set || !api->fifo_fill ||
        !api->fifo_read || !api->irq_tx_enable || !api->irq_tx_disable || !api->irq_rx_enable ||
        !api->irq_rx_disable || !api->irq_tx_ready || !api->irq_rx_ready || !api->irq_update) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (uart_config_get(dev, &native.saved) != 0) {
        return CRUX_ERR_NOT_SUPPORTED;
    }

    struct uart_config requested = {
        .baudrate = config->baud_rate,
        .data_bits = config->data_bits == 7 ? UART_CFG_DATA_BITS_7 : UART_CFG_DATA_BITS_8,
        .stop_bits = config->stop_bits == 1 ? UART_CFG_STOP_BITS_1 : UART_CFG_STOP_BITS_2,
        .parity = config->parity == CRUX_SERIAL_PARITY_NONE   ? UART_CFG_PARITY_NONE
                  : config->parity == CRUX_SERIAL_PARITY_EVEN ? UART_CFG_PARITY_EVEN
                                                              : UART_CFG_PARITY_ODD,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE};
    disable(dev);
    int result = uart_configure(dev, &requested);
    struct uart_config actual;
    if (result == 0) {
        result = uart_config_get(dev, &actual);
    }
    if (result == 0 &&
        (actual.baudrate != requested.baudrate || actual.data_bits != requested.data_bits ||
         actual.stop_bits != requested.stop_bits || actual.parity != requested.parity ||
         actual.flow_ctrl != requested.flow_ctrl)) {
        result = -ENOTSUP;
    }
    if (result != 0) {
        if (uart_configure(dev, &native.saved) != 0) {
            native.quarantine = true;
        }
        return result == -ENOTSUP || result == -ENOSYS || result == -EINVAL ? CRUX_ERR_NOT_SUPPORTED
                                                                            : CRUX_ERR_IO;
    }

    k_spinlock_key_t key = k_spin_lock(&native.gate);
    native.dev = dev;
    native.failed = false;
    ring_buf_init(&native.rx, CAPACITY, native.rx_storage);
    ring_buf_init(&native.tx, CAPACITY, native.tx_storage);
    k_sem_reset(&rx_event);
    k_sem_reset(&tx_event);
    (void)uart_err_check(dev); /* Start a fresh session, not an old error report. */
    native.active = true;
    result = uart_irq_callback_user_data_set(dev, callback, &native);
    if (result == 0) {
        uart_irq_err_enable(dev);
        uart_irq_rx_enable(dev);
    } else {
        native.active = false;
    }
    k_spin_unlock(&native.gate, key);
    if (result != 0) {
        if (uart_configure(dev, &native.saved) != 0) {
            native.quarantine = true;
        }
        return CRUX_ERR_NOT_SUPPORTED;
    }
    return CRUX_OK;
}

crux_status_t crux_serial_impl_close(uint32_t port)
{
    ARG_UNUSED(port);
    k_spinlock_key_t key = k_spin_lock(&native.gate);
    native.active = false;
    disable(native.dev);
    int result = uart_irq_callback_user_data_set(native.dev, NULL, NULL);
    ring_buf_reset(&native.rx);
    ring_buf_reset(&native.tx);
    k_spin_unlock(&native.gate, key);

    /* Discard queued native RX with a fixed setup/cleanup bound. A continuously
     * transmitting peer must not make close unbounded; quarantine if not drained.
     * Native hardware TX bytes already accepted cannot be recalled. */
    uint8_t discarded;
    unsigned drained = 0;
    while (drained < 1024 && uart_poll_in(native.dev, &discarded) == 0) {
        ++drained;
    }
    if (drained == 1024) {
        result = -EIO;
    }
    if (uart_configure(native.dev, &native.saved) != 0) {
        result = -EIO;
    }
    if (result != 0) {
        native.quarantine = true;
    }
    return result == 0 ? CRUX_OK : CRUX_ERR_IO;
}

static crux_status_t transfer(void *buffer, size_t size, const serial_deadline_t *deadline,
                              size_t *count, bool writing)
{
    for (;;) {
        k_spinlock_key_t key;
        if (k_spin_trylock(&native.gate, &key) != 0) {
            if (deadline->poll) {
                return CRUX_ERR_BUSY;
            }
            if (remaining(deadline) == 0) {
                return CRUX_ERR_TIMEOUT;
            }
            k_sleep(K_MSEC(1));
            continue;
        }
        crux_status_t result = CRUX_OK;
        if (observed_error()) {
            result = CRUX_ERR_IO;
        } else if (!deadline->poll && remaining(deadline) == 0) {
            result = CRUX_ERR_TIMEOUT;
        } else {
            *count = writing ? ring_buf_put(&native.tx, buffer, MIN(size, CAPACITY))
                             : ring_buf_get(&native.rx, buffer, MIN(size, CAPACITY));
            if (*count != 0 && writing) {
                uart_irq_tx_enable(native.dev);
            }
        }
        k_spin_unlock(&native.gate, key);
        if (result != CRUX_OK || *count != 0) {
            return result;
        }
        if (deadline->poll) {
            return CRUX_ERR_BUSY;
        }
        int64_t left = remaining(deadline);
        if (left == 0) {
            return CRUX_ERR_TIMEOUT;
        }
        if (k_sem_take(writing ? &tx_event : &rx_event, K_MSEC(left)) != 0) {
            return CRUX_ERR_TIMEOUT;
        }
    }
}

crux_status_t crux_serial_impl_read(uint32_t port, void *buffer, size_t size,
                                    const serial_deadline_t *deadline, size_t *count)
{
    ARG_UNUSED(port);
    return transfer(buffer, size, deadline, count, false);
}

crux_status_t crux_serial_impl_write(uint32_t port, const void *buffer, size_t size,
                                     const serial_deadline_t *deadline, size_t *count)
{
    ARG_UNUSED(port);
    return transfer((void *)buffer, size, deadline, count, true);
}
