#include "fake_rtc.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/* Test-only native driver. The real adapter still uses Zephyr's RTC API. */
static int read_error, write_error;
static struct rtc_time stored = {.tm_year = 123, .tm_mon = 0, .tm_mday = 1};
void fake_rtc_errors(int read_status, int write_status)
{
    read_error = read_status;
    write_error = write_status;
}
void fake_rtc_value(struct rtc_time value)
{
    stored = value;
}
static int read_time(const struct device *dev, struct rtc_time *out)
{
    (void)dev;
    *out = stored;
    return read_error;
}
static int write_time(const struct device *dev, const struct rtc_time *value)
{
    (void)dev;
    stored = *value;
    return write_error;
}
static DEVICE_API(rtc, fake_api) = {.get_time = read_time, .set_time = write_time};
DEVICE_DT_DEFINE(DT_ALIAS(rtc), NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_RTC_INIT_PRIORITY,
                 &fake_api);
