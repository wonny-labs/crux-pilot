/* Executes the actual sample composition with controlled clocks, owners and drivers.
 * This is a state-machine test, not a scheduler, socket-stack or watchdog test. */
// clang-format off
#include <zephyr/kernel.h> /* Required before thread_analyzer arch-dependent declarations. */
// clang-format on
#include "controller.h"
#include "device_input.h"
#include "network_client.h"
#include <crux/time.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/net/net_if.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

static jmp_buf exit_loop;
static int64_t clock_ms, stop_ms;
static int pair_result, pair_count, feeds, setups, forever_count, scenario;
static bool pair_active, carrier = true;
static char output[65536];
static size_t output_size;
static struct k_thread fake_thread;
static int64_t fake_now(void) {
    return clock_ms;
}
static int fake_sleep(k_timeout_t timeout);
static void fake_print(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int size = vsnprintf(output + output_size, sizeof(output) - output_size, format, args);
    va_end(args);
    zassert_true(size >= 0 && (size_t)size < sizeof(output) - output_size, "fixture log overflow");
    output_size += (size_t)size;
}
static int fake_pair(unsigned int trial, unsigned int sequence, int64_t deadline) {
    zassert_equal(trial, 1);
    zassert_equal(sequence, (unsigned int)pair_count + 1);
    zassert_equal(deadline, clock_ms + 14000);
    pair_count++;
    pair_active = true;
    clock_ms += 100;
    return pair_result;
}
static int fake_init(void) {
    return 0;
}
static int fake_stack(const struct k_thread *thread, size_t *unused) {
    ARG_UNUSED(thread);
    *unused = 2048;
    return 0;
}
static k_tid_t fake_create(struct k_thread *thread, k_thread_stack_t *stack, size_t size,
                           k_thread_entry_t entry, void *a, void *b, void *c, int priority,
                           uint32_t options, k_timeout_t delay) {
    ARG_UNUSED(stack);
    ARG_UNUSED(size);
    ARG_UNUSED(entry);
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    ARG_UNUSED(priority);
    ARG_UNUSED(options);
    ARG_UNUSED(delay);
    return thread;
}
static int fake_name(k_tid_t thread, const char *name) {
    ARG_UNUSED(thread);
    ARG_UNUSED(name);
    return 0;
}
static void fake_analyzer(unsigned int cpu) {
    ARG_UNUSED(cpu);
}
static struct k_thread *fake_current(void) {
    return &fake_thread;
}
static struct net_if *fake_interface(void) {
    return (struct net_if *)&fake_thread;
}
static bool fake_carrier(struct net_if *iface) {
    ARG_UNUSED(iface);
    return carrier;
}
static bool fake_ready(const struct device *device) {
    ARG_UNUSED(device);
    return true;
}
static int fake_install(const struct device *d, const struct wdt_timeout_cfg *cfg) {
    ARG_UNUSED(d);
    zassert_equal(cfg->window.max, 2000);
    return 0;
}
static int fake_setup(const struct device *d, uint8_t options) {
    ARG_UNUSED(d);
    ARG_UNUSED(options);
    setups++;
    return 0;
}
static int fake_feed(const struct device *d, int channel) {
    ARG_UNUSED(d);
    ARG_UNUSED(channel);
    feeds++;
    return 0;
}
static int fake_cause(uint32_t *value) {
    *value = 0x10;
    return 0;
}
static int fake_clear(void) {
    return 0;
}
static int fake_device(int *pressed) {
    *pressed = 0;
    return 0;
}
static crux_status_t fake_time(void) {
    return CRUX_OK;
}
static controller_reading_t fake_observe(uint32_t cycle) {
    return (controller_reading_t){.cycle = cycle, .status = CRUX_ERR_NO_DATA};
}
static crux_status_t fake_advance(controller_schedule_t *schedule, int64_t now) {
    schedule->deadline_ms = now + 100;
    return CRUX_OK;
}

#define CONFIG_CRUX_M3_INTEGRATED 1
#define CONFIG_CRUX_M3_TEST_COMMANDS 1
#define CRUX_M3_SOURCE "controlled-composition-fixture"
#define main composition_main
#define k_uptime_get fake_now
#define k_sleep fake_sleep
#define printk fake_print
#define k_thread_create fake_create
#define k_thread_name_set fake_name
#define k_thread_stack_space_get fake_stack
#define k_current_get fake_current
#define thread_analyzer_print fake_analyzer
#define net_if_get_default fake_interface
#define net_if_is_carrier_ok fake_carrier
#define device_is_ready fake_ready
#define wdt_install_timeout fake_install
#define wdt_setup fake_setup
#define wdt_feed fake_feed
#define hwinfo_get_supported_reset_cause fake_cause
#define hwinfo_get_reset_cause fake_cause
#define hwinfo_clear_reset_cause fake_clear
#define controller_network_init fake_init
#define controller_network_pair fake_pair
#define controller_device_init fake_init
#define controller_device_read fake_device
#define crux_time_init fake_time
#define controller_observe fake_observe
#define controller_schedule_advance fake_advance
#undef DEVICE_DT_GET_OR_NULL
#define DEVICE_DT_GET_OR_NULL(node) ((const struct device *)&fake_thread)
#undef shell_print
#undef shell_error
#define shell_print(shell, ...) (ARG_UNUSED(shell), fake_print(__VA_ARGS__))
#define shell_error(shell, ...) (ARG_UNUSED(shell), fake_print(__VA_ARGS__))
#include "../../../samples/controller/zephyr/m3_main.c"
#undef main

static int fake_sleep(k_timeout_t timeout) {
    if (K_TIMEOUT_EQ(timeout, K_FOREVER)) {
        forever_count++;
        longjmp(exit_loop, 1);
    }
    if (timeout.ticks < 0)
        clock_ms = k_ticks_to_ms_floor64(-timeout.ticks - 2);
    else
        clock_ms += k_ticks_to_ms_floor64(timeout.ticks);
    if (scenario == 1 || scenario == 2 || scenario == 4 || scenario == 5) {
        snapshot = (struct work_snapshot){.completed_ms = clock_ms,
                                          .sequence = (uint32_t)(clock_ms / 100 + 1),
                                          .time_ready = true,
                                          .ok = true};
        network_snapshot = (struct network_snapshot){.completed_ms = clock_ms,
                                                     .sequence = (uint32_t)(clock_ms / 100 + 1),
                                                     .window_end_ms = scenario == 4 ? 35000 : 60000,
                                                     .active = true,
                                                     .ok = true};
        if (clock_ms == (scenario == 5 ? 500 : 1500))
            atomic_set(&arm_request, 1);
        if (scenario == 2 && clock_ms >= 2500)
            network_snapshot.ok = false;
    }
    if (scenario == 3 && pair_active) {
        network_stall_requested = clock_ms;
        network_stall_deadline = clock_ms + 15000;
        atomic_set(&network_stall_request, 1);
        pair_active = false;
    }
    if (scenario == 6 && clock_ms >= 36000 && atomic_get(&network_stall_request) == 0) {
        network_stall_requested = clock_ms;
        network_stall_deadline = clock_ms + 15000;
        atomic_set(&network_stall_request, 1);
    }
    if (clock_ms >= stop_ms)
        longjmp(exit_loop, 1);
    return 0;
}
static void reset_fixture(void) {
    clock_ms = 0;
    stop_ms = 65000;
    pair_result = 0;
    pair_count = feeds = setups = forever_count = scenario = 0;
    pair_active = false;
    carrier = true;
    output_size = 0;
    output[0] = 0;
    fake_thread.stack_info.size = 4096;
    snapshot = (struct work_snapshot){.completed_ms = -1};
    network_snapshot = (struct network_snapshot){.completed_ms = -1, .window_end_ms = -1};
    atomic_set(&arm_request, 0);
    atomic_set(&armed, 0);
    atomic_set(&stall_request, 0);
    atomic_set(&network_window_request, 0);
    atomic_set(&network_stall_request, 0);
}
ZTEST(composition, test_no_automatic_window_and_single_request) {
    stop_ms = 1000;
    if (setjmp(exit_loop) == 0)
        run_network(NULL, NULL, NULL);
    zassert_equal(pair_count, 0);
    zassert_false(read_network().active);
    zassert_equal(request_network(NULL, 0, NULL), 0);
    zassert_equal(request_network(NULL, 0, NULL), -EALREADY);
}
ZTEST(composition, test_window_expires_without_new_pair_or_false_success) {
    atomic_set(&network_window_request, 1);
    if (setjmp(exit_loop) == 0)
        run_network(NULL, NULL, NULL);
    zassert_true(pair_count > 2);
    zassert_false(read_network().active);
    zassert_false(read_network().ok);
    zassert_equal(atomic_get(&network_window_request), 3);
    zassert_not_null(strstr(output, "M3_NET_WINDOW_STOP monotonic_ms=60000 result=0"));
}
ZTEST(composition, test_pair_failure_publishes_inactive_and_stops) {
    pair_result = -EIO;
    atomic_set(&network_window_request, 1);
    if (setjmp(exit_loop) == 0)
        run_network(NULL, NULL, NULL);
    zassert_equal(pair_count, 1);
    zassert_equal(read_network().status, -EIO);
    zassert_false(read_network().ok);
    zassert_false(read_network().active);
}
ZTEST(composition, test_network_owner_consumes_fault_at_boundary) {
    scenario = 3;
    atomic_set(&network_window_request, 1);
    if (setjmp(exit_loop) == 0)
        run_network(NULL, NULL, NULL);
    zassert_equal(pair_count, 1);
    zassert_equal(forever_count, 1);
    zassert_not_null(strstr(output, "M3_NET_FAULT_STALL"));
}
ZTEST(composition, test_expired_and_late_faults_are_rejected) {
    atomic_set(&network_window_request, 1);
    network_stall_requested = -15001;
    network_stall_deadline = -1;
    atomic_set(&network_stall_request, 1);
    if (setjmp(exit_loop) == 0)
        run_network(NULL, NULL, NULL);
    zassert_equal(forever_count, 0);
    zassert_not_null(strstr(output, "reason=expired"));
    reset_fixture();
    network_snapshot.active = true;
    network_snapshot.window_end_ms = 24999;
    work_stall_requested = 0;
    work_stall_deadline = 15000;
    atomic_set(&stall_request, 1);
    stop_ms = 200;
    if (setjmp(exit_loop) == 0)
        run_work(NULL, NULL, NULL);
    zassert_equal(forever_count, 0);
    zassert_not_null(strstr(output, "reason=inactive_or_window_budget"));
}
ZTEST(composition, test_joint_arm_and_owner_failure_stop_feeding) {
    scenario = 1;
    stop_ms = 4000;
    if (setjmp(exit_loop) == 0)
        (void)composition_main();
    zassert_equal(setups, 1);
    zassert_true(feeds > 10);
    reset_fixture();
    scenario = 2;
    stop_ms = 4000;
    if (setjmp(exit_loop) == 0)
        (void)composition_main();
    zassert_equal(setups, 1);
    zassert_true(feeds > 0 && feeds < 15);
    zassert_not_null(strstr(output, "M3_HEALTH_STALE monotonic_ms=2500"));
}

ZTEST(composition, test_actual_work_owner_accepts_valid_fault) {
    network_snapshot.active = true;
    network_snapshot.window_end_ms = 60000;
    work_stall_requested = 0;
    work_stall_deadline = 15000;
    atomic_set(&stall_request, 1);
    if (setjmp(exit_loop) == 0)
        run_work(NULL, NULL, NULL);
    zassert_equal(forever_count, 1);
    zassert_not_null(strstr(output, "M3_FAULT_STALL"));
}
ZTEST(composition, test_network_owner_rejects_late_fault_before_stall) {
    scenario = 6;
    atomic_set(&network_window_request, 1);
    if (setjmp(exit_loop) == 0)
        run_network(NULL, NULL, NULL);
    zassert_equal(forever_count, 0);
    zassert_not_null(strstr(output, "reason=window_budget"));
    zassert_is_null(strstr(output, "M3_NET_FAULT_STALL"));
}
ZTEST(composition, test_actual_arm_rejects_short_health_and_insufficient_window) {
    scenario = 4;
    stop_ms = 4000;
    if (setjmp(exit_loop) == 0)
        (void)composition_main();
    zassert_equal(setups, 0);
    zassert_equal(feeds, 0);
    zassert_not_null(strstr(output, "M3_ARM_REJECTED not_healthy"));
    reset_fixture();
    scenario = 5;
    stop_ms = 4000;
    if (setjmp(exit_loop) == 0)
        (void)composition_main();
    zassert_equal(setups, 0);
    zassert_equal(feeds, 0);
    zassert_not_null(strstr(output, "M3_ARM_REJECTED not_healthy"));
}
static void before(void *fixture) {
    ARG_UNUSED(fixture);
    reset_fixture();
}
ZTEST_SUITE(composition, NULL, NULL, before, NULL, NULL);
