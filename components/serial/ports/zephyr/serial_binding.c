#include "serial_zephyr.h"
#include <zephyr/devicetree.h>
#if !DT_NODE_HAS_STATUS(DT_ALIAS(crux_serial0), okay)
#error "Crux Serial requires an explicit dedicated crux-serial0 UART alias"
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(lpuart3)) && DT_NODE_EXISTS(DT_NODELABEL(csi))
#if DT_SAME_NODE(DT_ALIAS(crux_serial0), DT_NODELABEL(lpuart3)) &&                                 \
    DT_NODE_HAS_STATUS(DT_NODELABEL(csi), okay)
#error "LPUART3 Serial pins conflict with enabled EVKB CSI"
#endif
#endif
const struct device *serial_zephyr_device(unsigned port) {
    return port == 0 ? DEVICE_DT_GET(DT_ALIAS(crux_serial0)) : NULL;
}
#define LOG_RESERVED(node, prop, index)                                                            \
    if (dev == DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node, prop, index)))                                \
        return true;
bool serial_zephyr_reserved(const struct device *dev) {
#if DT_HAS_CHOSEN(zephyr_log_uart) && defined(CONFIG_LOG_BACKEND_UART)
    DT_FOREACH_PROP_ELEM(DT_CHOSEN(zephyr_log_uart), uarts, LOG_RESERVED)
#endif
#if DT_HAS_CHOSEN(zephyr_console) &&                                                               \
    (defined(CONFIG_UART_CONSOLE) ||                                                               \
     (defined(CONFIG_LOG_BACKEND_UART) && !DT_HAS_CHOSEN(zephyr_log_uart)))
    if (dev == DEVICE_DT_GET(DT_CHOSEN(zephyr_console)))
        return true;
#endif
#if DT_HAS_CHOSEN(zephyr_shell_uart) && defined(CONFIG_SHELL_BACKEND_SERIAL)
    if (dev == DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart)))
        return true;
#endif
#if DT_HAS_CHOSEN(zephyr_uart_mcumgr) && defined(CONFIG_MCUMGR_TRANSPORT_UART)
    if (dev == DEVICE_DT_GET(DT_CHOSEN(zephyr_uart_mcumgr)))
        return true;
#endif
#if DT_HAS_CHOSEN(zephyr_bt_uart) && defined(CONFIG_BT_HCI)
    if (dev == DEVICE_DT_GET(DT_CHOSEN(zephyr_bt_uart)))
        return true;
#endif
#if DT_NODE_EXISTS(DT_ALIAS(m2_hci_bt_uart)) && defined(CONFIG_BT_HCI)
    if (dev == DEVICE_DT_GET(DT_ALIAS(m2_hci_bt_uart)))
        return true;
#endif
    return false;
}
