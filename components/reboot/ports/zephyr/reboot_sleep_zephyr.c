/* The sleep port of the reboot service, on the kernel's sleep. Called on the worker thread only. A
 * sleep that the kernel ended early, by k_wakeup(), has not slept the time, and saying that it had
 * would collapse the delay and the wait of the worker. Nothing wakes the worker today. */
#include "reboot_port.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <stdint.h>

bool crux_reboot_port_sleep(uint32_t milliseconds)
{
    return k_msleep((int32_t)MIN(milliseconds, (uint32_t)INT32_MAX)) == 0;
}
