#include "time_port.h"
#include "time_port_zephyr_internal.h"

#include <errno.h>
#include <stdbool.h>

#include <crux/time_calendar.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/sys/util.h>

crux_status_t crux_time_zephyr_status_from_rtc_error(int error) {
    switch (error) {
    case 0:
        return CRUX_OK;
    case -ENODATA:
        return CRUX_ERR_NO_DATA;
    case -EINVAL:
        return CRUX_ERR_INVALID_ARGUMENT;
    case -ERANGE:
        return CRUX_ERR_OUT_OF_RANGE;
    case -ENODEV:
    case -ENXIO:
        return CRUX_ERR_NOT_READY;
    default:
        return CRUX_ERR_IO;
    }
}

#if DT_HAS_ALIAS(rtc)
#if DT_NODE_HAS_STATUS(DT_ALIAS(rtc), okay)
#define CRUX_TIME_RTC_AVAILABLE 1
static const struct device *const crux_time_rtc = DEVICE_DT_GET(DT_ALIAS(rtc));
#endif
#endif

crux_status_t crux_time_port_init(void) {
#if defined(CRUX_TIME_RTC_AVAILABLE)
    return device_is_ready(crux_time_rtc) ? CRUX_OK : CRUX_ERR_NOT_READY;
#else
    return CRUX_ERR_NOT_READY;
#endif
}

crux_status_t crux_time_port_read(crux_time_instant_t *out) {
#if defined(CRUX_TIME_RTC_AVAILABLE)
    struct rtc_time native_time = {0};
    crux_time_utc_t utc_time;
    crux_time_instant_t instant;
    crux_status_t status;
    int result;

    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!device_is_ready(crux_time_rtc)) {
        return CRUX_ERR_NOT_READY;
    }

    result = rtc_get_time(crux_time_rtc, &native_time);
    status = crux_time_zephyr_status_from_rtc_error(result);
    if (status != CRUX_OK) {
        return status;
    }
    if (native_time.tm_mon < 0 || native_time.tm_mon > 11) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (native_time.tm_year < 100 || native_time.tm_year > 199) {
        return CRUX_ERR_OUT_OF_RANGE;
    }

    utc_time.year = native_time.tm_year + 1900;
    utc_time.month = native_time.tm_mon + 1;
    utc_time.day = native_time.tm_mday;
    utc_time.hour = native_time.tm_hour;
    utc_time.minute = native_time.tm_min;
    utc_time.second = native_time.tm_sec;
    status = crux_time_from_utc(&utc_time, &instant);
    if (status != CRUX_OK) {
        return status;
    }

    *out = instant;
    return CRUX_OK;
#else
    ARG_UNUSED(out);
    return CRUX_ERR_NOT_READY;
#endif
}

crux_status_t crux_time_port_write(crux_time_instant_t value) {
#if defined(CRUX_TIME_RTC_AVAILABLE)
    struct rtc_time native_time = {0};
    crux_time_utc_t utc_time;
    crux_status_t status;
    int result;

    if (!device_is_ready(crux_time_rtc)) {
        return CRUX_ERR_NOT_READY;
    }

    status = crux_time_to_utc(value, &utc_time);
    if (status != CRUX_OK) {
        return status;
    }

    native_time.tm_year = utc_time.year - 1900;
    native_time.tm_mon = utc_time.month - 1;
    native_time.tm_mday = utc_time.day;
    native_time.tm_hour = utc_time.hour;
    native_time.tm_min = utc_time.minute;
    native_time.tm_sec = utc_time.second;
    native_time.tm_isdst = -1;
    native_time.tm_nsec = 0;

    result = rtc_set_time(crux_time_rtc, &native_time);
    return crux_time_zephyr_status_from_rtc_error(result);
#else
    ARG_UNUSED(value);
    return CRUX_ERR_NOT_READY;
#endif
}
