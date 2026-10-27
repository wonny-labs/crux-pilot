#include "realtime_port.h"
#include <crux/time_authority.h>
#include <stddef.h>

/* All state and native realtime accesses share the provider's native lock. */
static bool g_initialised;
static bool g_valid;
static bool g_synchronized;
static crux_realtime_source_t g_last_source;

static crux_status_t validate_value(const crux_realtime_t *value)
{
    if (value == NULL || value->nanoseconds >= UINT32_C(1000000000)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (value->seconds < CRUX_TIME_MIN_UNIX_SECONDS ||
        value->seconds >= CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    return CRUX_OK;
}

static void invalidate(void)
{
    g_valid = false;
    g_synchronized = false;
}

/* Caller holds the lock. Dirty provider outputs never reach public storage. */
static crux_status_t read_native(crux_realtime_t *out)
{
    crux_realtime_t value;
    crux_status_t status = crux_realtime_impl_read(&value);
    if (status == CRUX_OK) {
        status = validate_value(&value);
        if (status == CRUX_ERR_INVALID_ARGUMENT) {
            status = CRUX_ERR_IO; /* Malformed native representation. */
        }
    }
    if (status == CRUX_ERR_NO_DATA || status == CRUX_ERR_OUT_OF_RANGE) {
        invalidate();
    }
    if (status == CRUX_OK) {
        *out = value;
    }
    return status;
}

crux_status_t crux_realtime_init(void)
{
    crux_status_t status = crux_realtime_impl_lock();
    if (status != CRUX_OK)
        return status;
    if (!g_initialised) {
        status = crux_realtime_impl_init();
        if (status == CRUX_OK) {
            g_initialised = true;
            invalidate();
            g_last_source = CRUX_REALTIME_SOURCE_NONE;
        }
    }
    crux_realtime_impl_unlock();
    return status;
}

crux_status_t crux_realtime_now(crux_realtime_t *out)
{
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    crux_status_t status = crux_realtime_impl_lock();
    if (status != CRUX_OK)
        return status;
    if (!g_initialised)
        status = CRUX_ERR_NOT_READY;
    else if (!g_valid)
        status = CRUX_ERR_NO_DATA;
    else
        status = read_native(out);
    crux_realtime_impl_unlock();
    return status;
}

crux_status_t crux_realtime_get_snapshot(crux_realtime_snapshot_t *out)
{
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    crux_status_t status = crux_realtime_impl_lock();
    if (status != CRUX_OK)
        return status;
    if (!g_initialised) {
        status = CRUX_ERR_NOT_READY;
    } else {
        crux_realtime_snapshot_t value = {0};
        if (g_valid) {
            status = read_native(&value.timestamp);
            if (status == CRUX_ERR_NO_DATA)
                status = CRUX_OK;
        }
        if (status == CRUX_OK) {
            value.last_source = g_last_source;
            value.is_valid = g_valid;
            value.is_synchronized = g_synchronized;
            *out = value;
        }
    }
    crux_realtime_impl_unlock();
    return status;
}

crux_status_t crux_realtime_adopt(void)
{
    crux_status_t status = crux_realtime_impl_lock();
    if (status != CRUX_OK)
        return status;
    if (!g_initialised) {
        status = CRUX_ERR_NOT_READY;
    } else {
        crux_realtime_t value;
        status = read_native(&value);
        if (status == CRUX_OK) {
            g_valid = true;
            g_synchronized = false;
            g_last_source = CRUX_REALTIME_SOURCE_UNKNOWN;
        }
    }
    crux_realtime_impl_unlock();
    return status;
}

crux_status_t crux_realtime_set(const crux_realtime_t *time, crux_realtime_source_t source,
                                bool actively_synchronized)
{
    crux_status_t status = validate_value(time);
    if (status != CRUX_OK)
        return status;
    if ((source != CRUX_REALTIME_SOURCE_RTC && source != CRUX_REALTIME_SOURCE_MANUAL &&
         source != CRUX_REALTIME_SOURCE_EXTERNAL) ||
        (actively_synchronized && source != CRUX_REALTIME_SOURCE_EXTERNAL)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    status = crux_realtime_impl_lock();
    if (status != CRUX_OK)
        return status;
    if (!g_initialised) {
        status = CRUX_ERR_NOT_READY;
    } else {
        status = crux_realtime_impl_write(time);
        if (status == CRUX_OK) {
            g_valid = true;
            g_synchronized = actively_synchronized;
            g_last_source = source;
        } else {
            invalidate();
        }
    }
    crux_realtime_impl_unlock();
    return status;
}

crux_status_t crux_realtime_set_synchronized(bool actively_synchronized)
{
    crux_status_t status = crux_realtime_impl_lock();
    if (status != CRUX_OK)
        return status;
    if (!g_initialised)
        status = CRUX_ERR_NOT_READY;
    else if (!g_valid)
        status = CRUX_ERR_NO_DATA;
    else if (actively_synchronized && g_last_source != CRUX_REALTIME_SOURCE_EXTERNAL) {
        status = CRUX_ERR_INVALID_STATE;
    } else
        g_synchronized = actively_synchronized;
    crux_realtime_impl_unlock();
    return status;
}

crux_status_t crux_realtime_invalidate(void)
{
    crux_status_t status = crux_realtime_impl_lock();
    if (status != CRUX_OK)
        return status;
    if (!g_initialised)
        status = CRUX_ERR_NOT_READY;
    else
        invalidate();
    crux_realtime_impl_unlock();
    return status;
}

#ifdef CRUX_TIME_TESTING
void crux_time_test_reset(void)
{
    if (crux_realtime_impl_lock() == CRUX_OK) {
        g_initialised = false;
        invalidate();
        g_last_source = CRUX_REALTIME_SOURCE_NONE;
        crux_realtime_impl_unlock();
    }
}
#endif
