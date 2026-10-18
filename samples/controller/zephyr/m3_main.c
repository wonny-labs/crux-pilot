#include "controller.h"
#include "controller_health.h"
#include "device_input.h"

#include <crux/time.h>
#include <zephyr/kernel.h>

#include <errno.h>
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/atomic.h>
#if defined(CONFIG_CRUX_M3_NATIVE_OUTPUT)
#include "native_logger.h"
#include <zephyr/logging/log_ctrl.h>
#endif
#if defined(CONFIG_CRUX_M3_INTEGRATED)
#include "network_client.h"
#include <zephyr/net/net_if.h>
#endif
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

#if defined(CONFIG_CRUX_M3_INTEGRATED)
K_THREAD_STACK_DEFINE(network_stack, 4096);
static struct k_thread network_thread;
static struct k_spinlock network_lock;
static atomic_t network_window_request;
static atomic_t network_stall_request;
static int64_t network_stall_deadline;
static int64_t network_stall_requested;
static int64_t work_stall_deadline;
static int64_t work_stall_requested;

struct network_snapshot {
    int64_t completed_ms;
    int64_t window_end_ms;
    uint32_t sequence;
    int status;
    bool active;
    bool ok;
};

static struct network_snapshot network_snapshot = {.completed_ms = -1, .window_end_ms = -1};

static struct network_snapshot read_network(void) {
    k_spinlock_key_t key = k_spin_lock(&network_lock);
    struct network_snapshot value = network_snapshot;
    k_spin_unlock(&network_lock, key);
    return value;
}

static void publish_network(struct network_snapshot value) {
    k_spinlock_key_t key = k_spin_lock(&network_lock);
    network_snapshot = value;
    k_spin_unlock(&network_lock, key);
}

static bool fault_budget(int64_t now, int64_t request_deadline) {
    struct network_snapshot value = read_network();
    return value.active && controller_m3_fault_allowed(now, request_deadline, value.window_end_ms);
}

static void run_network(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    struct network_snapshot value = {.completed_ms = -1, .window_end_ms = -1};
    value.status = controller_network_init();
    publish_network(value);
    printk("M3_NET_OWNER_INIT result=%d automatic_window=0\n", value.status);
    if (value.status != 0) {
        atomic_set(&network_window_request, 3);
        return;
    }
    int64_t next_idle_report = k_uptime_get() + 1000;
    while (!atomic_cas(&network_window_request, 1, 2)) {
        if (k_uptime_get() >= next_idle_report) {
            controller_network_report();
            next_idle_report = k_uptime_get() + 1000;
        }
        k_sleep(K_MSEC(100));
    }
    value.window_end_ms = k_uptime_get() + 60000;
    value.active = true;
    publish_network(value);
    printk("M3_NET_WINDOW started_ms=%lld end_ms=%lld\n", (long long)k_uptime_get(),
           (long long)value.window_end_ms);
    while (k_uptime_get() < value.window_end_ms) {
        int64_t now = k_uptime_get();
        if (atomic_cas(&network_stall_request, 1, 2)) {
            k_spinlock_key_t key = k_spin_lock(&network_lock);
            int64_t deadline = network_stall_deadline;
            int64_t requested = network_stall_requested;
            k_spin_unlock(&network_lock, key);
            if (!fault_budget(now, deadline)) {
                printk("M3_NET_STALL_REJECTED checked_ms=%lld requested_ms=%lld deadline_ms=%lld "
                       "window_end_ms=%lld remaining_ms=%lld reason=%s\n",
                       (long long)now, (long long)requested, (long long)deadline,
                       (long long)value.window_end_ms, (long long)(value.window_end_ms - now),
                       now >= deadline ? "expired" : "window_budget");
            } else {
                printk("M3_NET_FAULT_STALL monotonic_ms=%lld sequence=%u completed_ms=%lld "
                       "requested_ms=%lld deadline_ms=%lld window_end_ms=%lld\n",
                       (long long)now, value.sequence, (long long)value.completed_ms,
                       (long long)requested, (long long)deadline, (long long)value.window_end_ms);
                for (;;) {
                    k_sleep(K_FOREVER);
                }
            }
        }
        if (value.window_end_ms - now < 14000) {
            k_sleep(K_MSEC(100));
            continue;
        }
        value.status = controller_network_pair(1, value.sequence + 1, now + 14000);
        if (value.status != 0) {
            value.ok = false;
            break;
        }
        value.completed_ms = k_uptime_get();
        value.sequence++;
        value.ok = true;
        publish_network(value);
        printk("M3_NET_WORK sequence=%u completed_ms=%lld result=0\n", value.sequence,
               (long long)value.completed_ms);
        /* The gap starts after completion; never accumulate catch-up releases. */
        k_sleep(K_MSEC(1000));
    }
    value.active = false;
    value.ok = false;
    publish_network(value);
    atomic_set(&network_window_request, 3);
    printk("M3_NET_WINDOW_STOP monotonic_ms=%lld result=%d\n", (long long)k_uptime_get(),
           value.status);
    /* Observe native return after the window without starting another transaction. */
    for (unsigned int report = 0; report < 3; ++report) {
        controller_network_report();
        k_sleep(K_MSEC(1000));
    }
}
#endif

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
#if defined(CONFIG_CRUX_M3_INTEGRATED)
    printk("M3_INIT time=%d device=%d stimulus=SW5 network=required\n", time_init, device_init);
#else
    printk("M3_INIT time=%d device=%d stimulus=unqualified network=held\n", time_init, device_init);
#endif
    controller_schedule_t schedule = {.deadline_ms = k_uptime_get(), .period_ms = 100};
    for (uint32_t cycle = 0;; ++cycle) {
        (void)k_sleep(K_TIMEOUT_ABS_MS(schedule.deadline_ms));
        if (atomic_get(&stall_request) == 1) {
#if defined(CONFIG_CRUX_M3_INTEGRATED)
            k_spinlock_key_t key = k_spin_lock(&network_lock);
            int64_t deadline = work_stall_deadline;
            int64_t requested = work_stall_requested;
            k_spin_unlock(&network_lock, key);
            int64_t checked = k_uptime_get();
            struct network_snapshot net = read_network();
            if (!fault_budget(checked, deadline)) {
                atomic_set(&stall_request, 2);
                printk("M3_STALL_REJECTED checked_ms=%lld requested_ms=%lld deadline_ms=%lld "
                       "window_end_ms=%lld remaining_ms=%lld reason=%s\n",
                       (long long)checked, (long long)requested, (long long)deadline,
                       (long long)net.window_end_ms, (long long)(net.window_end_ms - checked),
                       checked >= deadline ? "expired" : "inactive_or_window_budget");
            } else
#endif
            {
#if defined(CONFIG_CRUX_M3_INTEGRATED)
                printk("M3_FAULT_STALL monotonic_ms=%lld sequence=%u requested_ms=%lld "
                       "deadline_ms=%lld window_end_ms=%lld\n",
                       (long long)checked, cycle, (long long)requested, (long long)deadline,
                       (long long)net.window_end_ms);
#else
                printk("M3_FAULT_STALL monotonic_ms=%lld sequence=%u\n", (long long)k_uptime_get(),
                       cycle);
#endif
                for (;;) {
                    (void)k_sleep(K_FOREVER);
                }
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
    ARG_UNUSED(shell);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (atomic_get(&armed) != 0) {
        printk("M3_ARM_REJECTED already_armed\n");
        return -EALREADY;
    }
    atomic_set(&arm_request, 1);
    printk("M3_ARM_REQUESTED\n");
    return 0;
}

static int request_stall(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(shell);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
#if defined(CONFIG_CRUX_M3_INTEGRATED)
    if (atomic_get(&armed) == 0 || !atomic_cas(&stall_request, 0, -1)) {
        printk("M3_STALL_REJECTED not_armed_or_repeated\n");
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&network_lock);
    int64_t requested = k_uptime_get();
    work_stall_requested = requested;
    work_stall_deadline = requested + 15000;
    k_spin_unlock(&network_lock, key);
    atomic_set(&stall_request, 1);
#else
    if (atomic_get(&armed) == 0 || !atomic_cas(&stall_request, 0, 1)) {
        printk("M3_STALL_REJECTED not_armed_or_repeated\n");
        return -EINVAL;
    }
#endif
#if defined(CONFIG_CRUX_M3_INTEGRATED)
    printk("M3_STALL_REQUESTED requested_ms=%lld deadline_ms=%lld\n", (long long)requested,
           (long long)(requested + 15000));
#else
    printk("M3_STALL_REQUESTED\n");
#endif
    return 0;
}

#if defined(CONFIG_CRUX_M3_INTEGRATED)
static int request_network(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(shell);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (!atomic_cas(&network_window_request, 0, 1)) {
        printk("M3_NET_WINDOW_REJECTED repeated\n");
        return -EALREADY;
    }
    printk("M3_NET_WINDOW_REQUESTED duration_ms=60000\n");
    return 0;
}

static int request_network_stall(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(shell);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    if (atomic_get(&armed) == 0 || !atomic_cas(&network_stall_request, 0, -1)) {
        printk("M3_NET_STALL_REJECTED not_armed_or_repeated\n");
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&network_lock);
    int64_t requested = k_uptime_get();
    network_stall_requested = requested;
    network_stall_deadline = requested + 15000;
    k_spin_unlock(&network_lock, key);
    atomic_set(&network_stall_request, 1);
    printk("M3_NET_STALL_REQUESTED requested_ms=%lld deadline_ms=%lld\n", (long long)requested,
           (long long)(requested + 15000));
    return 0;
}
#endif

static int report_status(const struct shell *shell, size_t argc, char **argv) {
    ARG_UNUSED(shell);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
#if defined(CONFIG_CRUX_M3_INTEGRATED)
    printk("M3_SCOPE non_uart=SW5 network=required armed=%ld\n",
#else
    printk("M3_PENDING non_uart=unqualified network=held armed=%ld\n",
#endif
           (long)atomic_get(&armed));
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    m3_commands, SHELL_CMD(arm, NULL, "Arm the reviewed watchdog probe", request_arm),
    SHELL_CMD(stall, NULL, "Stall critical work once", request_stall),
    SHELL_CMD(status, NULL, "Report M3 scope", report_status),
#if defined(CONFIG_CRUX_M3_INTEGRATED)
    SHELL_CMD(network, NULL, "Start one bounded network window", request_network),
    SHELL_CMD(stall_network, NULL, "Stall the network owner once", request_network_stall),
#endif
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

/* Private initial offset, not a recurring phase lock or latency guarantee. */
#define CRUX_M3_INITIAL_OBSERVER_PHASE_MS 50

int main(void) {
#if defined(CONFIG_CRUX_M3_INTEGRATED)
    printk("M3_BOOT source=%s profile=integrated commands=%d fault_default=off\n",
#else
    printk("M3_BOOT source=%s profile=independent-health commands=%d fault_default=off\n",
#endif
           CRUX_M3_SOURCE, IS_ENABLED(CONFIG_CRUX_M3_TEST_COMMANDS));
#if defined(CONFIG_CRUX_M3_INTEGRATED)
    printk("M3_SCOPE non_uart=SW5 network=required readiness=not_ready automatic_window=0\n");
#else
    printk("M3_PENDING non_uart=unqualified network=held readiness=not_ready\n");
#endif
    report_reset_cause();
    k_tid_t worker = k_thread_create(&work_thread, work_stack, K_THREAD_STACK_SIZEOF(work_stack),
                                     run_work, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    (void)k_thread_name_set(worker, "m3-critical");
#if defined(CONFIG_CRUX_M3_INTEGRATED)
    k_tid_t network =
        k_thread_create(&network_thread, network_stack, K_THREAD_STACK_SIZEOF(network_stack),
                        run_network, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
    (void)k_thread_name_set(network, "m3-network");
    controller_health_t network_health;
    (void)controller_health_init(&network_health, 16000);
#endif
    const struct device *watchdog = DEVICE_DT_GET_OR_NULL(DT_ALIAS(watchdog0));
    controller_health_t health;
    (void)controller_health_init(&health, 500);
    int64_t continuous_health_ms = -1;
    int64_t next_report = 0;
    bool arm_attempted = false;
    bool stale_reported = false;
#if defined(CONFIG_CRUX_M3_INTEGRATED) && defined(CONFIG_CRUX_M3_NATIVE_OUTPUT)
    /* Separate initial idle samples from the owners' startup/report burst.
     * Keep the existing relative loop and never wait for a favourable queue.
     */
    (void)k_sleep(K_MSEC(CRUX_M3_INITIAL_OBSERVER_PHASE_MS));
#endif
    for (;;) {
        struct work_snapshot value = read_snapshot();
#if defined(CONFIG_CRUX_M3_INTEGRATED)
        struct network_snapshot net = read_network();
#endif
        int64_t now = k_uptime_get();
        controller_progress_t progress = {
            .completed_ms = value.completed_ms, .sequence = value.sequence, .ok = value.ok};
#if defined(CONFIG_CRUX_M3_INTEGRATED)
        struct net_if *iface = net_if_get_default();
        bool network_current = net.ok && net.active && now < net.window_end_ms && iface != NULL &&
                               net_if_is_carrier_ok(iface);
        controller_progress_t net_progress = {
            .completed_ms = net.completed_ms, .sequence = net.sequence, .ok = network_current};
        bool healthy =
            controller_health_joint_observe(&health, &network_health, now, progress, net_progress);
#else
        bool healthy = controller_health_observe(&health, now, progress);
#endif
        if (!healthy) {
            continuous_health_ms = -1;
        } else if (continuous_health_ms < 0) {
            continuous_health_ms = now;
        }
        if (atomic_cas(&arm_request, 1, 0)) {
#if defined(CONFIG_CRUX_M3_INTEGRATED)
            if (!healthy || !net.active ||
                !controller_m3_arm_allowed(now, continuous_health_ms, net.window_end_ms)) {
#else
            if (!healthy || continuous_health_ms < 0 || now - continuous_health_ms < 1000) {
#endif
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
#if defined(CONFIG_CRUX_M3_INTEGRATED)
                    printk("M3_WDT_ARMED timeout_ms=2000 debug_pause=1 freshness_ms=500 "
                           "network_freshness_ms=16000\n");
#else
                    printk("M3_WDT_ARMED timeout_ms=2000 debug_pause=1 freshness_ms=500\n");
#endif
                }
            }
        }
        if (atomic_get(&armed) != 0 && healthy) {
            int result = wdt_feed(watchdog, 0);
            if (result != 0) {
                printk("M3_WDT_FAILED feed=%d\n", result);
                return 1;
            }
#if defined(CONFIG_CRUX_M3_INTEGRATED)
            printk("M3_WDT_FEED monotonic_ms=%lld sequence=%u completed_ms=%lld "
                   "network_sequence=%u network_completed_ms=%lld\n",
                   (long long)k_uptime_get(), value.sequence, (long long)value.completed_ms,
                   net.sequence, (long long)net.completed_ms);
#else
            printk("M3_WDT_FEED monotonic_ms=%lld sequence=%u completed_ms=%lld\n",
                   (long long)k_uptime_get(), value.sequence, (long long)value.completed_ms);
#endif
        } else if (atomic_get(&armed) != 0 && !stale_reported) {
            stale_reported = true;
#if defined(CONFIG_CRUX_M3_INTEGRATED)
            printk("M3_HEALTH_STALE monotonic_ms=%lld sequence=%u network_sequence=%u "
                   "network_completed_ms=%lld window_end_ms=%lld\n",
                   (long long)now, value.sequence, net.sequence, (long long)net.completed_ms,
                   (long long)net.window_end_ms);
#else
            printk("M3_HEALTH_STALE monotonic_ms=%lld sequence=%u\n", (long long)now,
                   value.sequence);
#endif
        }
        if (now >= next_report) {
#if defined(CONFIG_CRUX_M3_NATIVE_OUTPUT)
            /* Observe before enqueueing this report, without affecting health decisions. */
            int64_t queued_at = k_uptime_get();
            unsigned int queued = log_buffered_cnt();
            struct m3_logger_resources logger = m3_logger_resources_get();
#endif
            size_t worker_unused = 0;
            size_t main_unused = 0;
            int worker_result = k_thread_stack_space_get(worker, &worker_unused);
            int main_result = k_thread_stack_space_get(k_current_get(), &main_unused);
#if defined(CONFIG_CRUX_M3_INTEGRATED)
            size_t network_unused = 0;
            int network_result = k_thread_stack_space_get(network, &network_unused);
            printk("M3_OBSERVER_ALIVE monotonic_ms=%lld healthy=%d sequence=%u readiness=%d "
                   "network_sequence=%u network_completed_ms=%lld network_active=%d "
                   "network_status=%d window_end_ms=%lld\n",
                   (long long)now, healthy, value.sequence,
                   controller_readiness(value.time_ready && healthy, value.device_status == 0,
                                        network_current && healthy, true),
                   net.sequence, (long long)net.completed_ms, net.active, net.status,
                   (long long)net.window_end_ms);
            printk("M3_STACK worker_status=%d worker_object=%zu worker_usable=%zu "
                   "worker_unused=%zu main_status=%d main_usable=%zu main_unused=%zu "
                   "network_status=%d network_object=%zu network_usable=%zu network_unused=%zu\n",
                   worker_result, sizeof(work_stack), K_THREAD_STACK_SIZEOF(work_stack),
                   worker_unused, main_result, k_current_get()->stack_info.size, main_unused,
                   network_result, sizeof(network_stack), K_THREAD_STACK_SIZEOF(network_stack),
                   network_unused);
#else
            printk("M3_OBSERVER_ALIVE monotonic_ms=%lld healthy=%d sequence=%u readiness=%d\n",
                   (long long)now, healthy, value.sequence,
                   controller_readiness(value.time_ready, value.device_status == 0, false, true));
            printk("M3_STACK worker_status=%d worker_object=%zu worker_usable=%zu "
                   "worker_unused=%zu main_status=%d main_usable=%zu main_unused=%zu\n",
                   worker_result, sizeof(work_stack), K_THREAD_STACK_SIZEOF(work_stack),
                   worker_unused, main_result, k_current_get()->stack_info.size, main_unused);
#endif
#if defined(CONFIG_CRUX_M3_NATIVE_OUTPUT)
            printk("M3_LOGGER monotonic_ms=%lld queue_snapshot_ms=%lld found=%u status=%d "
                   "configured=%u usable=%zu unused=%zu priority=%d queued=%u\n",
                   (long long)k_uptime_get(), (long long)queued_at, logger.found, logger.status,
                   (unsigned int)CONFIG_LOG_PROCESS_THREAD_STACK_SIZE, logger.usable, logger.unused,
                   logger.priority, queued);
#endif
            if (now >= 2000 && atomic_get(&armed) == 0) {
                thread_analyzer_print(0);
            }
            next_report = now + 1000;
        }
        (void)k_sleep(K_MSEC(100));
    }
}
