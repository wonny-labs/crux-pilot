/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The unmodified Zephyr MMC stack (drivers/disk/mmc_subsys.c, subsys/sd) against the eMMC
 * simulator, with and without a cache. Results show how the stack behaves against a model written
 * from its own needs, not how a device behaves.
 *
 * Every case starts from a freshly initialised card with no faults and zeroed statistics, and new
 * data is derived from what the medium holds, so no case depends on another having run.
 */
#include "emmc_sim.h"

#include <string.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/ztest.h>

#define DISK "MMC"
#define SECTOR_SIZE 512U
#define SECTOR_COUNT 8192U
#define MAX_SECTORS 8U
#define MAX_OFFSET 16U
#define CACHED (CONFIG_CRUX_EMMC_SIM_CACHE_KB > 0)

BUILD_ASSERT(CONFIG_SDHC_BUFFER_ALIGNMENT == 32);

#define AREA_SIZE (MAX_SECTORS * SECTOR_SIZE + MAX_OFFSET)

static uint8_t new_data[AREA_SIZE] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);
static uint8_t old_data[AREA_SIZE] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);
static uint8_t read_data[AREA_SIZE] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);

static struct emmc_sim_stats stats(void)
{
    struct emmc_sim_stats out;

    emmc_sim_get_stats(&out);
    return out;
}

/* Remembers what the medium holds in old_data and makes new contents in `out` that differ from it
 * in every byte, and from sector to sector. */
static void prepare_new_data(uint8_t *out, uint32_t sector, uint32_t count)
{
    zassert_equal(emmc_sim_read_persistent(sector, old_data, count), 0);
    for (uint32_t i = 0; i < count * SECTOR_SIZE; i++) {
        out[i] = old_data[i] ^ (uint8_t)(1U + (sector + i + i / SECTOR_SIZE) % 255U);
    }
}

static void assert_reads_back(uint32_t sector, uint32_t count, const uint8_t *expected)
{
    memset(read_data, 0, sizeof(read_data));
    zassert_equal(disk_access_read(DISK, read_data, sector, count), 0);
    zassert_mem_equal(read_data, expected, count * SECTOR_SIZE);
}

static void assert_persistent(uint32_t sector, uint32_t count, const uint8_t *expected)
{
    memset(read_data, 0, sizeof(read_data));
    zassert_equal(emmc_sim_read_persistent(sector, read_data, count), 0);
    zassert_mem_equal(read_data, expected, count * SECTOR_SIZE);
}

ZTEST(emmc_stack, test_init_reaches_ready)
{
    zassert_equal(disk_access_ioctl(DISK, DISK_IOCTL_CTRL_DEINIT, NULL), 0);
    zassert_equal(disk_access_status(DISK), DISK_STATUS_UNINIT);
    zassert_equal(disk_access_init(DISK), 0);
    zassert_equal(disk_access_status(DISK), DISK_STATUS_OK);
}

ZTEST(emmc_stack, test_geometry_matches_the_model)
{
    uint32_t count = 0;
    uint32_t size = 0;

    zassert_equal(disk_access_ioctl(DISK, DISK_IOCTL_GET_SECTOR_COUNT, &count), 0);
    zassert_equal(disk_access_ioctl(DISK, DISK_IOCTL_GET_SECTOR_SIZE, &size), 0);
    zassert_equal(count, SECTOR_COUNT);
    zassert_equal(size, SECTOR_SIZE);
}

ZTEST(emmc_stack, test_single_and_multi_block_roundtrip)
{
    const uint32_t single = 10;
    const uint32_t multi = 20;
    static uint8_t before[SECTOR_SIZE];
    static uint8_t after[SECTOR_SIZE];

    zassert_equal(disk_access_read(DISK, before, multi - 1, 1), 0);
    zassert_equal(disk_access_read(DISK, after, multi + MAX_SECTORS, 1), 0);

    prepare_new_data(new_data, single, 1);
    zassert_equal(disk_access_write(DISK, new_data, single, 1), 0);
    assert_reads_back(single, 1, new_data);

    prepare_new_data(new_data, multi, MAX_SECTORS);
    zassert_equal(disk_access_write(DISK, new_data, multi, MAX_SECTORS), 0);
    assert_reads_back(multi, MAX_SECTORS, new_data);
    zassert_equal(stats().writes, 2, "a multi-block write is one command");

    assert_reads_back(multi - 1, 1, before);
    assert_reads_back(multi + MAX_SECTORS, 1, after);
}

ZTEST(emmc_stack, test_unaligned_buffer_roundtrip)
{
    const uint32_t sector = 40;
    const uint32_t count = 3;
    uint8_t *source = new_data + 1;
    uint8_t *destination = read_data + 3;

    prepare_new_data(source, sector, count);
    zassert_equal(disk_access_write(DISK, source, sector, count), 0);
    zassert_equal(disk_access_read(DISK, destination, sector, count), 0);
    zassert_mem_equal(destination, source, count * SECTOR_SIZE);
    /* The stack's internal buffer holds one sector, so each sector is a command of its own. */
    zassert_equal(stats().writes, count);
    zassert_equal(stats().reads, count);
}

ZTEST(emmc_stack, test_out_of_range_is_rejected)
{
    const uint32_t last = SECTOR_COUNT - 1;

    zassert_equal(disk_access_write(DISK, new_data, last, 2), -EINVAL);
    zassert_equal(disk_access_read(DISK, read_data, last, 2), -EINVAL);
    zassert_equal(stats().writes, 0);
    zassert_equal(stats().reads, 0);

    prepare_new_data(new_data, last, 1);
    zassert_equal(disk_access_write(DISK, new_data, last, 1), 0, "the last sector is in range");
    assert_reads_back(last, 1, new_data);
}

ZTEST(emmc_stack, test_transient_write_fault_is_retried)
{
    const uint32_t sector = 50;

    prepare_new_data(new_data, sector, 1);
    emmc_sim_fail(EMMC_SIM_OP_WRITE, 1, 1);
    zassert_equal(disk_access_write(DISK, new_data, sector, 1), 0);
    zassert_equal(stats().writes, 1, "the retry is inside one request");
    assert_reads_back(sector, 1, new_data);

    /* The boundary shows that the fault above was real: the host makes retries + 1 attempts. */
    emmc_sim_fail(EMMC_SIM_OP_WRITE, 1, CONFIG_SD_DATA_RETRIES);
    zassert_equal(disk_access_write(DISK, new_data, sector, 1), 0);
    emmc_sim_fail(EMMC_SIM_OP_WRITE, 1, CONFIG_SD_DATA_RETRIES + 1);
    zassert_not_equal(disk_access_write(DISK, new_data, sector, 1), 0);
}

ZTEST(emmc_stack, test_hard_write_fault_returns_nonzero)
{
    const uint32_t first = 60;
    const uint32_t second = 61;

    emmc_sim_fail(EMMC_SIM_OP_WRITE, 2, UINT32_MAX);
    prepare_new_data(new_data, first, 1);
    zassert_equal(disk_access_write(DISK, new_data, first, 1), 0, "only the second write fails");

    /* The stack's own query after a failed write is refused by the model, so the value is not
     * the injected one; what matters is that the failure reaches the caller. */
    prepare_new_data(new_data, second, 1);
    zassert_not_equal(disk_access_write(DISK, new_data, second, 1), 0);
    assert_reads_back(second, 1, old_data);
    assert_persistent(second, 1, old_data);
}

ZTEST(emmc_stack, test_hard_read_fault_returns_nonzero)
{
    emmc_sim_fail(EMMC_SIM_OP_READ, 1, UINT32_MAX);
    zassert_not_equal(disk_access_read(DISK, read_data, 70, 1), 0);
    zassert_equal(stats().reads, 1, "the attempts are inside one request");
    emmc_sim_clear_faults();
    zassert_equal(disk_access_read(DISK, read_data, 70, 1), 0);
}

ZTEST(emmc_stack, test_busy_past_the_timeout_returns_error)
{
    const uint32_t sector = 80;

    prepare_new_data(new_data, sector, 1);
    emmc_sim_hold_busy(true);
    zassert_equal(disk_access_write(DISK, new_data, sector, 1), -ETIMEDOUT);
    zassert_equal(disk_access_ioctl(DISK, DISK_IOCTL_CTRL_SYNC, NULL), -EBUSY);
    emmc_sim_hold_busy(false);
    zassert_equal(disk_access_ioctl(DISK, DISK_IOCTL_CTRL_SYNC, NULL), 0);
}

ZTEST(emmc_stack, test_cache_state_follows_the_profile)
{
    zassert_equal(stats().cache_on, CACHED);
}

ZTEST(emmc_stack, test_reads_see_held_data)
{
    const uint32_t sector = 90;

    prepare_new_data(new_data, sector, 1);
    zassert_equal(disk_access_write(DISK, new_data, sector, 1), 0);
    assert_reads_back(sector, 1, new_data);
    assert_persistent(sector, 1, CACHED ? old_data : new_data);
}

ZTEST(emmc_stack, test_transfers_are_aligned_for_dma)
{
    const uint32_t sector = 100;
    const uint32_t offsets[] = {0, 1, 4, MAX_OFFSET};

    for (size_t i = 0; i < ARRAY_SIZE(offsets); i++) {
        prepare_new_data(new_data + offsets[i], sector, 2);
        zassert_equal(disk_access_write(DISK, new_data + offsets[i], sector, 2), 0);
        zassert_equal(disk_access_read(DISK, read_data + offsets[i], sector, 2), 0);
        zassert_mem_equal(read_data + offsets[i], new_data + offsets[i], 2 * SECTOR_SIZE);
    }
    zassert_equal(stats().misaligned_transfers, 0);

    /* A buffer given directly to the host shows that the counter can move. */
    struct sdhc_command cmd = {
        .opcode = SD_READ_SINGLE_BLOCK,
        .arg = sector,
        .response_type = SD_RSP_TYPE_R1,
    };
    struct sdhc_data data = {
        .block_addr = sector,
        .block_size = SECTOR_SIZE,
        .blocks = 1,
        .data = read_data + 1,
    };
    zassert_equal(sdhc_request(DEVICE_DT_GET(DT_NODELABEL(emmc_sim0)), &cmd, &data), 0);
    zassert_equal(stats().misaligned_transfers, 1);
}

ZTEST(emmc_stack, test_sync_sends_no_flush_at_the_pin)
{
    const uint32_t sector = 110;

    prepare_new_data(new_data, sector, 1);
    zassert_equal(disk_access_write(DISK, new_data, sector, 1), 0);
    zassert_equal(disk_access_ioctl(DISK, DISK_IOCTL_CTRL_SYNC, NULL), 0);
    zassert_equal(stats().flush_commands, 0);
    assert_persistent(sector, 1, CACHED ? old_data : new_data);
}

static void before_each(void *fixture)
{
    ARG_UNUSED(fixture);
    emmc_sim_clear_faults();
    zassert_equal(disk_access_init(DISK), 0);
    emmc_sim_reset_stats();
}

static void after_each(void *fixture)
{
    ARG_UNUSED(fixture);
    /* The next init cycles power, so held data does not carry into the next case. */
    (void)disk_access_ioctl(DISK, DISK_IOCTL_CTRL_DEINIT, NULL);
}

ZTEST_SUITE(emmc_stack, NULL, NULL, before_each, after_each, NULL);
