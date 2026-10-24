#include <crux/monotonic.h>
#include <cstdint>
#include <cstdio>

int main() {
    int64_t before = -1;
    if (crux_monotonic_now(&before) != CRUX_OK || before < 0)
        return 1;
    for (int i = 0; i < 100; ++i) {
        int64_t current = -1;
        if (crux_monotonic_now(&current) != CRUX_OK || current < before)
            return 2;
        before = current;
    }
    const int64_t deadline = before + 20;
    if (crux_monotonic_sleep_until(deadline) != CRUX_OK)
        return 3;
    int64_t after = -1;
    if (crux_monotonic_now(&after) != CRUX_OK || after < deadline)
        return 4;
    if (crux_monotonic_sleep_until(before) != CRUX_OK)
        return 5;
    std::printf("real Host absolute wait: %lld ms\n", static_cast<long long>(after - before));
    return 0;
}
