#include <crux/queue.h>
#include <crux/thread.h>
#include <crux/time.h>
#include <cstdio>
extern "C" int messaging_runtime_run(void);
int main() {
    crux_monotonic_time_t now = {0};
    crux_queue_t queue{};
    if (crux_time_get_monotonic(&now) != CRUX_OK || crux_thread_sleep_until(now) != CRUX_OK ||
        crux_queue_create(1, sizeof(now), &queue) != CRUX_OK ||
        crux_queue_send(queue, &now, sizeof(now), 0) != CRUX_OK)
        return 1;
    crux_monotonic_time_t received = {0};
    if (crux_queue_receive(queue, &received, sizeof(received), 0) != CRUX_OK ||
        !crux_monotonic_is_at_or_before(received, now) ||
        !crux_monotonic_is_at_or_before(now, received) || crux_queue_destroy(&queue) != CRUX_OK)
        return 2;
    int result = messaging_runtime_run();
    if (result == 0)
        std::puts("MESSAGING_CONSUMER passed C17/C++17 public-target sessions");
    return result;
}
