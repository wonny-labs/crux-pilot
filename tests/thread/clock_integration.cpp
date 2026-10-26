#include <crux/thread.h>
#include <crux/time.h>
#include <cstdint>
#include <cstdio>

int main()
{
    crux_monotonic_time_t before = {0};
    if (crux_time_get_monotonic(&before) != CRUX_OK || before.private_ns < 0)
        return 1;
    for (int i = 0; i < 100; ++i) {
        crux_monotonic_time_t current = {0};
        if (crux_time_get_monotonic(&current) != CRUX_OK ||
            crux_monotonic_is_before(current, before))
            return 2;
        before = current;
    }
    crux_monotonic_time_t deadline;
    if (crux_time_make_deadline_after(crux_duration_from_ms(20), &deadline) != CRUX_OK)
        return 3;
    if (crux_thread_sleep_until(deadline) != CRUX_OK)
        return 3;
    crux_monotonic_time_t after = {0};
    if (crux_time_get_monotonic(&after) != CRUX_OK || crux_monotonic_is_before(after, deadline))
        return 4;
    if (crux_thread_sleep_until(before) != CRUX_OK)
        return 5;
    std::printf(
        "real Host absolute wait: %lld ms\n",
        static_cast<long long>(crux_duration_to_ms_floor(crux_monotonic_diff(after, before))));
    return 0;
}
