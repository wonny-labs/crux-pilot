/* SPDX-License-Identifier: Apache-2.0 */
/*
 * A native_sim SD host driver that behaves as one eMMC device, so that the unmodified Zephyr MMC
 * stack runs without hardware. Register values and commands are those the stack needs at the pin;
 * any other command is refused, so that a stack change is visible. There is no modelled timing:
 * after CMD6 and after a write the card is busy for one sdhc_card_busy poll, however long the work.
 *
 * A write held in the cache stays held until a flush, whatever the cache size says, and power-off
 * drops it. That is the unfavourable case for software, not a measurement of a device.
 */
#define DT_DRV_COMPAT crux_emmc_sim

#include "emmc_sim.h"
#include "emmc_sim_bottom.h"

#include <cmdline.h>
#include <errno.h>
#include <posix_native_task.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1, "the state below is for one device");

#define SECTOR_SIZE 512U
#define SECTORS CONFIG_CRUX_EMMC_SIM_SECTORS

#define OCR (MMC_OCR_PWR_BUSY_FLAG | MMC_OCR_SECTOR_MODE | MMC_OCR_VDD27_36FLAG)
#define CSD_STRUCTURE_3 (3U << 30)
#define CSD_XFER_RATE (MMC_MAXFREQ_10MHZ | MMC_MAXFREQ_MULT_26)

#define STATUS_SWITCH_ERROR BIT(7)
#define STATUS_TRAN (SD_R1_RDY_DATA | (SDMMC_R1_TRANSFER << 9))
#define STATUS_NOT_READY (SDMMC_R1_PROGRAM << 9)

#define SWITCH_INDEX(arg) (((arg) >> 16) & 0xFFU)
#define SWITCH_VALUE(arg) (((arg) >> 8) & 0xFFU)

/* EXT_CSD byte offsets. */
#define EXT_CSD_FLUSH_CACHE 32U
#define EXT_CSD_CACHE_CTRL 33U
#define EXT_CSD_BUS_WIDTH 183U
#define EXT_CSD_HS_TIMING 185U
#define EXT_CSD_POWER_CLASS 187U
#define EXT_CSD_REV 192U
#define EXT_CSD_DEVICE_TYPE 196U
#define EXT_CSD_SEC_COUNT 212U
#define EXT_CSD_CACHE_SIZE 249U

#define EXT_CSD_REV_VALUE 8U
#define EXT_CSD_DEVICE_TYPE_HS 0x03U /* HS 52 and HS 26 */

enum trace_type { TRACE_WRITE = 1, TRACE_FLUSH = 2, TRACE_CACHE_ON = 3, TRACE_MARK = 4 };

struct fault {
    uint32_t skip;     /* operations to let through first */
    uint32_t attempts; /* tries still to fail, UINT32_MAX for ever */
};

static uint8_t persistent[SECTORS * SECTOR_SIZE];
static uint8_t shadow[SECTORS * SECTOR_SIZE];
static ATOMIC_DEFINE(dirty, SECTORS);

static uint8_t ext_csd[MMC_EXT_CSD_BYTES];
static uint32_t cache_kb = CONFIG_CRUX_EMMC_SIM_CACHE_KB;
static bool cache_on;
static bool switch_error;

static struct fault op_faults[3];
static struct fault command_fault;
static uint8_t command_fault_opcode;
static bool reject_next_switch;
static bool busy_held;
static uint32_t busy_polls;
static uint32_t not_ready_reads;
static uint32_t busy_polls_after_switch;
static uint32_t not_ready_reads_after_switch;

static struct emmc_sim_stats stats;

static const char *image_path;
static const char *trace_path;
static uint32_t cache_kb_option; /* the parser sets UINT32_MAX when the option is not given */

static void add_options(void)
{
    static struct args_struct_t options[] = {
        {.option = "emmc-image",
         .name = "path",
         .type = 's',
         .dest = (void *)&image_path,
         .descript = "Host file that holds the persistent state of the eMMC simulator"},
        {.option = "emmc-trace",
         .name = "path",
         .type = 's',
         .dest = (void *)&trace_path,
         .descript = "Host file that records writes, flushes, cache control and marks"},
        {.option = "emmc-cache-kb",
         .name = "kb",
         .type = 'u',
         .dest = (void *)&cache_kb_option,
         .descript = "Cache size of the eMMC simulator in KiB, zero for no cache"},
        ARG_TABLE_ENDMARKER,
    };

    native_add_command_line_opts(options);
}
NATIVE_TASK(add_options, PRE_BOOT_1, 1);

/* Uses up one of a count, of which UINT32_MAX never runs out. */
static bool consume(uint32_t *count)
{
    if (*count == 0) {
        return false;
    }
    if (*count != UINT32_MAX) {
        (*count)--;
    }
    return true;
}

static void trace_record(enum trace_type type, uint32_t sector, uint32_t count, uint32_t id,
                         const void *sectors)
{
    uint32_t header[] = {sys_cpu_to_le32(type), sys_cpu_to_le32(sector), sys_cpu_to_le32(count),
                         sys_cpu_to_le32(id)};

    emmc_sim_bottom_append_trace(header, sizeof(header));
    if (sectors != NULL) {
        emmc_sim_bottom_append_trace(sectors, count * SECTOR_SIZE);
    }
}

static void persist(uint32_t sector, const void *from, uint32_t count)
{
    memcpy(&persistent[sector * SECTOR_SIZE], from, count * SECTOR_SIZE);
    emmc_sim_bottom_store_image(sector * SECTOR_SIZE, from, count * SECTOR_SIZE);
}

static void power_off(void)
{
    memset(dirty, 0, sizeof(dirty));
    cache_on = false;
    switch_error = false;
    memset(ext_csd, 0, sizeof(ext_csd));
    sys_put_le32(SECTORS, &ext_csd[EXT_CSD_SEC_COUNT]);
    ext_csd[EXT_CSD_DEVICE_TYPE] = EXT_CSD_DEVICE_TYPE_HS;
    ext_csd[EXT_CSD_REV] = EXT_CSD_REV_VALUE;
    sys_put_le32(cache_kb, &ext_csd[EXT_CSD_CACHE_SIZE]);
}

static void flush_cache(void)
{
    trace_record(TRACE_FLUSH, 0, 0, 0, NULL);
    for (uint32_t sector = 0; sector < SECTORS; sector++) {
        if (atomic_test_and_clear_bit(dirty, sector)) {
            persist(sector, &shadow[sector * SECTOR_SIZE], 1);
        }
    }
}

static bool apply_switch(uint32_t index, uint32_t value)
{
    switch (index) {
    case EXT_CSD_BUS_WIDTH:
    case EXT_CSD_HS_TIMING:
    case EXT_CSD_POWER_CLASS:
        ext_csd[index] = value;
        return true;
    case EXT_CSD_CACHE_CTRL:
        /* The stack only turns the cache on; turning it off with held data is not modelled. */
        if (value != 1U) {
            return false;
        }
        ext_csd[index] = value;
        cache_on = sys_get_le32(&ext_csd[EXT_CSD_CACHE_SIZE]) != 0U;
        if (cache_on) {
            trace_record(TRACE_CACHE_ON, 0, 0, 0, NULL);
        }
        return true;
    case EXT_CSD_FLUSH_CACHE:
        if (value != 1U) {
            return false;
        }
        flush_cache();
        return true;
    default:
        return false;
    }
}

static void card_switch(uint32_t arg)
{
    if (reject_next_switch || !apply_switch(SWITCH_INDEX(arg), SWITCH_VALUE(arg))) {
        switch_error = true;
    }
    reject_next_switch = false;
    if (busy_polls_after_switch != 0U) {
        busy_polls = busy_polls_after_switch;
        busy_polls_after_switch = 0;
    }
    if (not_ready_reads_after_switch != 0U) {
        not_ready_reads = not_ready_reads_after_switch;
        not_ready_reads_after_switch = 0;
    }
    busy_polls = MAX(busy_polls, 1U);
}

static uint32_t read_status(void)
{
    uint32_t status = consume(&not_ready_reads) ? STATUS_NOT_READY : STATUS_TRAN;

    if (switch_error) {
        status |= STATUS_SWITCH_ERROR;
        switch_error = false;
    }
    return status;
}

static void read_sectors(uint32_t sector, uint8_t *to, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *held = atomic_test_bit(dirty, sector + i) ? shadow : persistent;

        memcpy(&to[i * SECTOR_SIZE], &held[(sector + i) * SECTOR_SIZE], SECTOR_SIZE);
    }
}

static void write_sectors(uint32_t sector, const uint8_t *from, uint32_t count)
{
    trace_record(TRACE_WRITE, sector, count, 0, from);
    busy_polls = MAX(busy_polls, 1U);
    if (!cache_on) {
        persist(sector, from, count);
        return;
    }
    memcpy(&shadow[sector * SECTOR_SIZE], from, count * SECTOR_SIZE);
    for (uint32_t i = 0; i < count; i++) {
        atomic_set_bit(dirty, sector + i);
    }
}

static bool transfer_is_valid(uint32_t sector, const struct sdhc_data *data)
{
    return data != NULL && data->block_size == SECTOR_SIZE && sector < SECTORS &&
           data->blocks <= SECTORS - sector;
}

static int execute(struct sdhc_command *cmd, struct sdhc_data *data)
{
    memset(cmd->response, 0, sizeof(cmd->response));

    switch (cmd->opcode) {
    case SD_GO_IDLE_STATE:
    case SD_ALL_SEND_CID:
        return 0;
    case MMC_SEND_OP_COND:
        cmd->response[0] = OCR;
        return 0;
    case MMC_SEND_RELATIVE_ADDR:
    case SD_SELECT_CARD:
        cmd->response[0] = STATUS_TRAN;
        return 0;
    case SD_SWITCH:
        card_switch(cmd->arg);
        cmd->response[0] = STATUS_TRAN;
        return 0;
    case MMC_SEND_EXT_CSD:
        /* Without data this is the stack's SD interface probe, which an eMMC device ignores. */
        if (data == NULL) {
            return -ETIMEDOUT;
        }
        memcpy(data->data, ext_csd, sizeof(ext_csd));
        cmd->response[0] = STATUS_TRAN;
        return 0;
    case SD_SEND_CSD:
        cmd->response[3] = CSD_STRUCTURE_3 | CSD_XFER_RATE;
        return 0;
    case SD_SEND_STATUS:
        cmd->response[0] = read_status();
        return 0;
    case SD_READ_SINGLE_BLOCK:
    case SD_READ_MULTIPLE_BLOCK:
        if (!transfer_is_valid(cmd->arg, data)) {
            return -EIO;
        }
        read_sectors(cmd->arg, data->data, data->blocks);
        cmd->response[0] = STATUS_TRAN;
        return 0;
    case SD_WRITE_SINGLE_BLOCK:
    case SD_WRITE_MULTIPLE_BLOCK:
        if (!transfer_is_valid(cmd->arg, data)) {
            return -EIO;
        }
        write_sectors(cmd->arg, data->data, data->blocks);
        cmd->response[0] = STATUS_TRAN;
        return 0;
    default:
        return -ENOTSUP;
    }
}

/* Counts the request and returns the fault slot of its operation, if it is one that has a slot. */
static struct fault *count_request(const struct sdhc_command *cmd, const struct sdhc_data *data)
{
    if (data != NULL && ((uintptr_t)data->data & (CONFIG_SDHC_BUFFER_ALIGNMENT - 1)) != 0U) {
        stats.misaligned_transfers++;
    }

    switch (cmd->opcode) {
    case SD_READ_SINGLE_BLOCK:
    case SD_READ_MULTIPLE_BLOCK:
        stats.reads++;
        return &op_faults[EMMC_SIM_OP_READ];
    case SD_WRITE_SINGLE_BLOCK:
    case SD_WRITE_MULTIPLE_BLOCK:
        stats.writes++;
        return &op_faults[EMMC_SIM_OP_WRITE];
    case SD_SWITCH:
        if (SWITCH_INDEX(cmd->arg) != EXT_CSD_FLUSH_CACHE) {
            return NULL;
        }
        stats.flush_commands++;
        return &op_faults[EMMC_SIM_OP_FLUSH];
    case SD_SEND_STATUS:
        stats.status_reads++;
        return NULL;
    default:
        return NULL;
    }
}

/* Whether a fault applies to this request. Called once for each request of its kind. */
static bool fault_applies(struct fault *fault)
{
    if (fault == NULL || fault->attempts == 0U) {
        return false;
    }
    if (fault->skip > 0U) {
        fault->skip--;
        return false;
    }
    return true;
}

/* As imx_usdhc.c does, a failed transfer is tried retries + 1 times within one request. */
static int emmc_sim_request(const struct device *dev, struct sdhc_command *cmd,
                            struct sdhc_data *data)
{
    ARG_UNUSED(dev);
    struct fault *op_fault = count_request(cmd, data);
    bool op_fails = fault_applies(op_fault);
    bool command_fails = cmd->opcode == command_fault_opcode && fault_applies(&command_fault);

    for (unsigned int attempt = 0; attempt <= cmd->retries; attempt++) {
        bool failed = (op_fails && consume(&op_fault->attempts)) ||
                      (command_fails && consume(&command_fault.attempts));

        if (!failed) {
            return execute(cmd, data);
        }
    }
    return -EIO;
}

static int emmc_sim_set_io(const struct device *dev, struct sdhc_io *ios)
{
    ARG_UNUSED(dev);
    if (ios->power_mode == SDHC_POWER_OFF) {
        power_off();
    }
    return 0;
}

static int emmc_sim_get_card_present(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 1;
}

static int emmc_sim_card_busy(const struct device *dev)
{
    ARG_UNUSED(dev);
    return busy_held || consume(&busy_polls);
}

static int emmc_sim_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
    ARG_UNUSED(dev);
    *props = (struct sdhc_host_props){
        .f_min = SDMMC_CLOCK_400KHZ,
        .f_max = MMC_CLOCK_52MHZ,
        .power_delay = 1,
        .host_caps = {.bus_8_bit_support = true, .vol_330_support = true},
        .bus_4_bit_support = true,
    };
    return 0;
}

static int emmc_sim_reset(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0;
}

static int emmc_sim_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    if (cache_kb_option != UINT32_MAX) {
        cache_kb = cache_kb_option;
    }
    if (image_path != NULL) {
        emmc_sim_bottom_load_image(image_path, persistent, sizeof(persistent));
    }
    if (trace_path != NULL) {
        emmc_sim_bottom_open_trace(trace_path);
    }
    return 0;
}

static DEVICE_API(sdhc, emmc_sim_api) = {
    .reset = emmc_sim_reset,
    .request = emmc_sim_request,
    .set_io = emmc_sim_set_io,
    .get_card_present = emmc_sim_get_card_present,
    .card_busy = emmc_sim_card_busy,
    .get_host_props = emmc_sim_get_host_props,
};

DEVICE_DT_INST_DEFINE(0, emmc_sim_init, NULL, NULL, NULL, POST_KERNEL, CONFIG_SDHC_INIT_PRIORITY,
                      &emmc_sim_api);

void emmc_sim_fail(enum emmc_sim_op op, uint32_t nth, uint32_t attempts)
{
    __ASSERT(nth >= 1U, "nth is 1-based");
    op_faults[op] = (struct fault){.skip = nth - 1U, .attempts = attempts};
}

void emmc_sim_fail_command(uint8_t opcode, uint32_t nth, uint32_t attempts)
{
    __ASSERT(nth >= 1U, "nth is 1-based");
    command_fault_opcode = opcode;
    command_fault = (struct fault){.skip = nth - 1U, .attempts = attempts};
}

void emmc_sim_reject_next_switch(void)
{
    reject_next_switch = true;
}

void emmc_sim_status_not_ready(uint32_t count)
{
    not_ready_reads = count;
}

void emmc_sim_hold_busy(bool busy)
{
    busy_held = busy;
}

void emmc_sim_busy_after_switch(bool forever)
{
    busy_polls_after_switch = forever ? UINT32_MAX : 1U;
}

void emmc_sim_not_ready_after_switch(uint32_t count)
{
    not_ready_reads_after_switch = count;
}

void emmc_sim_clear_faults(void)
{
    memset(op_faults, 0, sizeof(op_faults));
    command_fault = (struct fault){0};
    reject_next_switch = false;
    busy_held = false;
    busy_polls = 0;
    not_ready_reads = 0;
    busy_polls_after_switch = 0;
    not_ready_reads_after_switch = 0;
    switch_error = false;
}

void emmc_sim_set_cache_kb(uint32_t kb)
{
    cache_kb = kb;
}

void emmc_sim_get_stats(struct emmc_sim_stats *out)
{
    *out = stats;
    out->cache_on = cache_on;
}

void emmc_sim_reset_stats(void)
{
    stats = (struct emmc_sim_stats){0};
}

int emmc_sim_read_persistent(uint32_t sector, void *out, uint32_t count)
{
    if (sector >= SECTORS || count > SECTORS - sector) {
        return -EINVAL;
    }
    memcpy(out, &persistent[sector * SECTOR_SIZE], count * SECTOR_SIZE);
    return 0;
}

void emmc_sim_trace_mark(uint32_t id)
{
    trace_record(TRACE_MARK, 0, 0, id, NULL);
}
