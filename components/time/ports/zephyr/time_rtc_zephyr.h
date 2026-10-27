#ifndef CRUX_TIME_RTC_ZEPHYR_H
#define CRUX_TIME_RTC_ZEPHYR_H

#include <crux/time.h>

/* Trusted native composition only, enabled by CONFIG_CRUX_TIME_RTC. These
 * operations never change Time validity/source/system realtime. Thread context;
 * callers serialize RTC driver operations. Read preserves output on failure.
 * Current qualified adapters store whole seconds only; nonzero fractional
 * storage is NOT_SUPPORTED. Load/adoption policy belongs to the application. */
crux_status_t crux_time_zephyr_rtc_read(crux_realtime_t *out);
crux_status_t crux_time_zephyr_rtc_store(const crux_realtime_t *time);

#endif
