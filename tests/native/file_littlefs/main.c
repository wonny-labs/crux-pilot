#include "file_native.h"
#include <crux/file.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
/* Private composition hook; no native headers/types are in consumer API. */
extern crux_file_result_t pilot_file_create(void);
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t mount = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)PARTITION_ID(file_test_partition),
    .mnt_point = "/files",
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static int truncate_error, close_error, close_calls;
static ssize_t write_result = -1;
int __real_fs_truncate(struct fs_file_t *f, off_t size);
int __real_fs_close(struct fs_file_t *f);
ssize_t __real_fs_write(struct fs_file_t *f, const void *b, size_t n);
int __wrap_fs_truncate(struct fs_file_t *f, off_t size) {
    return truncate_error ? truncate_error : __real_fs_truncate(f, size);
}
int __wrap_fs_close(struct fs_file_t *f) {
    close_calls++;
    int rc = __real_fs_close(f);
    return close_error ? close_error : rc;
}
ssize_t __wrap_fs_write(struct fs_file_t *f, const void *b, size_t n) {
    if (write_result == -1)
        return __real_fs_write(f, b, n);
    if (write_result <= 0)
        return write_result;
    return __real_fs_write(f, b, MIN(n, (size_t)write_result));
}
static void *setup(void) {
    crux_file_t *f = NULL;
    zassert_equal(crux_file_open("x", CRUX_FILE_READ, &f), CRUX_FILE_ERR_NOT_READY);
    zassert_ok(fs_mkfs(FS_LITTLEFS, (uintptr_t)mount.storage_dev, NULL, 0));
    zassert_ok(fs_mount(&mount));
    zassert_equal(crux_file_zephyr_bind("/files"), CRUX_FILE_OK);
    zassert_equal(pilot_file_create(), CRUX_FILE_OK);
    return NULL;
}
ZTEST(file_littlefs, test_persisted_crud_and_sync) {
    crux_file_t *f = NULL;
    size_t n;
    char bytes[8] = {0};
    zassert_equal(crux_file_open("record", CRUX_FILE_CREATE_REPLACE, &f), CRUX_FILE_OK);
    zassert_equal(crux_file_write(f, "pilot", 5, &n), CRUX_FILE_OK);
    zassert_equal(n, 5);
    zassert_equal(crux_file_sync(f), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&f), CRUX_FILE_OK);
    zassert_is_null(f);
    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mount(&mount));
    zassert_equal(crux_file_open("record", CRUX_FILE_READ, &f), CRUX_FILE_OK);
    zassert_equal(crux_file_read(f, bytes, sizeof(bytes), &n), CRUX_FILE_OK);
    zassert_equal(n, 5);
    zassert_mem_equal(bytes, "pilot", 5);
    zassert_equal(crux_file_seek(f, 6), CRUX_FILE_ERR_OUT_OF_RANGE);
    zassert_equal(crux_file_seek(f, UINT64_MAX), CRUX_FILE_ERR_OUT_OF_RANGE);
    zassert_equal(crux_file_write(f, "x", 1, &n), CRUX_FILE_ERR_ACCESS);
    zassert_equal(crux_file_close(&f), CRUX_FILE_OK);
    zassert_equal(crux_file_remove("record"), CRUX_FILE_OK);
    zassert_equal(crux_file_open("record", CRUX_FILE_READ, &f), CRUX_FILE_ERR_NOT_FOUND);
}
static void before(void *fixture) {
    ARG_UNUSED(fixture);
    truncate_error = close_error = 0;
    write_result = -1;
    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mkfs(FS_LITTLEFS, (uintptr_t)mount.storage_dev, NULL, 0));
    zassert_ok(fs_mount(&mount));
}
ZTEST(file_littlefs, test_native_partial_and_released_state) {
    crux_file_t *f = NULL;
    size_t n;
    truncate_error = -ENOSPC;
    close_error = -EIO;
    int before_close = close_calls;
    zassert_equal(crux_file_open("fail", CRUX_FILE_CREATE_REPLACE, &f), CRUX_FILE_ERR_NO_SPACE);
    zassert_is_null(f);
    zassert_equal(close_calls, before_close + 1);
    zassert_equal(crux_file_zephyr_last_cleanup_error(), CRUX_FILE_ERR_IO);
    truncate_error = close_error = 0;
    for (unsigned i = 0; i < 12; i++) {
        zassert_equal(crux_file_open("good", CRUX_FILE_CREATE_REPLACE, &f), CRUX_FILE_OK);
        write_result = 3;
        zassert_equal(crux_file_write(f, "abcdefgh", 8, &n), CRUX_FILE_PARTIAL);
        zassert_equal(n, 3);
        write_result = 0;
        zassert_equal(crux_file_write(f, "abcdefgh", 8, &n), CRUX_FILE_PARTIAL);
        zassert_equal(n, 0);
        write_result = -12345;
        zassert_equal(crux_file_write(f, "abcdefgh", 8, &n), CRUX_FILE_ERR_IO);
        zassert_equal(n, 0);
        write_result = -1;
        close_error = -EIO; /* real close releases native state before error is surfaced */
        before_close = close_calls;
        zassert_equal(crux_file_close(&f), CRUX_FILE_ERR_IO);
        zassert_is_null(f);
        zassert_equal(crux_file_close(&f), CRUX_FILE_ERR_INVALID_STATE);
        zassert_equal(close_calls, before_close + 1);
        close_error = 0;
    }
}
ZTEST(file_littlefs, test_full_and_reclaim) {
    crux_file_t *f = NULL;
    static uint8_t data[1024];
    memset(data, 0x5a, sizeof(data));
    size_t n;
    zassert_equal(crux_file_open("sentinel", CRUX_FILE_CREATE_REPLACE, &f), CRUX_FILE_OK);
    zassert_equal(crux_file_write(f, "kept", 4, &n), CRUX_FILE_OK);
    zassert_equal(crux_file_sync(f), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&f), CRUX_FILE_OK);
    zassert_equal(crux_file_open("filler", CRUX_FILE_CREATE_REPLACE, &f), CRUX_FILE_OK);
    bool full = false;
    for (unsigned i = 0; i < 128; i++) {
        crux_file_result_t rc = crux_file_write(f, data, sizeof(data), &n);
        if (rc == CRUX_FILE_OK)
            rc = crux_file_sync(f);
        if (rc == CRUX_FILE_ERR_NO_SPACE) {
            full = true;
            break;
        }
        zassert_equal(rc, CRUX_FILE_OK);
    }
    zassert_true(full, "finite 128 KiB workload must exhaust a 64 KiB volume");
    crux_file_result_t closed = crux_file_close(&f);
    zassert_true(closed == CRUX_FILE_OK || closed == CRUX_FILE_ERR_NO_SPACE);
    zassert_equal(crux_file_remove("filler"), CRUX_FILE_OK);
    zassert_equal(crux_file_open("new", CRUX_FILE_CREATE_REPLACE, &f), CRUX_FILE_OK);
    zassert_equal(crux_file_write(f, data, sizeof(data), &n), CRUX_FILE_OK);
    zassert_equal(crux_file_sync(f), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&f), CRUX_FILE_OK);
    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mount(&mount));
    zassert_equal(crux_file_open("sentinel", CRUX_FILE_READ, &f), CRUX_FILE_OK);
    memset(data, 0, 4);
    zassert_equal(crux_file_read(f, data, 4, &n), CRUX_FILE_OK);
    zassert_equal(n, 4);
    zassert_mem_equal(data, "kept", 4);
    zassert_equal(crux_file_close(&f), CRUX_FILE_OK);
}
static uint8_t checkpoint[0x10000];
static uint8_t *media(void) {
    const struct device *dev = PARTITION_DEVICE(file_test_partition);
    size_t size;
    uint8_t *bytes = flash_simulator_get_memory(dev, &size);
    zassert_true(size >= PARTITION_OFFSET(file_test_partition) + sizeof(checkpoint));
    return bytes + PARTITION_OFFSET(file_test_partition);
}
static int remaining;
static int interrupted_byte(const struct device *dev, off_t offset, uint8_t value) {
    ARG_UNUSED(dev);
    ARG_UNUSED(offset);
    if (remaining-- > 0)
        return value;
    return -EIO;
}
ZTEST(file_littlefs, test_interrupted_sync_checkpoint_reinitialization) {
    crux_file_t *f = NULL;
    size_t n;
    zassert_equal(crux_file_open("safe", CRUX_FILE_CREATE_REPLACE, &f), CRUX_FILE_OK);
    zassert_equal(crux_file_write(f, "before", 6, &n), CRUX_FILE_OK);
    zassert_equal(crux_file_sync(f), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&f), CRUX_FILE_OK);
    zassert_equal(crux_file_open("safe", CRUX_FILE_READ_WRITE, &f), CRUX_FILE_OK);
    zassert_equal(crux_file_write(f, "after!", 6, &n), CRUX_FILE_OK);
    remaining = 16;
    struct flash_simulator_cb cb = {.write_byte = interrupted_byte};
    const struct device *dev = PARTITION_DEVICE(file_test_partition);
    flash_simulator_set_callbacks(dev, &cb);
    zassert_equal(crux_file_sync(f), CRUX_FILE_ERR_IO);
    memcpy(checkpoint, media(), sizeof(checkpoint)); /* interruption image, before cleanup */
    (void)crux_file_close(&f); /* retire volatile handles; its possible writes are discarded */
    zassert_is_null(f);
    zassert_ok(fs_unmount(&mount));
    flash_simulator_set_callbacks(dev, NULL);
    memcpy(media(), checkpoint, sizeof(checkpoint));
    zassert_ok(fs_mount(&mount));
    zassert_equal(crux_file_open("safe", CRUX_FILE_READ, &f), CRUX_FILE_OK);
    char bytes[6];
    zassert_equal(crux_file_read(f, bytes, sizeof(bytes), &n), CRUX_FILE_OK);
    zassert_equal(n, 6);
    zassert_mem_equal(bytes, "before", 6);
    zassert_equal(crux_file_close(&f), CRUX_FILE_OK);
}
ZTEST(file_littlefs, test_mount_failure_never_formats) {
    zassert_ok(fs_unmount(&mount));
    memcpy(checkpoint, media(), sizeof(checkpoint));
    memset(media(), 0, sizeof(checkpoint));
    zassert_true(fs_mount(&mount) < 0);
    for (size_t i = 0; i < sizeof(checkpoint); i++)
        zassert_equal(media()[i], 0);
    crux_file_t *f = NULL;
    zassert_equal(crux_file_open("x", CRUX_FILE_READ, &f), CRUX_FILE_ERR_NOT_READY);
    memcpy(media(), checkpoint, sizeof(checkpoint));
    zassert_ok(fs_mount(&mount));
}
ZTEST(file_littlefs, test_seek_unflushed_eof) {
    crux_file_t *f = NULL;
    size_t n;
    char bytes[3];
    zassert_equal(crux_file_open("seek", CRUX_FILE_CREATE_REPLACE, &f), CRUX_FILE_OK);
    zassert_equal(crux_file_write(f, "abcdef", 6, &n), CRUX_FILE_OK);
    zassert_equal(crux_file_seek(f, 3), CRUX_FILE_OK);
    zassert_equal(crux_file_read(f, bytes, 3, &n), CRUX_FILE_OK);
    zassert_equal(n, 3);
    zassert_mem_equal(bytes, "def", 3);
    zassert_equal(crux_file_seek(f, 2), CRUX_FILE_OK);
    zassert_equal(crux_file_seek(f, 7), CRUX_FILE_ERR_OUT_OF_RANGE);
    zassert_equal(crux_file_read(f, bytes, 3, &n), CRUX_FILE_OK);
    zassert_equal(n, 3);
    zassert_mem_equal(bytes, "cde", 3);
    zassert_equal(crux_file_close(&f), CRUX_FILE_OK);
}
ZTEST(file_littlefs, test_backend_transfer_width_before_dispatch) {
    crux_file_t *f = NULL;
    size_t n;
    char byte = 0;
    zassert_equal(crux_file_open("width", CRUX_FILE_CREATE_REPLACE, &f), CRUX_FILE_OK);
    zassert_equal(crux_file_read(f, &byte, (size_t)INT32_MAX + 1, &n), CRUX_FILE_ERR_OUT_OF_RANGE);
    zassert_equal(n, 0);
    write_result = 0; /* bounded VFS double avoids reading a huge caller buffer */
    zassert_equal(crux_file_write(f, &byte, (size_t)INT32_MAX + 1, &n), CRUX_FILE_ERR_OUT_OF_RANGE);
    zassert_equal(n, 0);
    write_result = -1;
    zassert_equal(crux_file_close(&f), CRUX_FILE_OK);
}
ZTEST_SUITE(file_littlefs, NULL, setup, before, NULL, NULL);
