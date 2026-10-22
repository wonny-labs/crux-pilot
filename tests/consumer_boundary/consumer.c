#include <crux/status.h>
#include <crux/time.h>
#include <crux/time_calendar.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    crux_time_instant_t now = 0;
    crux_time_utc_t utc;
    const bool valid = crux_time_init() == CRUX_OK &&
                       crux_time_set(CRUX_TIME_MIN_UNIX_SECONDS) == CRUX_OK &&
                       crux_time_get(&now) == CRUX_OK && now == CRUX_TIME_MIN_UNIX_SECONDS;
    return valid && crux_time_to_utc(now, &utc) == CRUX_OK && utc.year == 2000 &&
                   crux_time_get(NULL) == CRUX_ERR_INVALID_ARGUMENT
               ? 0
               : 1;
}
