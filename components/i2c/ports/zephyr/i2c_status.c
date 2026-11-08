#include "i2c_status.h"

#include <errno.h>

crux_status_t crux_i2c_zephyr_status(int native_result)
{
    if (native_result == 0) {
        return CRUX_OK;
    }
    if (native_result == -ENOTSUP || native_result == -ENOSYS) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    return CRUX_ERR_IO;
}
