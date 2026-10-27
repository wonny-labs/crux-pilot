#include <crux/time.h>
#include <stdio.h>

int main(void)
{
    crux_monotonic_time_t before, after;
    const crux_duration_t duration = crux_duration_from_ms(1);
    if (crux_monotonic_now(&before) != CRUX_OK || crux_busy_wait(duration) != CRUX_OK ||
        crux_monotonic_now(&after) != CRUX_OK)
        return 1;
    if (crux_duration_to_ns(crux_monotonic_diff(after, before)) < crux_duration_to_ns(duration))
        return 1;
    puts("Linux native bounded CPU wait: reached requested monotonic interval");
    return 0;
}
