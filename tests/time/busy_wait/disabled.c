#include <crux/time.h>
#include <stdio.h>
int main(void)
{
    if (crux_busy_wait(crux_duration_from_ns(-1)) != CRUX_ERR_INVALID_ARGUMENT ||
        crux_busy_wait(crux_duration_from_ns(0)) != CRUX_OK ||
        crux_busy_wait(crux_duration_from_ns(INT64_MAX)) != CRUX_ERR_NOT_SUPPORTED) {
        return 1;
    }
    puts("disabled Time Busy-wait: no optional native dependency");
    return 0;
}
