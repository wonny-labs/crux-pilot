#include "network_mdio_trace.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

static struct device devices[2];
static struct k_thread logger = {.name = "logging", .stack_info = {.size = 2048}};
static bool retained, copying, traversing, late, duplicate;
static int64_t now;
static unsigned int getters, count;
static char lines[8][1024];

const struct device *observer_device_get(int node) {
    assert(node == 1 || node == 2);
    return &devices[node - 1];
}
bool device_is_ready(const struct device *dev) {
    assert(!copying && !traversing && dev != NULL);
    return true;
}
int64_t k_uptime_get(void) {
    return now;
}
const char *k_thread_name_get(k_tid_t thread) {
    return thread->name;
}
void k_thread_foreach(void (*callback)(const struct k_thread *, void *), void *context) {
    traversing = true;
    callback(&logger, context);
    if (duplicate) {
        callback(&logger, context);
    }
    traversing = false;
}
int k_thread_stack_space_get(const struct k_thread *thread, size_t *unused) {
    assert(!traversing && thread == &logger);
    *unused = 1536;
    return 0;
}
int k_thread_priority_get(k_tid_t thread) {
    assert(!traversing && thread == &logger);
    return 14;
}
uint32_t log_buffered_cnt(void) {
    assert(!traversing && !copying);
    return 0;
}
bool crux_m3_mdio_failure_get(const struct device *dev, struct crux_m3_mdio_failure *out) {
    assert(dev == &devices[1] && !traversing);
    copying = true;
    bool snapshot = retained;
    if (snapshot) {
        memset(out, 0, sizeof(*out));
        out->valid = 1;
        out->result = -116;
    }
    getters++;
    now++;
    if (late && getters == 2) {
        retained = true; /* A later failure does not change the completed snapshot. */
    }
    copying = false;
    return snapshot;
}
int printk(const char *format, ...) {
    assert(!copying && !traversing && count < 8);
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(lines[count], sizeof(lines[count]), format, arguments);
    va_end(arguments);
    assert(length > 0 && (size_t)length < sizeof(lines[count]));
    assert(lines[count][length - 1] == '\n');
    count++;
    return length;
}
int main(int argc, char **argv) {
    retained = argc > 1 && strcmp(argv[1], "failure") == 0;
    late = argc > 1 && strcmp(argv[1], "late") == 0;
    duplicate = argc > 1 && strcmp(argv[1], "duplicate") == 0;
    now = 100;
    crux_m3_deferred_poll();
    assert(count == 0);
    crux_m3_deferred_start();
    crux_m3_deferred_start();
    assert(count == 1 &&
           strcmp(lines[0], "M3_OUTPUT_START monotonic_ms=100 window_ms=15000\n") == 0);
    now = 15099;
    crux_m3_deferred_poll();
    assert(count == 1);
    now = 15100;
    crux_m3_deferred_poll();
    unsigned int final = retained && !late ? 2 : 1;
    assert(strncmp(lines[final], "M3_OUTPUT_FINAL ", 16) == 0);
    assert(
        strstr(lines[final], "snapshot_before_ms=15101 snapshot_after_ms=15102 elapsed_ms=15001 "));
    assert(strstr(lines[final], retained && !late ? "failure_valid=1 " : "failure_valid=0 "));
    if (duplicate) {
        assert(strstr(lines[final], "logger_found=2 status=-1 configured=2048 usable=0 "));
    } else {
        assert(strstr(lines[final], "logger_found=1 status=0 configured=2048 usable=2048 "
                                    "unused=1536 priority=14 queued=0"));
    }
    if (final == 2) {
        assert(strncmp(lines[1], "M3_MDIO_FIRST_FAILURE ", 22) == 0);
    }
    unsigned int previous = count;
    now = 20000;
    crux_m3_deferred_poll();
    assert(count == previous);
    if (late) {
        crux_m3_mdio_trace_observe();
        assert(count == previous + 1);
        assert(strncmp(lines[count - 1], "M3_MDIO_FIRST_FAILURE ", 22) == 0);
    }
    puts("ACTUAL_DEFERRED_COMPLETION_OBSERVER_PASS; snapshot scope only, no physical timing claim");
}
