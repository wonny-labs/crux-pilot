/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Clock and timestamp services. This profile has no inode timestamps, so there is no clock
 * contract: the services succeed and report zero.
 */
#include <zephyr/sys/util.h>

#include <redfs.h>

#if REDCONF_INODE_TIMESTAMPS != 0
#error "Inode timestamps need a real clock service"
#endif

REDSTATUS RedOsClockInit(void)
{
    return 0;
}

REDSTATUS RedOsClockUninit(void)
{
    return 0;
}

uint32_t RedOsClockGetTime(void)
{
    return 0U;
}

REDSTATUS RedOsTimestampInit(void)
{
    return 0;
}

REDSTATUS RedOsTimestampUninit(void)
{
    return 0;
}

REDTIMESTAMP RedOsTimestamp(void)
{
    return 0U;
}

uint64_t RedOsTimePassed(REDTIMESTAMP tsSince)
{
    ARG_UNUSED(tsSince);
    return 0U;
}
