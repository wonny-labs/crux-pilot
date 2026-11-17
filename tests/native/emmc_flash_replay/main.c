/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The update steps of MCUboot's RAM load with revert, run through the flash map and the flash-API
 * driver on the eMMC simulator, with a trace mark after each step returns. run_driver_replay.py
 * starts it on a baseline medium, reads the simulator's trace and replays power cuts over it.
 *
 * It is not MCUboot. It issues the flash_area calls that MCUboot's functions issue (boot_set_next,
 * the bootloader's copy_done write and its scramble of a slot) in the order and with the sizes
 * that the trailer model of docs/evidence/boot-emmc/trailer-replay assumes, and nothing else.
 *
 *   --workload=confirm | revert | confirm-pad | revert-pad
 *       erase slot 1, write the image, request (not for -pad: the padded image carries the magic),
 *       the bootloader's trial (copy_done), then confirm (image_ok) or revert (scramble slot 1)
 *   --image-hex=HEX   the image of the update
 *   --magic-hex=HEX   the 16 bytes of the trailer magic
 *
 * Marks: 1 erased, 2 image, 3 requested, 4 trial, 5 confirmed or reverted.
 */
#include "emmc_sim.h"

#include <cmdline.h>
#include <posix_board_if.h>
#include <posix_native_task.h>

#include <stdio.h>
#include <string.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/util.h>

#define SECTOR 512U
#define PAGE 4096U
#define CHUNK 4096U
#define ERASED 0xFFU
#define MAGIC_SIZE 16U
#define SWAP_TYPE_TEST 2U
#define IMAGE_MAX 4096U

static const char *workload;
static const char *image_hex;
static const char *magic_hex;

static void add_options(void)
{
    static struct args_struct_t options[] = {
        {.option = "workload",
         .name = "name",
         .type = 's',
         .dest = (void *)&workload,
         .descript = "confirm, revert, confirm-pad or revert-pad"},
        {.option = "image-hex",
         .name = "hex",
         .type = 's',
         .dest = (void *)&image_hex,
         .descript = "The image of the update, in hex"},
        {.option = "magic-hex",
         .name = "hex",
         .type = 's',
         .dest = (void *)&magic_hex,
         .descript = "The 16 bytes of the trailer magic, in hex"},
        ARG_TABLE_ENDMARKER,
    };

    native_add_command_line_opts(options);
}
NATIVE_TASK(add_options, PRE_BOOT_1, 10);

static void fail(const char *what, int rc)
{
    printf("FAIL %s %d\n", what, rc);
    fflush(stdout);
    posix_exit(1);
}

static int nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static size_t from_hex(const char *text, uint8_t *out, size_t max)
{
    size_t length = text == NULL ? 0 : strlen(text);

    if (length == 0U || length % 2U != 0U || length / 2U > max) {
        fail("hex", -1);
    }
    for (size_t i = 0; i < length / 2U; i++) {
        int high = nibble(text[2U * i]);
        int low = nibble(text[2U * i + 1U]);

        if (high < 0 || low < 0) {
            fail("hex", -2);
        }
        out[i] = (uint8_t)(high << 4 | low);
    }
    return length / 2U;
}

static uint8_t image[IMAGE_MAX];
static uint8_t magic[MAGIC_SIZE];
static uint8_t block[CHUNK];

static void check(const char *what, int rc)
{
    if (rc != 0) {
        fail(what, rc);
    }
}

/* boot_write_trailer: the flag, then the erased value to the end of the write block. */
static void write_flag(const struct flash_area *fa, uint32_t offset, uint8_t value)
{
    memset(block, ERASED, SECTOR);
    block[0] = value;
    check("flag", flash_area_write(fa, offset, block, SECTOR));
}

/* boot_write_magic: the erased value, then the magic in the last 16 bytes of the unit. */
static void write_magic(const struct flash_area *fa, uint32_t size)
{
    memset(block, ERASED, SECTOR);
    memcpy(&block[SECTOR - MAGIC_SIZE], magic, MAGIC_SIZE);
    check("magic", flash_area_write(fa, size - SECTOR, block, SECTOR));
}

/* boot_erase_region, a page at a time and forwards, as for a device that needs explicit erase. */
static void scramble(const struct flash_area *fa)
{
    for (uint32_t offset = 0; offset < fa->fa_size; offset += PAGE) {
        check("erase", flash_area_erase(fa, offset, PAGE));
    }
}

/* The trailer offsets of a slot, for BOOT_MAX_ALIGN of one sector. */
static uint32_t image_ok_offset(const struct flash_area *fa)
{
    return fa->fa_size - 2U * SECTOR;
}

static uint32_t copy_done_offset(const struct flash_area *fa)
{
    return fa->fa_size - 3U * SECTOR;
}

static uint32_t swap_info_offset(const struct flash_area *fa)
{
    return fa->fa_size - 4U * SECTOR;
}

/* The image in chunks of 4 KiB, ascending, as a copy of a signed image to a slot would be. The
 * padded form fills the slot with the erased value and puts the magic at its end. */
static void write_image(const struct flash_area *fa, size_t length, bool padded)
{
    size_t total = padded ? fa->fa_size : ROUND_UP(length, SECTOR);

    for (size_t offset = 0; offset < total; offset += CHUNK) {
        size_t chunk = MIN((size_t)CHUNK, total - offset);

        memset(block, ERASED, chunk);
        if (offset < length) {
            memcpy(block, &image[offset], MIN(chunk, length - offset));
        }
        if (padded && offset + chunk == total) {
            memcpy(&block[chunk - MAGIC_SIZE], magic, MAGIC_SIZE);
        }
        check("image", flash_area_write(fa, offset, block, chunk));
    }
}

static bool is(const char *name)
{
    return workload != NULL && strcmp(workload, name) == 0;
}

int main(void)
{
    const struct flash_area *fa;
    bool padded = is("confirm-pad") || is("revert-pad");
    bool revert = is("revert") || is("revert-pad");
    size_t length;

    if (!padded && !is("confirm") && !revert) {
        fail("workload", -1);
    }
    length = from_hex(image_hex, image, sizeof(image));
    if (from_hex(magic_hex, magic, sizeof(magic)) != MAGIC_SIZE) {
        fail("magic", -1);
    }
    check("open", flash_area_open(PARTITION_ID(emmc_slot1), &fa));

    scramble(fa);
    emmc_sim_trace_mark(1);

    write_image(fa, length, padded);
    emmc_sim_trace_mark(2);

    if (!padded) {
        write_magic(fa, fa->fa_size);
        write_flag(fa, swap_info_offset(fa), SWAP_TYPE_TEST);
    }
    emmc_sim_trace_mark(3);

    write_flag(fa, copy_done_offset(fa), 1);
    emmc_sim_trace_mark(4);

    if (revert) {
        scramble(fa);
    } else {
        write_flag(fa, image_ok_offset(fa), 1);
    }
    emmc_sim_trace_mark(5);

    flash_area_close(fa);
    printf("OK %s\n", workload);
    fflush(stdout);
    posix_exit(0);
    return 0;
}
