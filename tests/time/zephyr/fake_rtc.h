#ifndef CRUX_TEST_FAKE_RTC_H
#define CRUX_TEST_FAKE_RTC_H
#include <zephyr/drivers/rtc.h>
void fake_rtc_errors(int read_error, int write_error);
void fake_rtc_value(struct rtc_time value);
#endif
