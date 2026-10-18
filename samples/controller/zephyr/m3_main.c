#include "controller.h"
#include "controller_health.h"
#include "device_input.h"

#include <crux/time.h>
#include <errno.h>
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#if defined(CONFIG_CRUX_M3_TEST_COMMANDS)
#include <zephyr/shell/shell.h>
#endif

K_THREAD_STACK_DEFINE(work_stack, 4096);
static struct k_thread work_thread;
static struct k_spinlock snapshot_lock;
static atomic_t arm_request;
static atomic_t armed;
static atomic_t stall_request;

struct work_snapshot {
    int64_t completed_ms;
    uint32_t sequence;
    int time_status;
    int device_status;
    int pressed;
    bool time_ready;
    bool ok;
};

static struct work_snapshot snapshot = {.completed_ms = -1};

static struct work_snapshot read_snapshot(void) {
    k_spinlock_key_t key = k_spin_lock(&snapshot_lock);
    struct work_snapshot value = snapshot;
    k_spin_unlock(&snapshot_lock, key);
    return value;
}

static void publish_snapshot(struct work_snapshot value) {
    k_spinlock_key_t key = k_spin_lock(&snapshot_lock);
    snapshot = value;
    k_spin_unlock(&snapshot_lock, key);
}

static void run_work(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    int time_init = crux_time_init();
    int device_init = controller_device_init();
    printk("M3_INIT time=%d device=%d stimulus=unqualified network=held\n", time_init, device_init);
    controller_schedule_t schedule = {.deadline_ms = k_uptime_get(), .period_ms = 100};
    for (uint32_t cycle = 0;; ++cycle) {
        (void)k_sleep(K_TIMEOUT_ABS_MS(schedule.deadline_ms));
        if (atomic_get(&stall_request) != 0) {
            printk("M3_FAULT_STALL monotonic_ms=%lld sequence=%u\n", (long long)k_uptime_get(),
                   cycle);
            for (;;) {
                (void)k_sleep(K_FOREVER);
            }
        }
        controller_reading_t reading = controller_observe(cycle);
        int pressed = -1;
        int device_status = device_init == 0 ? controller_device_read(&pressed) : device_init;
        struct work_snapshot value = {
            .completed_ms = k_uptime_get(),
            .sequence = cycle + 1,
            .time_status = reading.status,
            .device_status = device_status,
            .pressed = pressed,
            .time_ready = time_init == CRUX_OK,
            .ok = time_init == CRUX_OK && device_status == 0 &&
                  (reading.status == CRUX_OK || reading.status == CRUX_ERR_NO_DATA)};
        publish_snapshot(value);
        if (cycle % 10 == 0) {
            printk("M3_WORK sequence=%u monotonic_ms=%lld time=%d pressed=%d device=%d\n",
                   value.sequence, (long long)value.completed_ms, value.time_status, value.pressed,
                   value.device_status);
        }
        if (controller_schedule_advance(&schedule, k_uptime_get()) != CRUX_OK) {
            value.ok = false;
            publish_snapshot(value);
            printk("M3_WORK_FAILED schedule\n");
            return;
        }
    }
}

static void report_reset_cause(void) {
    uint32_t supported = 0;
    uint32_t cause = 0;
    uint32_t remaining = 0;
    int support_result = hwinfo_get_supported_reset_cause(&supported);
    int cause_result = hwinfo_get_reset_cause(&cause);
    int clear_result = hwinfo_clear_reset_cause();
    int remaining_result = hwinfo_get_reset_cause(&remaining);
    printk("M3_RESET support_status=%d supported=0x%x cause_status=%d cause=0x%x "
           "clear_status=%d remaining_status=%d remaining=0x%x\n",
           support_result, supported, cause_result, cause, clear_result, remaining_result,
           remaining);
}

#if defined(CONFIG_CRUX_M3_TEST_COMMANDS)
static int request_arm(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (atomic_get(&armed) != 0) {
        shell_error(shell, "M3_ARM_REJECTED already_armed");
        return -EALREADY;
    }
    atomic_set(&arm_request, 1);
    shell_print(shell, "M3_ARM_REQUESTED");
    return 0;
}

static int request_stall(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (atomic_get(&armed) == 0 || !atomic_cas(&stall_request, 0, 1)) {
        shell_error(shell, "M3_STALL_REJECTED not_armed_or_repeated");
        return -EINVAL;
    }
    shell_print(shell, "M3_STALL_REQUESTED");
    return 0;
}

static int report_status(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    shell_print(shell, "M3_PENDING non_uart=unqualified network=held armed=%ld",
                (long)atomic_get(&armed));
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(m3_commands,
                               SHELL_CMD(arm, NULL, "Arm the reviewed watchdog probe", request_arm),
                               SHELL_CMD(stall, NULL, "Stall critical work once", request_stall),
                               SHELL_CMD(status, NULL, "Report incomplete M3 scope", report_status),
                               SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(m3, &m3_commands, "Development-only independent M3 probe", NULL);
#endif

static int arm_watchdog(const struct device *watchdog) {
    if (watchdog == NULL || !device_is_ready(watchdog)) {
        return -ENODEV;
    }
    const struct wdt_timeout_cfg timeout = {
        .window = {.min = 0, .max = 2000}, .callback = NULL, .flags = WDT_FLAG_RESET_SOC};
    int channel = wdt_install_timeout(watchdog, &timeout);
    if (channel < 0) {
        return channel;
    }
    if (channel != 0) {
        return -EINVAL;
    }
    return wdt_setup(watchdog, WDT_OPT_PAUSE_HALTED_BY_DBG);
}

int main(void) {
    printk("M3_BOOT source=%s profile=independent-health commands=%d fault_default=off\n",
           CRUX_M3_SOURCE, IS_ENABLED(CONFIG_CRUX_M3_TEST_COMMANDS));
    printk("M3_PENDING non_uart=unqualified network=held readiness=not_ready\n");
    report_reset_cause();
    k_tid_t worker = k_thread_create(&work_thread, work_stack, K_THREAD_STACK_SIZEOF(work_stack),
                                     run_work, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    (void)k_thread_name_set(worker, "m3-critical");
    const struct device *watchdog = DEVICE_DT_GET_OR_NULL(DT_ALIAS(watchdog0));
    controller_health_t health;
    (void)controller_health_init(&health, 500);
    int64_t continuous_health_ms = -1;
    int64_t next_report = 0;
    bool arm_attempted = false;
    bool stale_reported = false;
    for (;;) {
        struct work_snapshot value = read_snapshot();
        int64_t now = k_uptime_get();
        bool healthy =
            controller_health_observe(&health, now, value.completed_ms, value.sequence, value.ok);
        if (!healthy) {
            continuous_health_ms = -1;
        } else if (continuous_health_ms < 0) {
            continuous_health_ms = now;
        }
        if (atomic_cas(&arm_request, 1, 0)) {
            if (!healthy || continuous_health_ms < 0 || now - continuous_health_ms < 1000) {
                printk("M3_ARM_REJECTED not_healthy\n");
            } else if (arm_attempted) {
                printk("M3_ARM_REJECTED attempted\n");
            } else {
                arm_attempted = true;
                int result = arm_watchdog(watchdog);
                if (result != 0) {
                    printk("M3_WDT_FAILED setup=%d\n", result);
                } else {
                    atomic_set(&armed, 1);
                    printk("M3_WDT_ARMED timeout_ms=2000 debug_pause=1 freshness_ms=500\n");
                }
            }
        }
        if (atomic_get(&armed) != 0 && healthy) {
            int result = wdt_feed(watchdog, 0);
            if (result != 0) {
                printk("M3_WDT_FAILED feed=%d\n", result);
                return 1;
            }
            printk("M3_WDT_FEED monotonic_ms=%lld sequence=%u completed_ms=%lld\n",
                   (long long)k_uptime_get(), value.sequence, (long long)value.completed_ms);
        } else if (atomic_get(&armed) != 0 && !stale_reported) {
            stale_reported = true;
            printk("M3_HEALTH_STALE monotonic_ms=%lld sequence=%u\n", (long long)now,
                   value.sequence);
        }
        if (now >= next_report) {
            size_t worker_unused = 0;
            size_t main_unused = 0;
            int worker_result = k_thread_stack_space_get(worker, &worker_unused);
            int main_result = k_thread_stack_space_get(k_current_get(), &main_unused);
            printk("M3_OBSERVER_ALIVE monotonic_ms=%lld healthy=%d sequence=%u readiness=%d\n",
                   (long long)now, healthy, value.sequence,
                   controller_readiness(value.time_ready, value.device_status == 0, false, true));
            printk("M3_STACK worker_status=%d worker_object=%zu worker_usable=%zu "
                   "worker_unused=%zu main_status=%d main_usable=%zu main_unused=%zu\n",
                   worker_result, sizeof(work_stack), K_THREAD_STACK_SIZEOF(work_stack),
                   worker_unused, main_result, k_current_get()->stack_info.size, main_unused);
            if (now >= 2000 && atomic_get(&armed) == 0) {
                thread_analyzer_run();
            }
            next_report = now + 1000;
        }
        (void)k_sleep(K_MSEC(100));
    }
}
