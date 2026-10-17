#include <crux/time.h>

#include "time_port.h"

#include <stdbool.h>
#include <stddef.h>

static bool g_initialised;
static bool g_valid;

crux_status_t crux_time_init(void) {
    crux_status_t status;

    if (g_initialised) {
        return CRUX_OK;
    }

    status = crux_time_port_init();
    if (status != CRUX_OK) {
        return status;
    }

    g_initialised = true;
    g_valid = false;
    return CRUX_OK;
}

crux_status_t crux_time_get(crux_time_instant_t *out) {
    crux_time_instant_t value;
    crux_status_t status;

    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!g_initialised) {
        return CRUX_ERR_NOT_READY;
    }
    if (!g_valid) {
        return CRUX_ERR_NO_DATA;
    }

    status = crux_time_port_read(&value);
    if (status == CRUX_ERR_NO_DATA) {
        g_valid = false;
        return status;
    }
    if (status != CRUX_OK) {
        return status;
    }
    if (value < CRUX_TIME_MIN_UNIX_SECONDS || value >= CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE) {
        return CRUX_ERR_OUT_OF_RANGE;
    }

    *out = value;
    return CRUX_OK;
}

crux_status_t crux_time_set(crux_time_instant_t value) {
    crux_status_t status;

    if (value < CRUX_TIME_MIN_UNIX_SECONDS || value >= CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    if (!g_initialised) {
        return CRUX_ERR_NOT_READY;
    }

    status = crux_time_port_write(value);
    if (status != CRUX_OK) {
        g_valid = false;
        return status;
    }

    g_valid = true;
    return CRUX_OK;
}

#ifdef CRUX_TIME_TESTING
void crux_time_test_reset(void) {
    g_initialised = false;
    g_valid = false;
}
#endif
