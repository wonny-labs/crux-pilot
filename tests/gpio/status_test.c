#include "gpio_status.h"

#include <assert.h>
#include <errno.h>

int main(void)
{
    assert(crux_gpio_zephyr_status(0) == CRUX_OK);
    assert(crux_gpio_zephyr_status(-ENOTSUP) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_gpio_zephyr_status(-ENOSYS) == CRUX_ERR_NOT_SUPPORTED);
    /* Only -ENOTSUP and -ENOSYS mean unsupported; -EINVAL is an I/O-level failure. */
    assert(crux_gpio_zephyr_status(-EINVAL) == CRUX_ERR_IO);
    assert(crux_gpio_zephyr_status(-EIO) == CRUX_ERR_IO);
    assert(crux_gpio_zephyr_status(-EBUSY) == CRUX_ERR_IO);
    return 0;
}
