#ifndef CRUX_STATUS_H
#define CRUX_STATUS_H

typedef enum {
    CRUX_OK = 0,
    CRUX_ERR_INVALID_ARGUMENT = -1,
    CRUX_ERR_OUT_OF_RANGE = -2,
    CRUX_ERR_NOT_READY = -3,
    CRUX_ERR_NO_DATA = -4,
    CRUX_ERR_IO = -5,
    CRUX_ERR_BUSY = -6,
    CRUX_ERR_TIMEOUT = -7,
    CRUX_ERR_NO_RESOURCES = -8,
    CRUX_ERR_NOT_SUPPORTED = -9,
    CRUX_ERR_INVALID_STATE = -10
} crux_status_t;

#endif
