#include "controller.h"
#include "runtime.h"
#include <crux/logging.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(logging_test));
static struct k_mutex capture_lock;
static char capture[16384];
static size_t used;
static struct k_thread *worker;
static void discover(const struct k_thread *thread, void *unused)
{
    (void)unused;
    const char *name = k_thread_name_get((k_tid_t)thread);
    if (name != NULL && strcmp(name, "logging") == 0)
        worker = (struct k_thread *)thread;
}
static void collect(const struct device *dev, size_t size, void *unused)
{
    (void)size;
    (void)unused;
    uint8_t bytes[128];
    uint32_t count;
    k_mutex_lock(&capture_lock, K_FOREVER);
    while ((count = uart_emul_get_tx_data(dev, bytes, sizeof(bytes))) != 0) {
        zassert_true(used + count < sizeof(capture), "capture overflow");
        memcpy(capture + used, bytes, count);
        used += count;
        capture[used] = '\0';
    }
    k_mutex_unlock(&capture_lock);
}
static bool contains(const char *text)
{
    k_mutex_lock(&capture_lock, K_FOREVER);
    bool found = strstr(capture, text) != NULL;
    k_mutex_unlock(&capture_lock);
    return found;
}
static void wait_for(const char *text)
{
    for (unsigned i = 0; i < 200 && !contains(text); ++i)
        k_sleep(K_MSEC(10));
    zassert_true(contains(text), "missing actual UART output: %s", text);
}
static void *setup(void)
{
    k_mutex_init(&capture_lock);
    zassert_true(device_is_ready(uart));
    uart_emul_callback_tx_data_ready_set(uart, collect, NULL);
    k_thread_foreach(discover, NULL);
    zassert_not_null(worker);
    return NULL;
}
ZTEST(logging, test_native_copy_filter_and_loss)
{
    zassert_equal(crux_log_set_level(CRUX_LOG_DEBUG), CRUX_OK);
    k_thread_suspend(worker);
    char tag[] = "mutable";
    char text[] = "original 100% %s";
    zassert_equal(crux_log_write(CRUX_LOG_INFO, tag, text), CRUX_OK);
    memset(tag, 'x', sizeof(tag) - 1);
    memset(text, 'x', sizeof(text) - 1);
    zassert_equal(crux_log_write(CRUX_LOG_ERROR, "levels", "error-record"), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_WARN, "levels", "warn-record"), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_DEBUG, "levels", "debug-record"), CRUX_OK);
    zassert_equal(crux_log_set_level(CRUX_LOG_OFF), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_ERROR, "levels", "hidden-record"), CRUX_OK);
    zassert_equal(crux_log_set_level(CRUX_LOG_DEBUG), CRUX_OK);
    for (unsigned i = 0; i < 100; ++i)
        zassert_equal(crux_log_write(CRUX_LOG_INFO, "burst", "overflow payload retaining oldest"),
                      CRUX_OK);
    k_thread_resume(worker);
    wait_for("<inf> crux_logging: mutable: original 100% %s");
    wait_for("<err> crux_logging: levels: error-record");
    wait_for("<wrn> crux_logging: levels: warn-record");
    wait_for("<dbg> crux_logging: crux_log_impl_write: levels: debug-record");
    wait_for("messages dropped");
    zassert_false(contains("hidden-record"));
    zassert_false(contains("xxxxxxxx"));
}
ZTEST_SUITE(logging, NULL, setup, NULL, NULL, NULL);

static void producer(void *index, void *unused1, void *unused2)
{
    (void)unused1;
    (void)unused2;
    const char *tag = (const char *)index;
    for (unsigned i = 0; i < 3; ++i) {
        char text[16];
        int length = snprintf(text, sizeof(text), "record=%u", i);
        zassert_true(length > 0 && (size_t)length < sizeof(text));
        zassert_equal(crux_log_write(CRUX_LOG_INFO, tag, text), CRUX_OK);
    }
}
/* Observe only fully delimited native output, checking each producer's
 * sequence without imposing a total order across concurrent producers. */
static unsigned producer_records(const char *tag)
{
    unsigned count = 0;
    k_mutex_lock(&capture_lock, K_FOREVER);
    const char *line = capture;
    const char *end;
    while ((end = strchr(line, '\n')) != NULL) {
        const char *match = strstr(line, tag);
        if (match != NULL && match < end) {
            char expected[64];
            int length = snprintf(expected, sizeof(expected), "%s: record=%u", tag, count);
            zassert_true(length > 0 && (size_t)length < sizeof(expected));
            zassert_true(match + length <= end && strncmp(match, expected, (size_t)length) == 0,
                         "corrupt, duplicate or reordered record");
            ++count;
        }
        line = end + 1;
    }
    k_mutex_unlock(&capture_lock);
    return count;
}
K_THREAD_STACK_ARRAY_DEFINE(producer_stacks, 3, 2048);
static struct k_thread producers[3];
ZTEST(logging, test_concurrent_native_records)
{
    zassert_equal(crux_log_set_level(CRUX_LOG_INFO), CRUX_OK);
    const char *tags[] = {"producer.a", "producer.b", "producer.c"};
    for (unsigned i = 0; i < 3; ++i)
        k_thread_create(&producers[i], producer_stacks[i],
                        K_THREAD_STACK_SIZEOF(producer_stacks[i]), producer, (void *)tags[i], NULL,
                        NULL, 5, 0, K_NO_WAIT);
    for (unsigned i = 0; i < 3; ++i)
        zassert_equal(k_thread_join(&producers[i], K_SECONDS(2)), 0);
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned attempt = 0; attempt < 200 && producer_records(tags[i]) < 3; ++attempt)
            k_sleep(K_MSEC(10));
        zassert_equal(producer_records(tags[i]), 3, "missing complete records");
    }
}

ZTEST(logging, test_maximum_text_not_truncated)
{
    char text[CRUX_LOG_TEXT_MAX + 1];
    memset(text, 'q', CRUX_LOG_TEXT_MAX);
    memcpy(text + CRUX_LOG_TEXT_MAX - 8, "END-256!", 8);
    text[CRUX_LOG_TEXT_MAX] = '\0';
    zassert_equal(crux_log_set_level(CRUX_LOG_INFO), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_INFO, "maximum", text), CRUX_OK);
    wait_for(text);
}

ZTEST(logging, test_portable_consumers)
{
    zassert_equal(crux_log_set_level(CRUX_LOG_INFO), CRUX_OK);
    zassert_equal(logging_runtime_record(42, 0), CRUX_OK);
    zassert_equal(logging_cpp_record(43, -7), CRUX_OK);
    zassert_equal(logging_controller_state(false), CRUX_OK);
    wait_for("runtime.c: sequence=42 code=0");
    wait_for("runtime.cpp: sequence=43 code=-7");
    wait_for("controller: fault");
}
static void in_interrupt(const void *unused)
{
    (void)unused;
    zassert_equal(crux_log_write(CRUX_LOG_ERROR, "isr", "forbidden"), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_log_set_level(CRUX_LOG_OFF), CRUX_ERR_NOT_SUPPORTED);
}
ZTEST(logging, test_isr_rejected)
{
    irq_offload(in_interrupt, NULL);
}
