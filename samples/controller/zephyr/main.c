#include "controller.h"

#include <crux/time_authority.h>
#include <zephyr/kernel.h>

K_THREAD_STACK_DEFINE(controller_stack, 4096);
static struct k_thread controller_thread;
static int scenario_result;

static void run_controller(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    scenario_result = 1;
    crux_status_t status = crux_time_init();
    printk("controller composition=zephyr init=%d\n", status);
    if (status != CRUX_OK) {
        return;
    }
    controller_schedule_t schedule = {.deadline_ms = k_uptime_get(), .period_ms = 100};
    for (uint32_t cycle = 0; cycle < 20; ++cycle) {
        (void)k_sleep(K_TIMEOUT_ABS_MS(schedule.deadline_ms));
        int64_t released = k_uptime_get();
        if (released < schedule.deadline_ms) {
            return;
        }
        if (cycle == 2 || cycle == 6) {
            const crux_realtime_t value = {cycle == 2 ? INT64_C(1792238400) : INT64_C(1792234800),
                                           0};
            status = crux_time_set_realtime(&value, CRUX_TIME_SOURCE_MANUAL, false);
            if (status != CRUX_OK) {
                printk("controller set_error=%d\n", status);
                return;
            }
        }
        controller_reading_t reading = controller_observe(cycle);
        printk("cycle=%u status=%d valid=%d ", reading.cycle, reading.status,
               reading.status == CRUX_OK);
        if (reading.status == CRUX_OK) {
            printk("utc=%lld ", (long long)reading.utc_seconds);
        } else {
            printk("utc=unavailable ");
        }
        printk("monotonic_ms=%lld lateness_ms=%lld\n", (long long)released,
               (long long)(released - schedule.deadline_ms));
        if (controller_schedule_advance(&schedule, k_uptime_get()) != CRUX_OK) {
            return;
        }
        printk("schedule skipped=%llu\n", (unsigned long long)schedule.skipped);
    }
    scenario_result = 0;
}

int main(void) {
    k_tid_t worker = k_thread_create(&controller_thread, controller_stack,
                                     K_THREAD_STACK_SIZEOF(controller_stack), run_controller, NULL,
                                     NULL, NULL, 5, 0, K_NO_WAIT);
    if (k_thread_join(worker, K_SECONDS(10)) != 0) {
        printk("CONTROLLER_FAILED join\n");
        return 1;
    }
    size_t unused = 0;
    if (k_thread_stack_space_get(worker, &unused) != 0 || scenario_result != 0) {
        printk("CONTROLLER_FAILED scenario_or_stack\n");
        return 1;
    }
    printk("controller stack_bytes=%zu unused_bytes=%zu priority=5 heap_bytes=%d\n",
           sizeof(controller_stack), unused, CONFIG_HEAP_MEM_POOL_SIZE);
    printk("CONTROLLER_COMPLETE cycles=20\n");
    return 0;
}
