/* Host stand-in for Zephyr's fs.h, with only what the bench medium glue calls. The two functions
 * are defined by the test, which stands in for the file system layer and the devices under it. */
#ifndef HOST_SHIM_ZEPHYR_FS_FS_H
#define HOST_SHIM_ZEPHYR_FS_FS_H
#include <stdint.h>
enum { FS_FATFS = 1 };
#define FS_MOUNT_FLAG_NO_FORMAT (1u << 0)
struct fs_mount_t {
    int type;
    const char *mnt_point;
    void *fs_data;
    unsigned flags;
};
int fs_mount(struct fs_mount_t *mp);
int fs_mkfs(int fs_type, uintptr_t dev_id, void *cfg, int flags);
#endif
