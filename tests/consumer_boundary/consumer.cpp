#include <crux/status.h>
#include <crux/time.h>
#include <crux/time_calendar.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_same<crux_time_instant_t, std::int64_t>::value, "Time representation");

int main() {
    crux_time_instant_t now = 0;
    crux_time_utc_t utc;
    return crux_time_init() == CRUX_OK && crux_time_set(CRUX_TIME_MIN_UNIX_SECONDS) == CRUX_OK &&
                   crux_time_get(&now) == CRUX_OK && now == CRUX_TIME_MIN_UNIX_SECONDS &&
                   crux_time_to_utc(now, &utc) == CRUX_OK && utc.year == 2000 &&
                   crux_time_get(nullptr) == CRUX_ERR_INVALID_ARGUMENT
               ? 0
               : 1;
}
