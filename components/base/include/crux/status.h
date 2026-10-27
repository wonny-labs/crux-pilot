#ifndef CRUX_STATUS_H
#define CRUX_STATUS_H

/**
 * Shared results: zero is success and negative values indicate failure.
 * NOT_READY means an unavailable service; NO_DATA means no valid observation.
 * BUSY denotes temporary unavailability; TIMEOUT denotes an expired finite wait.
 * INVALID_STATE covers operations disallowed by the current lifecycle.
 * File and Configuration have separate result types with positive outcomes.
 */
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
