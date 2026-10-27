#include <crux/status.h>
#include <crux/time_authority.h>
#include <crux/time_calendar.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(std::is_same<decltype(crux_realtime_t::seconds), std::int64_t>::value,
              "Time representation");

int main()
{
    crux_realtime_t now = {0, 0};
    const crux_realtime_t start = {CRUX_TIME_MIN_UNIX_SECONDS, 0};
    crux_datetime_t utc;
    return crux_realtime_init() == CRUX_OK &&
                   crux_realtime_set(&start, CRUX_REALTIME_SOURCE_MANUAL, false) == CRUX_OK &&
                   crux_realtime_now(&now) == CRUX_OK &&
                   now.seconds == CRUX_TIME_MIN_UNIX_SECONDS &&
                   crux_realtime_to_utc(now, &utc) == CRUX_OK && utc.year == 2000 &&
                   crux_realtime_now(nullptr) == CRUX_ERR_INVALID_ARGUMENT
               ? 0
               : 1;
}
