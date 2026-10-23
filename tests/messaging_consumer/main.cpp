#include <crux/monotonic.h>
#include <crux/queue.h>
#include <cstdio>
extern "C" int messaging_runtime_run(void);
int main() {
    int64_t now = -1;
    crux_queue_t queue{};
    if (crux_monotonic_now(&now) != CRUX_OK || now < 0 ||
        crux_monotonic_sleep_until(now) != CRUX_OK ||
        crux_queue_create(1, sizeof(now), &queue) != CRUX_OK ||
        crux_queue_send(queue, &now, sizeof(now), 0) != CRUX_OK)
        return 1;
    int64_t received = -1;
    if (crux_queue_receive(queue, &received, sizeof(received), 0) != CRUX_OK || received != now ||
        crux_queue_destroy(&queue) != CRUX_OK)
        return 2;
    int result = messaging_runtime_run();
    if (result == 0)
        std::puts("MESSAGING_CONSUMER passed C17/C++17 public-target sessions");
    return result;
}
