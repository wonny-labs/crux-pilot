#include <crux/status.h>
#include <crux/time_authority.h>
#include <crux/time_calendar.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    crux_realtime_t now = {0, 0};
    const crux_realtime_t start = {CRUX_TIME_MIN_UNIX_SECONDS, 0};
    crux_time_utc_t utc;
    const bool valid = crux_time_init() == CRUX_OK &&
                       crux_time_set_realtime(&start, CRUX_TIME_SOURCE_MANUAL, false) == CRUX_OK &&
                       crux_time_get_realtime(&now) == CRUX_OK &&
                       now.seconds == CRUX_TIME_MIN_UNIX_SECONDS;
    return valid && crux_time_to_utc(now, &utc) == CRUX_OK && utc.year == 2000 &&
                   crux_time_get_realtime(NULL) == CRUX_ERR_INVALID_ARGUMENT
               ? 0
               : 1;
}
