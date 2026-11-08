#include "i2c_status.h"

#include <assert.h>
#include <errno.h>

int main(void)
{
    assert(crux_i2c_zephyr_status(0) == CRUX_OK);
    /* Only -ENOTSUP and -ENOSYS mean unsupported; every other failure is an I/O failure. */
    assert(crux_i2c_zephyr_status(-ENOTSUP) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_i2c_zephyr_status(-ENOSYS) == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_i2c_zephyr_status(-EINVAL) == CRUX_ERR_IO);
    assert(crux_i2c_zephyr_status(-EIO) == CRUX_ERR_IO);
    assert(crux_i2c_zephyr_status(-ENXIO) == CRUX_ERR_IO);
    assert(crux_i2c_zephyr_status(-EBUSY) == CRUX_ERR_IO);
    assert(crux_i2c_zephyr_status(-ETIMEDOUT) == CRUX_ERR_IO);
    return 0;
}
