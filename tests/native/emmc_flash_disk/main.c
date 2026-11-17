/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The flash-API driver over the MMC disk (zephyr/drivers/flash/flash_disk.c) against the eMMC
 * simulator, with and without a cache. The cases show how the driver behaves against a model
 * written from the needs of the Zephyr MMC stack. They are not evidence about an eMMC device.
 *
 * Every case starts with no faults and zeroed statistics, and uses a region of the medium of its
 * own, so no case depends on another having run.
 */
#include "emmc_sim.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#define DISK "MMC"
#define SECTOR 512U
#define PAGE 4096U
#define SECTORS CONFIG_CRUX_EMMC_SIM_SECTORS
#define MEDIUM_SIZE ((size_t)SECTORS * SECTOR)
#define FLASH_DEV DEVICE_DT_GET(DT_NODELABEL(mmcflash))
#define CACHED (CONFIG_CRUX_EMMC_SIM_CACHE_KB > 0)
#define READ_ONLY IS_ENABLED(CONFIG_CRUX_FLASH_DISK_READ_ONLY)
#define FLUSH_CONTROL IS_ENABLED(CONFIG_CRUX_FLASH_DISK_FLUSH_CONTROL)
#define SDHC_ALIGN CONFIG_SDHC_BUFFER_ALIGNMENT
/* Whether a returned write or erase is on the medium: not in the control that skips the flush. */
#define ON_MEDIUM_AFTER_CALL (!(FLUSH_CONTROL && CACHED))

/* Regions that the cases use, in bytes. Slots 0 and 1 are the partitions of the overlay. */
#define SLOT1_OFFSET 0x8000U
#define SLOT1_SIZE 0x8000U
#define REGION_READ 0x20000U
#define REGION_WRITE 0x50000U
#define REGION_ERASE 0x60000U
#define REGION_FLUSH 0x70000U
#define REGION_ERRORS 0x80000U
#define REGION_NEVER_WRITTEN 0x180000U

BUILD_ASSERT(CONFIG_SDHC_BUFFER_ALIGNMENT == 32);

/* 140 sectors: more than one direct read of CONFIG_CRUX_FLASH_DISK_DIRECT_SECTORS (128). */
#define AREA (140U * SECTOR)
#define DIRECT CONFIG_CRUX_FLASH_DISK_DIRECT_SECTORS
BUILD_ASSERT(DIRECT == 128, "the direct-read cases are written for a limit of 128 sectors");
BUILD_ASSERT(CONFIG_CRUX_FLASH_DISK_BUFFER_SECTORS == 8,
             "the bounce cases are written for a bounce buffer of 8 sectors");
static uint8_t expected[AREA] __aligned(SDHC_ALIGN);
/* Room for a sentinel byte after the longest read at the largest destination shift. */
static uint8_t actual[AREA + 64U] __aligned(SDHC_ALIGN);
static uint8_t source[AREA + 1U] __aligned(SDHC_ALIGN);

static struct emmc_sim_stats stats(void)
{
    struct emmc_sim_stats out;

    emmc_sim_get_stats(&out);
    return out;
}

static void pattern(uint8_t *out, size_t len, uint32_t seed)
{
    for (size_t i = 0; i < len; i++) {
        out[i] = (uint8_t)(seed * 131U + i * 7U + i / SECTOR);
    }
}

/* Puts known content on the medium through the disk layer, behind the driver's back, and makes
 * the cache drain it so that the persistent state and the stack agree. */
static void seed_medium(uint32_t byte_offset, size_t len, uint32_t seed)
{
    pattern(expected, len, seed);
    zassert_equal(disk_access_write(DISK, expected, byte_offset / SECTOR, len / SECTOR), 0);
    zassert_equal(disk_access_ioctl(DISK, DISK_IOCTL_CTRL_SYNC, NULL), 0);
}

static void persistent(uint32_t byte_offset, size_t len, uint8_t *out)
{
    zassert_equal(emmc_sim_read_persistent(byte_offset / SECTOR, out, len / SECTOR), 0);
}

static void before(void *unused)
{
    ARG_UNUSED(unused);
    emmc_sim_clear_faults();
    emmc_sim_reset_stats();
}

static void skip_if_read_only(void)
{
    if (READ_ONLY) {
        ztest_test_skip();
    }
}

ZTEST(emmc_flash_disk, test_parameters_and_page_layout)
{
    const struct device *dev = FLASH_DEV;
    const struct flash_parameters *params = flash_get_parameters(dev);
    struct flash_pages_info info;
    uint64_t size = 0;

    zassert_true(device_is_ready(dev));
    zassert_equal(params->write_block_size, SECTOR);
    zassert_equal(params->erase_value, 0xFF, "the driver reports 0xFF, not what the medium reads");
    zassert_equal(flash_params_get_erase_cap(params), FLASH_ERASE_C_EXPLICIT);
    zassert_equal(flash_get_size(dev, &size), 0);
    zassert_equal(size, MEDIUM_SIZE);
    zassert_equal(flash_get_page_count(dev), MEDIUM_SIZE / PAGE);
    zassert_equal(flash_get_page_info_by_offs(dev, 0x9000, &info), 0);
    zassert_equal(info.start_offset, 0x9000);
    zassert_equal(info.size, PAGE);
    zassert_equal(info.index, 9);
    zassert_equal(flash_get_page_info_by_offs(dev, MEDIUM_SIZE, &info), -EINVAL);
}

ZTEST(emmc_flash_disk, test_flash_map_describes_the_slots)
{
    const struct flash_area *fa;

    zassert_equal(flash_area_open(PARTITION_ID(emmc_slot1), &fa), 0);
    zassert_equal_ptr(flash_area_get_device(fa), FLASH_DEV);
    zassert_equal(fa->fa_off, SLOT1_OFFSET);
    zassert_equal(fa->fa_size, SLOT1_SIZE);
    zassert_equal(flash_area_align(fa), SECTOR);
    zassert_equal(flash_area_erased_val(fa), 0xFF);
    flash_area_close(fa);
}

ZTEST(emmc_flash_disk, test_unwritten_medium_reads_zero_not_the_reported_erase_value)
{
    memset(actual, 0x5A, SECTOR);
    zassert_equal(flash_read(FLASH_DEV, REGION_NEVER_WRITTEN, actual, SECTOR), 0);
    for (size_t i = 0; i < SECTOR; i++) {
        zassert_equal(actual[i], 0x00, "byte %zu", i);
    }
}

ZTEST(emmc_flash_disk, test_read_has_no_alignment_restrictions)
{
    static const struct {
        uint32_t offset;
        size_t len;
        size_t dest_shift;
    } cases[] = {
        {0, SECTOR, 0},       {0, SECTOR, 1},     {1, 1, 0},      {1, SECTOR, 3},
        {SECTOR - 1, 2, 0},   {SECTOR - 1, 2, 5}, {100, 3000, 0}, {100, 3000, 7},
        {SECTOR, 4096, 0},    {SECTOR, 4096, 13}, {3, 4093, 31},  {0, AREA - 1, 1},
        {511, AREA - 511, 0}, {2047, 8193, 17},
    };

    seed_medium(REGION_READ, AREA, 7);
    emmc_sim_reset_stats();
    for (size_t c = 0; c < ARRAY_SIZE(cases); c++) {
        size_t len = cases[c].len;
        uint8_t *dest = &actual[cases[c].dest_shift];

        memset(actual, 0xEE, sizeof(actual));
        zassert_equal(flash_read(FLASH_DEV, REGION_READ + cases[c].offset, dest, len), 0,
                      "case %zu", c);
        zassert_mem_equal(dest, &expected[cases[c].offset], len, "case %zu", c);
        /* Nothing before or after the destination is touched. */
        if (cases[c].dest_shift != 0) {
            zassert_equal(actual[cases[c].dest_shift - 1], 0xEE, "case %zu", c);
        }
        zassert_equal(dest[len], 0xEE, "case %zu", c);
    }
    zassert_equal(stats().writes, 0, "a read writes nothing");
    zassert_equal(stats().flush_commands, 0, "a read flushes nothing");
}

ZTEST(emmc_flash_disk, test_read_rejects_ranges_outside_the_device)
{
    zassert_equal(flash_read(FLASH_DEV, MEDIUM_SIZE - 1, actual, 2), -EINVAL);
    zassert_equal(flash_read(FLASH_DEV, MEDIUM_SIZE, actual, 1), -EINVAL);
    zassert_equal(flash_read(FLASH_DEV, -1, actual, 1), -EINVAL);
    zassert_equal(flash_read(FLASH_DEV, 1, actual, SIZE_MAX), -EINVAL, "offset + len wraps");
    zassert_equal(stats().reads, 0);
}

ZTEST(emmc_flash_disk, test_write_round_trips_from_any_source_address)
{
    static const struct {
        uint32_t offset;
        size_t sectors;
        size_t src_shift;
    } cases[] = {
        {0, 1, 0}, {SECTOR, 1, 1}, {4 * SECTOR, 8, 0}, {13 * SECTOR, 9, 3}, {32 * SECTOR, 64, 5},
    };

    skip_if_read_only();
    for (size_t c = 0; c < ARRAY_SIZE(cases); c++) {
        size_t len = cases[c].sectors * SECTOR;
        uint32_t at = REGION_WRITE + cases[c].offset;

        pattern(&source[cases[c].src_shift], len, 100U + c);
        zassert_equal(flash_write(FLASH_DEV, at, &source[cases[c].src_shift], len), 0, "case %zu",
                      c);
        zassert_equal(flash_read(FLASH_DEV, at, actual, len), 0);
        zassert_mem_equal(actual, &source[cases[c].src_shift], len, "case %zu", c);
        if (ON_MEDIUM_AFTER_CALL) {
            persistent(at, len, actual);
            zassert_mem_equal(actual, &source[cases[c].src_shift], len,
                              "case %zu is on the medium when the call returns", c);
        }
    }
}

ZTEST(emmc_flash_disk, test_write_rejects_misaligned_and_out_of_range_access)
{
    static const struct {
        off_t offset;
        size_t len;
    } cases[] = {
        {1, SECTOR},           {SECTOR + 8, SECTOR},     {0, 1},
        {0, SECTOR - 1},       {0, SECTOR + 8},          {MEDIUM_SIZE - SECTOR, 2 * SECTOR},
        {MEDIUM_SIZE, SECTOR}, {-(off_t)SECTOR, SECTOR}, {SECTOR, SIZE_MAX - SECTOR + 1U},
    };
    uint8_t before_image[SECTOR];

    skip_if_read_only();
    seed_medium(REGION_WRITE, SECTOR, 3);
    memcpy(before_image, expected, SECTOR);
    emmc_sim_reset_stats();
    for (size_t c = 0; c < ARRAY_SIZE(cases); c++) {
        zassert_equal(flash_write(FLASH_DEV, cases[c].offset, source, cases[c].len), -EINVAL,
                      "case %zu", c);
    }
    zassert_equal(stats().writes, 0, "no command is issued for a rejected call");
    zassert_equal(stats().flush_commands, 0, "and none is flushed");
    persistent(REGION_WRITE, SECTOR, actual);
    zassert_mem_equal(actual, before_image, SECTOR);
}

ZTEST(emmc_flash_disk, test_erase_writes_the_erase_value_over_whole_pages)
{
    skip_if_read_only();
    seed_medium(REGION_ERASE - PAGE, 4U * PAGE, 9);
    zassert_equal(flash_erase(FLASH_DEV, REGION_ERASE, 2U * PAGE), 0);

    if (ON_MEDIUM_AFTER_CALL) {
        persistent(REGION_ERASE - PAGE, 4U * PAGE, actual);
        for (size_t i = 0; i < 4U * PAGE; i++) {
            bool erased = i >= PAGE && i < 3U * PAGE;

            if (erased) {
                zassert_equal(actual[i], 0xFF, "byte %zu is erased", i);
            } else {
                zassert_equal(actual[i], expected[i], "byte %zu outside the range is kept", i);
            }
        }
    }
    memset(actual, 0, 2U * PAGE);
    zassert_equal(flash_read(FLASH_DEV, REGION_ERASE, actual, 2U * PAGE), 0);
    for (size_t i = 0; i < 2U * PAGE; i++) {
        zassert_equal(actual[i], 0xFF);
    }
}

ZTEST(emmc_flash_disk, test_erase_rejects_misaligned_and_out_of_range_access)
{
    static const struct {
        off_t offset;
        size_t len;
    } cases[] = {
        {SECTOR, PAGE},
        {0, PAGE - SECTOR},
        {0, 0},
        {1, PAGE},
        {MEDIUM_SIZE - PAGE, 2U * PAGE},
        {MEDIUM_SIZE, PAGE},
        {-(off_t)PAGE, PAGE},
        {PAGE, SIZE_MAX - PAGE + 1U},
    };

    skip_if_read_only();
    seed_medium(REGION_ERASE, PAGE, 4);
    emmc_sim_reset_stats();
    for (size_t c = 0; c < ARRAY_SIZE(cases); c++) {
        zassert_equal(flash_erase(FLASH_DEV, cases[c].offset, cases[c].len), -EINVAL, "case %zu",
                      c);
    }
    zassert_equal(stats().writes, 0);
    zassert_equal(stats().flush_commands, 0);
    persistent(REGION_ERASE, PAGE, actual);
    zassert_mem_equal(actual, expected, PAGE);
}

ZTEST(emmc_flash_disk, test_each_write_and_erase_call_is_flushed_before_it_returns)
{
    struct emmc_sim_stats before_stats;

    skip_if_read_only();
    if (FLUSH_CONTROL) {
        ztest_test_skip();
    }
    pattern(source, 3U * SECTOR, 21);
    zassert_equal(flash_write(FLASH_DEV, REGION_FLUSH, source, 3U * SECTOR), 0);
    zassert_equal(stats().flush_commands, CACHED ? 1U : 0U, "one flush for a multi-sector write");
    persistent(REGION_FLUSH, 3U * SECTOR, actual);
    zassert_mem_equal(actual, source, 3U * SECTOR);

    before_stats = stats();
    zassert_equal(flash_erase(FLASH_DEV, REGION_FLUSH, PAGE), 0);
    zassert_equal(stats().flush_commands - before_stats.flush_commands, CACHED ? 1U : 0U,
                  "one flush for the erase of one page");

    before_stats = stats();
    zassert_equal(flash_erase(FLASH_DEV, REGION_FLUSH, 4U * PAGE), 0);
    zassert_equal(stats().flush_commands - before_stats.flush_commands, CACHED ? 2U : 0U,
                  "two for several pages: one after the first chunk, so that the first page is "
                  "on the medium before the rest, and one at the end, not one for each");
    persistent(REGION_FLUSH, 4U * PAGE, actual);
    for (size_t i = 0; i < 4U * PAGE; i++) {
        zassert_equal(actual[i], 0xFF);
    }

    before_stats = stats();
    zassert_equal(flash_read(FLASH_DEV, REGION_FLUSH, actual, PAGE), 0);
    zassert_equal(stats().flush_commands, before_stats.flush_commands);
    zassert_equal(stats().writes, before_stats.writes);
}

ZTEST(emmc_flash_disk, test_without_the_flush_a_returned_write_is_still_in_the_cache)
{
    if (!FLUSH_CONTROL || !CACHED) {
        ztest_test_skip();
    }
    seed_medium(REGION_FLUSH, SECTOR, 5);
    emmc_sim_reset_stats();
    pattern(source, SECTOR, 22);
    zassert_equal(flash_write(FLASH_DEV, REGION_FLUSH, source, SECTOR), 0);
    zassert_equal(stats().flush_commands, 0);
    persistent(REGION_FLUSH, SECTOR, actual);
    zassert_mem_equal(actual, expected, SECTOR, "the medium still holds the old sector");
    zassert_true(memcmp(actual, source, SECTOR) != 0);
    zassert_equal(flash_read(FLASH_DEV, REGION_FLUSH, actual, SECTOR), 0);
    zassert_mem_equal(actual, source, SECTOR, "a read sees the held write");
}

ZTEST(emmc_flash_disk, test_read_only_driver_refuses_write_and_erase)
{
    const struct flash_area *fa;

    if (!READ_ONLY) {
        ztest_test_skip();
    }
    seed_medium(REGION_WRITE, SECTOR, 6);
    emmc_sim_reset_stats();
    zassert_equal(flash_write(FLASH_DEV, REGION_WRITE, source, SECTOR), -ENOTSUP);
    zassert_equal(flash_erase(FLASH_DEV, REGION_WRITE, PAGE), -ENOTSUP);
    zassert_equal(flash_area_open(PARTITION_ID(emmc_slot1), &fa), 0);
    zassert_equal(flash_area_write(fa, 0, source, SECTOR), -ENOTSUP);
    zassert_equal(flash_area_erase(fa, 0, PAGE), -ENOTSUP);
    flash_area_close(fa);
    zassert_equal(stats().writes, 0);
    zassert_equal(stats().flush_commands, 0);
    zassert_equal(flash_read(FLASH_DEV, REGION_WRITE, actual, SECTOR), 0);
    zassert_mem_equal(actual, expected, SECTOR);
}

ZTEST(emmc_flash_disk, test_slot_through_the_flash_map_erases_writes_and_reads)
{
    const struct flash_area *fa;

    skip_if_read_only();
    zassert_equal(flash_area_open(PARTITION_ID(emmc_slot1), &fa), 0);
    zassert_equal(flash_area_erase(fa, 0, SLOT1_SIZE), 0);
    zassert_equal(flash_area_read(fa, 100, actual, 3000), 0);
    for (size_t i = 0; i < 3000U; i++) {
        zassert_equal(actual[i], 0xFF);
    }
    pattern(source, 2U * SECTOR, 31);
    zassert_equal(flash_area_write(fa, SLOT1_SIZE - 2U * SECTOR, source, 2U * SECTOR), 0);
    zassert_equal(flash_area_read(fa, SLOT1_SIZE - 2U * SECTOR, actual, 2U * SECTOR), 0);
    zassert_mem_equal(actual, source, 2U * SECTOR);
    if (ON_MEDIUM_AFTER_CALL) {
        persistent(SLOT1_OFFSET + SLOT1_SIZE - 2U * SECTOR, 2U * SECTOR, actual);
        zassert_mem_equal(actual, source, 2U * SECTOR, "partition offsets are added once");
    }
    zassert_equal(flash_area_write(fa, SLOT1_SIZE - SECTOR, source, 2U * SECTOR), -EINVAL,
                  "a write past the end of the partition is refused by the flash map");
    flash_area_close(fa);
}

ZTEST(emmc_flash_disk, test_a_failed_command_fails_the_call_stops_it_and_is_not_flushed)
{
    skip_if_read_only();
    pattern(source, 64U * SECTOR, 41);

    emmc_sim_fail(EMMC_SIM_OP_WRITE, 1, UINT32_MAX);
    zassert_equal(flash_write(FLASH_DEV, REGION_ERRORS, source, SECTOR), -EIO);
    zassert_equal(stats().flush_commands, 0, "a failed command is not flushed");
    emmc_sim_clear_faults();

    /* The second command of a long write fails: the driver stops there, and the call is not
     * flushed, so it is not acknowledged. */
    emmc_sim_reset_stats();
    emmc_sim_fail(EMMC_SIM_OP_WRITE, 2, UINT32_MAX);
    zassert_equal(flash_write(FLASH_DEV, REGION_ERRORS, source, 64U * SECTOR), -EIO);
    zassert_equal(stats().writes, 2, "no command follows the failed one");
    zassert_equal(stats().flush_commands, 0);
    emmc_sim_clear_faults();
    zassert_equal(flash_write(FLASH_DEV, REGION_ERRORS, source, 64U * SECTOR), 0,
                  "the lock is released and the device works again");

    /* An erase that fails on its first chunk issues nothing more and no flush. */
    emmc_sim_reset_stats();
    emmc_sim_fail(EMMC_SIM_OP_WRITE, 1, UINT32_MAX);
    zassert_equal(flash_erase(FLASH_DEV, REGION_ERRORS, 16U * PAGE), -EIO);
    zassert_equal(stats().writes, 1);
    zassert_equal(stats().flush_commands, 0);
    emmc_sim_clear_faults();
    zassert_equal(flash_erase(FLASH_DEV, REGION_ERRORS, PAGE), 0);
}

ZTEST(emmc_flash_disk, test_a_multi_page_erase_puts_its_first_page_on_the_medium_first)
{
    uint8_t first_page[PAGE];

    skip_if_read_only();
    if (!CACHED || FLUSH_CONTROL) {
        ztest_test_skip();
    }
    seed_medium(REGION_ERRORS, 4U * PAGE, 12);
    emmc_sim_reset_stats();
    /* The third chunk fails: the first has been flushed, the second is held in the cache. */
    emmc_sim_fail(EMMC_SIM_OP_WRITE, 3, UINT32_MAX);
    zassert_equal(flash_erase(FLASH_DEV, REGION_ERRORS, 4U * PAGE), -EIO);
    zassert_equal(stats().flush_commands, 1);
    persistent(REGION_ERRORS, PAGE, first_page);
    for (size_t i = 0; i < PAGE; i++) {
        zassert_equal(first_page[i], 0xFF, "the first page is erased on the medium");
    }
    persistent(REGION_ERRORS + PAGE, PAGE, first_page);
    zassert_mem_equal(first_page, &expected[PAGE], PAGE, "the second is not, it is only held");
    emmc_sim_clear_faults();
}

ZTEST(emmc_flash_disk, test_a_failed_flush_fails_the_call)
{
    skip_if_read_only();
    if (!CACHED || FLUSH_CONTROL) {
        ztest_test_skip();
    }
    pattern(source, SECTOR, 42);
    emmc_sim_fail(EMMC_SIM_OP_FLUSH, 1, UINT32_MAX);
    zassert_equal(flash_write(FLASH_DEV, REGION_ERRORS, source, SECTOR), -EIO,
                  "the data is not on the medium, so the call must not succeed");
    emmc_sim_clear_faults();

    emmc_sim_fail(EMMC_SIM_OP_FLUSH, 1, UINT32_MAX);
    zassert_equal(flash_erase(FLASH_DEV, REGION_ERRORS, PAGE), -EIO);
    emmc_sim_clear_faults();
}

ZTEST(emmc_flash_disk, test_a_failed_read_fails_the_call)
{
    seed_medium(REGION_READ, PAGE, 8);
    emmc_sim_fail(EMMC_SIM_OP_READ, 1, UINT32_MAX);
    zassert_equal(flash_read(FLASH_DEV, REGION_READ, actual, PAGE), -EIO);
    emmc_sim_clear_faults();
    zassert_equal(flash_read(FLASH_DEV, REGION_READ, actual, PAGE), 0);
    zassert_mem_equal(actual, expected, PAGE);
}

ZTEST(emmc_flash_disk, test_a_failed_read_in_the_middle_of_a_long_read_stops_it)
{
    seed_medium(REGION_READ, 64U * SECTOR, 8);
    emmc_sim_reset_stats();
    emmc_sim_fail(EMMC_SIM_OP_READ, 2, UINT32_MAX);
    /* An unaligned destination takes the bounce path, 8 sectors a command. */
    zassert_equal(flash_read(FLASH_DEV, REGION_READ, &actual[1], 64U * SECTOR), -EIO);
    zassert_equal(stats().reads, 2, "no command follows the failed one");
    emmc_sim_clear_faults();
    zassert_equal(flash_read(FLASH_DEV, REGION_READ, &actual[1], 64U * SECTOR), 0);
    zassert_mem_equal(&actual[1], expected, 64U * SECTOR);
}

ZTEST(emmc_flash_disk, test_direct_reads_are_limited_and_the_tail_goes_through_the_buffer)
{
    static const struct {
        size_t len;
        uint32_t commands;
    } cases[] = {
        {64U * SECTOR, 1},                  /* direct */
        {64U * SECTOR + 1U, 2},             /* direct, then one buffered sector for the tail */
        {DIRECT * SECTOR, 1},               /* exactly the limit */
        {(DIRECT + 1U) * SECTOR, 2},        /* one past it */
        {(DIRECT + 2U) * SECTOR + 100U, 3}, /* two direct reads and a buffered tail */
    };

    seed_medium(REGION_READ, AREA, 11);
    for (size_t c = 0; c < ARRAY_SIZE(cases); c++) {
        emmc_sim_reset_stats();
        memset(actual, 0xEE, sizeof(actual));
        zassert_equal(flash_read(FLASH_DEV, REGION_READ, actual, cases[c].len), 0, "case %zu", c);
        zassert_mem_equal(actual, expected, cases[c].len, "case %zu", c);
        zassert_equal(actual[cases[c].len], 0xEE, "case %zu", c);
        zassert_equal(stats().reads, cases[c].commands, "case %zu", c);
    }
}

ZTEST(emmc_flash_disk, test_buffered_reads_move_a_bounce_buffer_at_a_time)
{
    static const struct {
        size_t len;
        uint32_t commands;
    } cases[] = {
        {8U * SECTOR, 1},
        {8U * SECTOR + 1U, 2},
        {9U * SECTOR, 2},
        {16U * SECTOR, 2},
    };

    seed_medium(REGION_READ, AREA, 13);
    for (size_t c = 0; c < ARRAY_SIZE(cases); c++) {
        emmc_sim_reset_stats();
        memset(actual, 0xEE, sizeof(actual));
        /* The destination is not aligned for the disk layer, so the driver cannot read into it. */
        zassert_equal(flash_read(FLASH_DEV, REGION_READ, &actual[1], cases[c].len), 0, "case %zu",
                      c);
        zassert_mem_equal(&actual[1], expected, cases[c].len, "case %zu", c);
        zassert_equal(stats().reads, cases[c].commands, "case %zu", c);
    }
}

ZTEST(emmc_flash_disk, test_access_succeeds_up_to_the_last_byte_of_the_device)
{
    uint8_t byte = 0;
    uint8_t last_sector[SECTOR];

    /* Reads: the last byte, the last sector through the buffer, and an empty read at the end. */
    seed_medium(MEDIUM_SIZE - SECTOR, SECTOR, 14);
    memcpy(last_sector, expected, SECTOR);
    zassert_equal(flash_read(FLASH_DEV, MEDIUM_SIZE - 1U, &byte, 1), 0);
    zassert_equal(byte, last_sector[SECTOR - 1U]);
    zassert_equal(flash_read(FLASH_DEV, MEDIUM_SIZE - SECTOR, &actual[1], SECTOR), 0);
    zassert_mem_equal(&actual[1], last_sector, SECTOR);
    zassert_equal(flash_read(FLASH_DEV, MEDIUM_SIZE, actual, 0), 0);
    zassert_equal(flash_read(FLASH_DEV, MEDIUM_SIZE + 1U, actual, 0), -EINVAL);

    if (READ_ONLY) {
        return;
    }
    /* Writes and erases: the last sector and the last page. */
    pattern(source, SECTOR, 15);
    zassert_equal(flash_write(FLASH_DEV, MEDIUM_SIZE - SECTOR, source, SECTOR), 0);
    zassert_equal(flash_read(FLASH_DEV, MEDIUM_SIZE - SECTOR, actual, SECTOR), 0);
    zassert_mem_equal(actual, source, SECTOR);
    zassert_equal(flash_erase(FLASH_DEV, MEDIUM_SIZE - PAGE, PAGE), 0);
    zassert_equal(flash_read(FLASH_DEV, MEDIUM_SIZE - PAGE, actual, PAGE), 0);
    for (size_t i = 0; i < PAGE; i++) {
        zassert_equal(actual[i], 0xFF);
    }
}

ZTEST(emmc_flash_disk, test_a_write_of_nothing_or_from_no_buffer_is_refused)
{
    skip_if_read_only();
    seed_medium(REGION_WRITE, SECTOR, 16);
    emmc_sim_reset_stats();
    zassert_equal(flash_write(FLASH_DEV, REGION_WRITE, source, 0), -EINVAL);
    /* A NULL source must not turn a write into an erase. */
    zassert_equal(flash_write(FLASH_DEV, REGION_WRITE, NULL, SECTOR), -EINVAL);
    zassert_equal(stats().writes, 0);
    zassert_equal(stats().flush_commands, 0);
    persistent(REGION_WRITE, SECTOR, actual);
    zassert_mem_equal(actual, expected, SECTOR);
}

ZTEST_SUITE(emmc_flash_disk, NULL, NULL, before, NULL, NULL);
