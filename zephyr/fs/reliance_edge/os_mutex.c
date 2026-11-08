/* SPDX-License-Identifier: Apache-2.0 */
/* The file system mutex as a Zephyr k_mutex. A lock or unlock that fails is a programming error. */
#include <zephyr/kernel.h>

#include <redfs.h>

static struct k_mutex crux_red_mutex;

REDSTATUS RedOsMutexInit(void)
{
    return (k_mutex_init(&crux_red_mutex) == 0) ? 0 : -RED_EINVAL;
}

REDSTATUS RedOsMutexUninit(void)
{
    return 0;
}

void RedOsMutexAcquire(void)
{
    if (k_mutex_lock(&crux_red_mutex, K_FOREVER) != 0) {
        k_panic();
    }
}

void RedOsMutexRelease(void)
{
    if (k_mutex_unlock(&crux_red_mutex) != 0) {
        k_panic();
    }
}
