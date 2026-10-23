/* SPDX-License-Identifier: Apache-2.0 */
/* A private native_sim Flash API proxy beneath the selected NVS engine. */
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#include "nvs_priv.h"

BUILD_ASSERT(IS_ENABLED(CONFIG_BOARD_NATIVE_SIM));
BUILD_ASSERT(IS_ENABLED(CONFIG_FLASH_SIMULATOR));
BUILD_ASSERT(!IS_ENABLED(CONFIG_USERSPACE));

#define PART storage_partition
static const struct device *const real = DEVICE_DT_GET(DT_MTD_FROM_PARTITION(DT_NODELABEL(PART)));
static struct device proxy;
static struct flash_driver_api api;
static const struct flash_driver_api *original;
static struct nvs_fs fs;
static size_t reads, writes, erases;
static size_t fail_at;
static bool persistent;
static off_t begin;
static size_t span;

static bool within(off_t offset, size_t length) {
    return offset >= begin && (size_t)(offset - begin) <= span &&
           length <= span - (size_t)(offset - begin);
}
static int fault_read(const struct device *dev, off_t offset, void *data, size_t length) {
    ARG_UNUSED(dev);
    zassert_true(within(offset, length), "probe read escaped simulated area");
    reads++;
    if (persistent || (fail_at && reads == fail_at)) {
        return -EIO;
    }
    return original->read(real, offset, data, length);
}
static int trace_write(const struct device *dev, off_t offset, const void *data, size_t length) {
    ARG_UNUSED(dev);
    zassert_true(within(offset, length), "probe write escaped simulated area");
    writes++;
    return original->write(real, offset, data, length);
}
static int trace_erase(const struct device *dev, off_t offset, size_t length) {
    ARG_UNUSED(dev);
    zassert_true(within(offset, length), "probe erase escaped simulated area");
    erases++;
    return original->erase(real, offset, length);
}
static void before(void *fixture) {
    ARG_UNUSED(fixture);
    struct flash_pages_info page;
    zassert_true(device_is_ready(real));
    begin = PARTITION_OFFSET(PART);
    zassert_ok(flash_get_page_info_by_offs(real, begin, &page));
    span = page.size * 2;
    zassert_true(span <= PARTITION_SIZE(PART));
    zassert_ok(flash_erase(real, begin, span));
    proxy = *real;
    original = real->api;
    api = *original;
    api.read = fault_read;
    api.write = trace_write;
    api.erase = trace_erase;
    proxy.api = &api;
    memset(&fs, 0, sizeof(fs));
    fs.flash_device = &proxy;
    fs.offset = begin;
    fs.sector_size = page.size;
    fs.sector_count = 2;
    reads = writes = erases = fail_at = 0;
    persistent = false;
}
static void expect_read_failure(const char *scenario) {
    const uint8_t marker = 0xa5;
    uint8_t observed = 0;
    zassert_ok(flash_write(real, begin, &marker, sizeof(marker)));
    int rc = nvs_mount(&fs);
    zassert_ok(flash_read(real, begin, &observed, sizeof(observed)));
    printk(
        "NVS_STARTUP scenario=%s bad_init=%d rc=%d reads=%zu writes=%zu erases=%zu marker=%02x\n",
        scenario, IS_ENABLED(CONFIG_NVS_INIT_BAD_MEMORY_REGION), rc, reads, writes, erases,
        observed);
    zassert_equal(rc, -EIO, "original Flash read error must remain observable");
    zassert_equal(writes, 0, "no metadata write after failed startup scan");
    zassert_equal(erases, 0, "no erase after failed startup scan");
    zassert_equal(observed, marker, "pre-existing simulator data changed");
    zassert_false(fs.ready);
}
ZTEST(nvs_startup, test_mount_initializes_empty_region) {
    zassert_ok(nvs_mount(&fs));
    zassert_true(fs.ready);
    zassert_true(writes > 0, "empty mount writes a GC-done marker");
}
ZTEST(nvs_startup, test_persistent_read_failure) {
    persistent = true;
    expect_read_failure("persistent");
}
ZTEST(nvs_startup, test_first_scan_read_failure) {
    fail_at = 1;
    expect_read_failure("first");
}
ZTEST(nvs_startup, test_second_scan_read_failure) {
    uint8_t closed[sizeof(struct nvs_ate)] = {0};
    zassert_ok(flash_write(real, begin + fs.sector_size - sizeof(closed), closed, sizeof(closed)));
    fail_at = 2;
    expect_read_failure("second");
}
ZTEST_SUITE(nvs_startup, NULL, NULL, before, NULL, NULL);
