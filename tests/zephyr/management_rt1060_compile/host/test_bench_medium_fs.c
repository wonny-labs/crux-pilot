/*
 * Host test of the Zephyr glue of the bench medium (src/bench_medium_fs.c), the one source of the
 * bench image that mounts the file medium and, for the RAM disk only, formats it. The file system
 * layer and the devices are fakes: a RAM disk named RAM: and a card named SD:. Which of the two the
 * glue is built for is set by the compile definition, as the Kconfig form sets it in the image; the
 * test is built once for each. Nothing here touches a board, a card or a card reader.
 */
#include "../src/bench_medium_fs.c"

#include <errno.h>
#include <string.h>

static int failures;
#define CHECK(condition, ...)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

typedef struct {
    const char *mount_point;
    const char *disk_name;
    bool has_file_system;
    unsigned mount_calls;
    unsigned format_calls;
} fake_disk_t;

static fake_disk_t ram_disk = {"/RAM:", "RAM:", false, 0, 0};
static fake_disk_t card = {"/SD:", "SD:", false, 0, 0};
static unsigned unknown_calls;
static unsigned mount_without_no_format_flag;

static fake_disk_t *by_mount_point(const char *mount_point)
{
    return strcmp(mount_point, ram_disk.mount_point) == 0 ? &ram_disk
           : strcmp(mount_point, card.mount_point) == 0   ? &card
                                                          : NULL;
}

int fs_mount(struct fs_mount_t *mp)
{
    fake_disk_t *disk = by_mount_point(mp->mnt_point);
    if (disk == NULL) {
        unknown_calls++;
        return -ENODEV;
    }
    if ((mp->flags & FS_MOUNT_FLAG_NO_FORMAT) == 0) {
        mount_without_no_format_flag++; /* Zephyr would format on a failed mount */
    }
    disk->mount_calls++;
    return disk->has_file_system ? 0 : -EINVAL;
}

int fs_mkfs(int fs_type, uintptr_t dev_id, void *cfg, int flags)
{
    (void)fs_type;
    (void)cfg;
    (void)flags;
    const char *name = (const char *)dev_id;
    if (strcmp(name, ram_disk.disk_name) == 0) {
        ram_disk.format_calls++;
        ram_disk.has_file_system = true;
        return 0;
    }
    if (strcmp(name, card.disk_name) == 0) {
        card.format_calls++;
        card.has_file_system = true;
        return 0;
    }
    unknown_calls++;
    return -ENODEV;
}

int main(void)
{
#if defined(CONFIG_CRUX_BENCH_FILE_RAM_DISK)
    /* A blank RAM disk, as every boot finds it: formatted, and only that disk. */
    CHECK(bench_medium_mount(), "a blank RAM disk is mounted");
    CHECK(ram_disk.format_calls == 1 && ram_disk.mount_calls == 2, "RAM disk calls %u %u",
          ram_disk.format_calls, ram_disk.mount_calls);
    CHECK(card.format_calls == 0 && card.mount_calls == 0,
          "the card was touched by a RAM disk image");
    /* The same image mounts a RAM disk that already holds a file system without another format. */
    ram_disk.mount_calls = 0;
    ram_disk.format_calls = 0;
    CHECK(bench_medium_mount() && ram_disk.format_calls == 0,
          "a formatted RAM disk is not formatted");
    CHECK(strcmp(BENCH_MEDIUM_MOUNT_POINT, "/RAM:") == 0, "mount point %s",
          BENCH_MEDIUM_MOUNT_POINT);
#elif defined(CONFIG_CRUX_BENCH_FILE_MICROSD)
    /* A blank card, and then a card whose mount fails for another reason: never a format. */
    CHECK(!bench_medium_mount(), "a blank card is not mounted");
    CHECK(card.format_calls == 0 && card.mount_calls == 1, "card calls %u %u", card.format_calls,
          card.mount_calls);
    CHECK(!card.has_file_system, "the card gained a file system");
    CHECK(ram_disk.mount_calls == 0 && ram_disk.format_calls == 0, "the RAM disk was touched");
    card.has_file_system = true;
    CHECK(bench_medium_mount() && card.format_calls == 0, "a card with a file system is mounted");
    CHECK(strcmp(BENCH_MEDIUM_MOUNT_POINT, "/SD:") == 0, "mount point %s",
          BENCH_MEDIUM_MOUNT_POINT);
#else
#error "built for no file backend"
#endif
    CHECK(unknown_calls == 0, "calls on a device that is neither");
    CHECK(mount_without_no_format_flag == 0, "a mount without FS_MOUNT_FLAG_NO_FORMAT");
    printf(failures == 0 ? "bench medium glue: all checks passed\n"
                         : "bench medium glue: %d FAILED\n",
           failures);
    return failures == 0 ? 0 : 1;
}
