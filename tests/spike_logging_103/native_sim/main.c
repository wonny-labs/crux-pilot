/*
 * Issue #103: observe the real pinned Zephyr native UART + FS logging backends.
 * This fixture uses simulated Flash only. It intentionally does not enable
 * CONFIG_CRUX_LOGGING because today's Crux composition rejects extra backends.
 * The result establishes native feasibility, not portable Crux integration.
 */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/arch/posix/posix_trace.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk-hooks.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(spike103, LOG_LEVEL_INF);

/*
 * native_sim's UART Console can take the printk hook after the built-in
 * POSIX Console initialisation. Restore an independent stdout test channel
 * after native UART setup so Twister can observe Ztest without consuming
 * the dedicated logging UART emulator output.
 */
static int spike_trace_char(int c)
{
    static char line[256];
    static size_t used;

    if (c == '\r') {
        return c;
    }
    line[used++] = (char)c;
    if (c == '\n' || used == sizeof(line) - 1) {
        line[used] = '\0';
        posix_print_trace("%s", line);
        used = 0;
    }
    return c;
}

static int spike_restore_console(void)
{
    __printk_hook_install(spike_trace_char);
    return 0;
}

SYS_INIT(spike_restore_console, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#define MARKER "SPIKE103_NATIVE_UART_FS_RECORD"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(spike_fs);
static struct fs_mount_t spike_mount = {
    .type = FS_LITTLEFS,
    .fs_data = &spike_fs,
    .storage_dev = (void *)PARTITION_ID(spike_log_partition),
    .mnt_point = "/logspike",
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(spike_log_uart));
static struct k_mutex capture_lock;
static char uart_capture[2048];
static size_t uart_used;
static atomic_t fs_writes;
static atomic_t fs_syncs;
static atomic_t fs_marker_written;

static bool bytes_contain(const void *haystack, size_t length, const char *needle)
{
    const size_t needle_length = strlen(needle);
    if (length < needle_length) {
        return false;
    }
    const uint8_t *data = haystack;
    for (size_t i = 0; i <= length - needle_length; ++i) {
        if (memcmp(data + i, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

ssize_t __real_fs_write(struct fs_file_t *file, const void *data, size_t size);
int __real_fs_sync(struct fs_file_t *file);

ssize_t __wrap_fs_write(struct fs_file_t *file, const void *data, size_t size)
{
    const ssize_t rc = __real_fs_write(file, data, size);
    atomic_inc(&fs_writes);
    if (rc > 0 && bytes_contain(data, (size_t)rc, MARKER)) {
        atomic_set(&fs_marker_written, 1);
    }
    return rc;
}

int __wrap_fs_sync(struct fs_file_t *file)
{
    atomic_inc(&fs_syncs);
    return __real_fs_sync(file);
}

static void on_tx(const struct device *dev, size_t size, void *user_data)
{
    ARG_UNUSED(size);
    ARG_UNUSED(user_data);
    uint8_t bytes[128];
    uint32_t count;
    k_mutex_lock(&capture_lock, K_FOREVER);
    while ((count = uart_emul_get_tx_data(dev, bytes, sizeof(bytes))) != 0) {
        if (uart_used + count < sizeof(uart_capture)) {
            memcpy(uart_capture + uart_used, bytes, count);
            uart_used += count;
            uart_capture[uart_used] = '\0';
        }
    }
    k_mutex_unlock(&capture_lock);
}

static bool uart_has_marker(void)
{
    bool present;
    k_mutex_lock(&capture_lock, K_FOREVER);
    present = strstr(uart_capture, MARKER) != NULL;
    k_mutex_unlock(&capture_lock);
    return present;
}

static void *setup(void)
{
    k_mutex_init(&capture_lock);
    zassert_true(device_is_ready(uart), "UART emulator is not ready");
    /* Only this 64-KiB simulator partition is formatted. No target Flash. */
    zassert_ok(fs_mkfs(FS_LITTLEFS, (uintptr_t)spike_mount.storage_dev, NULL, 0));
    zassert_ok(fs_mount(&spike_mount));
    uart_emul_callback_tx_data_ready_set(uart, on_tx, NULL);
    return NULL;
}

ZTEST(spike103, test_native_uart_and_fs_receive_the_same_record)
{
    /* Clear counters after setup to exclude formatter/mount activity. */
    atomic_set(&fs_writes, 0);
    atomic_set(&fs_syncs, 0);
    atomic_set(&fs_marker_written, 0);

    LOG_INF("%s", MARKER);

    bool uart_seen = false;
    bool file_seen = false;
    for (unsigned i = 0; i < 300; ++i) {
        uart_seen = uart_has_marker();
        file_seen = atomic_get(&fs_marker_written) != 0;
        if (uart_seen && file_seen) {
            break;
        }
        k_sleep(K_MSEC(10));
    }
    /* Allow one idle period to observe any FS process-done notification. */
    k_sleep(K_MSEC(50));

    printk("SPIKE103_NATIVE_RESULT uart=%d fs_write=%d fs_marker=%d fs_sync=%d\n",
           uart_seen, (int)atomic_get(&fs_writes), file_seen,
           (int)atomic_get(&fs_syncs));
    zassert_true(uart_seen, "native UART backend did not transmit marker");
    zassert_true(file_seen, "native FS backend did not write marker");
    /*
     * fs_sync count is measured, not asserted. A lack of sync is a durability
     * concern, not a reason to forge a PASS/FAIL for a proposed behaviour.
     * No power-failure or durable-write assertion is made by this SPIKE.
     */
}

ZTEST_SUITE(spike103, NULL, setup, NULL, NULL, NULL);
