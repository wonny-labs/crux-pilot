/* Host stand-in for Zephyr's printk.h. */
#ifndef HOST_SHIM_ZEPHYR_SYS_PRINTK_H
#define HOST_SHIM_ZEPHYR_SYS_PRINTK_H
#include <stdio.h>
#define printk printf
#endif
