#include "system_zephyr.h"
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_CRUX_SYSTEM_NATIVE_SIM_WATCHDOG) && !defined(CONFIG_BOARD_NATIVE_SIM)
#error "CRUX_SYSTEM_NATIVE_SIM_WATCHDOG is a native_sim test setting, refused on other boards"
#endif

#if defined(CONFIG_BOARD_NATIVE_SIM)
#if defined(CONFIG_CRUX_SYSTEM_NATIVE_SIM_WATCHDOG)
#include "system_sim_watchdog.h"
/* Test-only: the private simulated watchdog of system_sim_watchdog.c. */

const struct device *crux_watchdog_zephyr_device(void)
{
    return crux_system_sim_watchdog_device();
}

bool crux_watchdog_zephyr_running(void)
{
    return crux_system_sim_watchdog_running();
}
#else
/* No physical watchdog is claimed by the normal simulator composition. Tests
 * substitute an actual native driver object at this private binding seam. */

const struct device *crux_watchdog_zephyr_device(void)
{
    return NULL;
}

bool crux_watchdog_zephyr_running(void)
{
    return false;
}
#endif
#elif defined(CONFIG_BOARD_MIMXRT1060_EVK)
#if !DT_NODE_EXISTS(DT_ALIAS(crux_watchdog0))
#error "Crux System requires explicit crux-watchdog0 selection"
#elif !DT_NODE_HAS_STATUS(DT_ALIAS(crux_watchdog0), okay)
#error "Crux System watchdog must be enabled"
#else
#include <fsl_wdog.h>

#define SYSTEM_WDOG DT_ALIAS(crux_watchdog0)
BUILD_ASSERT(DT_NODE_HAS_STATUS(SYSTEM_WDOG, okay), "Crux System watchdog must be enabled");
BUILD_ASSERT(DT_SAME_NODE(SYSTEM_WDOG, DT_NODELABEL(wdog0)) &&
                 DT_NODE_HAS_COMPAT(SYSTEM_WDOG, nxp_imx_wdog) &&
                 DT_REG_ADDR(SYSTEM_WDOG) == 0x400b8000,
             "Crux System requires the qualified RT1060 WDOG1 resource");

const struct device *crux_watchdog_zephyr_device(void)
{
    return DEVICE_DT_GET(SYSTEM_WDOG);
}

bool crux_watchdog_zephyr_running(void)
{
    return (WDOG_GetStatusFlags((WDOG_Type *)DT_REG_ADDR(SYSTEM_WDOG)) & kWDOG_RunningFlag) != 0;
}
#endif
#else
#error "Crux System unsupported Zephyr board"
#endif
