/*
 * The Zephyr glue of the bench image's file medium (row C3, D2): the mount, and for the RAM disk the
 * format. This is the ONLY source of the image that names a format function, and it names it only in
 * the RAM disk form, so a card build has no reference to it at all. The unit test
 * tests/build/test_management_bench_medium.py shows both, and check_mkfs_callers.py shows it on the
 * linked objects of a build. The decision is bench_medium.c.
 *
 * Which medium is chosen by the Kconfig form (Kconfig.bench, bench_declaration.json), and the CMake
 * guard refuses a build in which the provider, the disk drivers and the form disagree.
 */
#include "bench_guard.h"
#include "bench_medium.h"

#include <ff.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/printk.h>

#if defined(CONFIG_CRUX_BENCH_FILE_RAM_DISK) == defined(CONFIG_CRUX_BENCH_FILE_MICROSD)
#error "The bench image has exactly one file backend: CONFIG_CRUX_BENCH_FILE_RAM_DISK or CONFIG_CRUX_BENCH_FILE_MICROSD"
#endif

static FATFS fat;
/* FS_MOUNT_FLAG_NO_FORMAT: a failed mount is a failed mount in the file system layer too. The format is ours. */
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = BENCH_MEDIUM_MOUNT_POINT,
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static int mount_medium(void *context)
{
    (void)context;
    return fs_mount(&mount);
}

#if defined(CONFIG_CRUX_BENCH_FILE_RAM_DISK)
/* The disk of the RAM disk node of ../ram_disk.overlay. A card's name is not here and cannot be passed. */
#define BENCH_MEDIUM_RAM_DISK_NAME "RAM:"
#define BENCH_MEDIUM_KIND BENCH_MEDIUM_RAM_DISK
#define BENCH_RAM_DISK_BYTES                                                                       \
    ((unsigned)(DT_PROP(DT_NODELABEL(bench_ram_disk), sector_size) *                                \
                DT_PROP(DT_NODELABEL(bench_ram_disk), sector_count)))

static int format_ram_disk(void *context)
{
    (void)context;
    return fs_mkfs(FS_FATFS, (uintptr_t)BENCH_MEDIUM_RAM_DISK_NAME, NULL, 0);
}
#define BENCH_MEDIUM_FORMAT format_ram_disk
#else
#define BENCH_MEDIUM_KIND BENCH_MEDIUM_CARD
#define BENCH_MEDIUM_FORMAT NULL
#endif

bool bench_medium_mount(void)
{
    const bench_medium_ops_t ops = {
        .mount = mount_medium, .format = BENCH_MEDIUM_FORMAT, .context = NULL};
    bench_medium_result_t result = bench_medium_prepare(BENCH_MEDIUM_KIND, &ops);
    bool mounted =
        result == BENCH_MEDIUM_MOUNTED || result == BENCH_MEDIUM_FORMATTED_AND_MOUNTED;

#if defined(CONFIG_CRUX_BENCH_FILE_RAM_DISK)
    /* What the owner reads on the UART: where the files are, how big it is, and that it is gone at a reset. */
    printk("BENCH file-medium ram-disk %u B in SDRAM, %s, mounted=%d: nothing persists across a reset "
           "(accounts, keys, certificate and events are lost)\n",
           BENCH_RAM_DISK_BYTES,
           result == BENCH_MEDIUM_FORMATTED_AND_MOUNTED ? "formatted at this boot"
                                                        : "found formatted",
           mounted);
#else
    printk("BENCH file-medium microsd card, mounted=%d, a card is never formatted by this image%s\n",
           mounted, mounted ? "" : ": the mount failed and nothing was written");
#endif
    return mounted;
}
