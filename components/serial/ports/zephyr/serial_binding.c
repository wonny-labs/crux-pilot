#include "serial_zephyr.h"
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#define SERIAL_NODE DT_ALIAS(crux_serial0)
#if !DT_NODE_HAS_STATUS(SERIAL_NODE, okay)
#error "Crux Serial requires an explicit dedicated crux-serial0 UART alias"
#else
/* Bounded M5 profile, not generic board/device qualification. */
#if defined(CONFIG_BOARD_MIMXRT1060_EVK) && DT_NODE_EXISTS(DT_NODELABEL(lpuart3))
#define SUPPORTED_TARGET_UART                                                                      \
    (DT_NODE_HAS_COMPAT(SERIAL_NODE, nxp_lpuart) &&                                                \
     DT_SAME_NODE(SERIAL_NODE, DT_NODELABEL(lpuart3)))
#else
#define SUPPORTED_TARGET_UART 0
#endif
BUILD_ASSERT((IS_ENABLED(CONFIG_BOARD_NATIVE_SIM) &&
              DT_NODE_HAS_COMPAT(SERIAL_NODE, zephyr_uart_emul)) ||
                 SUPPORTED_TARGET_UART,
             "Crux Serial unsupported UART resource");
BUILD_ASSERT(!DT_PROP_OR(SERIAL_NODE, hw_flow_control, 0) &&
                 !DT_PROP_OR(SERIAL_NODE, single_wire, 0) &&
                 !DT_PROP_OR(SERIAL_NODE, nxp_rs485_mode, 0),
             "Crux Serial unsupported UART electrical mode");
BUILD_ASSERT(DT_CHILD_NUM_STATUS_OKAY(SERIAL_NODE) == 0,
             "Crux Serial does not support UART child consumers");

#define REJECT_OWNER(node, message) BUILD_ASSERT(!DT_SAME_NODE(SERIAL_NODE, node), message)
#define LOG_RESERVED(node, prop, index)                                                            \
    REJECT_OWNER(DT_PHANDLE_BY_IDX(node, prop, index), "Crux Serial conflicts with UART Logging");
#if defined(CONFIG_LOG_BACKEND_UART)
#if DT_HAS_CHOSEN(zephyr_log_uart)
DT_FOREACH_PROP_ELEM(DT_CHOSEN(zephyr_log_uart), uarts, LOG_RESERVED)
#elif DT_HAS_CHOSEN(zephyr_console)
REJECT_OWNER(DT_CHOSEN(zephyr_console), "Crux Serial conflicts with UART Logging");
#endif
#endif
#if DT_HAS_CHOSEN(zephyr_console) && defined(CONFIG_UART_CONSOLE)
REJECT_OWNER(DT_CHOSEN(zephyr_console), "Crux Serial conflicts with UART Console");
#endif
#if DT_HAS_CHOSEN(zephyr_shell_uart) && defined(CONFIG_SHELL_BACKEND_SERIAL)
REJECT_OWNER(DT_CHOSEN(zephyr_shell_uart), "Crux Serial conflicts with UART Shell");
#endif
#if DT_HAS_CHOSEN(zephyr_uart_mcumgr) && defined(CONFIG_UART_MCUMGR)
REJECT_OWNER(DT_CHOSEN(zephyr_uart_mcumgr), "Crux Serial conflicts with UART management");
#endif
#if DT_HAS_CHOSEN(zephyr_bt_uart) && defined(CONFIG_BT_HCI)
REJECT_OWNER(DT_CHOSEN(zephyr_bt_uart), "Crux Serial conflicts with Bluetooth UART");
#endif
#if DT_NODE_EXISTS(DT_ALIAS(m2_hci_bt_uart)) && defined(CONFIG_BT_HCI)
REJECT_OWNER(DT_ALIAS(m2_hci_bt_uart), "Crux Serial conflicts with Bluetooth UART");
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(lpuart3)) && DT_NODE_EXISTS(DT_NODELABEL(csi))
#if DT_SAME_NODE(SERIAL_NODE, DT_NODELABEL(lpuart3)) && DT_NODE_HAS_STATUS(DT_NODELABEL(csi), okay)
#error "LPUART3 Serial pins conflict with enabled EVKB CSI"
#endif
#endif
const struct device *crux_serial_zephyr_device(unsigned port)
{
    return port == 0 ? DEVICE_DT_GET(SERIAL_NODE) : NULL;
}
#endif
