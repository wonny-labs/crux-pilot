/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Reliance Edge as an external Zephyr VFS file system. The VFS passes full paths; the volume has an
 * empty prefix, so the adapter passes what follows the mount point, and the root when nothing does.
 * Reliance Edge error numbers are translated explicitly, because the two number spaces are not
 * assumed to be equal.
 */
#include "fs_reliance_edge.h"
#include "fs_reliance_edge_priv.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs_sys.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <redposix.h>

/* With only an external file system, Zephyr's name limit falls back to 12. */
BUILD_ASSERT(MAX_FILE_NAME >= REDCONF_NAME_MAX,
             "set CONFIG_FILE_SYSTEM_MAX_FILE_NAME to at least the Reliance Edge name limit");

#if REDCONF_VOLUME_COUNT != 1
#error "The adapter serves exactly one Reliance Edge volume"
#endif
#if REDCONF_API_POSIX != 1 || REDCONF_READ_ONLY != 0
#error "The adapter needs the read-write POSIX-like Reliance Edge API"
#endif
/* Without an automatic transaction on fsync, a sync commits nothing and still reports success. */
#if (REDCONF_TRANSACT_DEFAULT & RED_TRANSACT_FSYNC) == 0
#error "REDCONF_TRANSACT_DEFAULT must include RED_TRANSACT_FSYNC"
#endif
/* Block buffers go straight to the disk, so they must satisfy the SD host's DMA alignment. */
#if defined(CONFIG_SDHC) && (REDCONF_BUFFER_ALIGNMENT < CONFIG_SDHC_BUFFER_ALIGNMENT)
#error "REDCONF_BUFFER_ALIGNMENT must be at least CONFIG_SDHC_BUFFER_ALIGNMENT"
#endif
#if defined(CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT) != (REDCONF_API_POSIX_FORMAT == 1)
#error "The formatter must be compiled exactly when CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT is set"
#endif

/* The name of the only volume, given its empty path prefix. */
#define CRUX_RED_VOLUME ""
#define CRUX_RED_FD_FREE (-1)

/* An open file: the Reliance Edge descriptor, and whether it has writes not yet synced. */
struct crux_red_file {
    int32_t fd;
    bool dirty;
};

/* Directories take no slot, so a full table means that Reliance Edge is out of handles too. */
static struct crux_red_file crux_red_files[REDCONF_HANDLE_COUNT];

/* Protects the mount state, the initialisation flag and the slot table. Reliance Edge takes its own
 * mutex inside each call, never the other way round. */
static K_MUTEX_DEFINE(crux_red_state_lock);
static bool crux_red_initialised;
static const struct fs_mount_t *crux_red_mounted;

static void crux_red_lock(void)
{
    if (k_mutex_lock(&crux_red_state_lock, K_FOREVER) != 0) {
        k_panic();
    }
}

static void crux_red_unlock(void)
{
    if (k_mutex_unlock(&crux_red_state_lock) != 0) {
        k_panic();
    }
}

static int crux_red_to_errno(REDSTATUS status)
{
    switch (status) {
    case RED_EPERM:
        return -EPERM;
    case RED_ENOENT:
        return -ENOENT;
    case RED_EIO:
        return -EIO;
    case RED_EBADF:
        return -EBADF;
    case RED_ENOMEM:
        return -ENOMEM;
    case RED_EACCES:
        return -EACCES;
    case RED_EBUSY:
        return -EBUSY;
    case RED_EEXIST:
        return -EEXIST;
    case RED_EXDEV:
        return -EXDEV;
    case RED_ENOTDIR:
        return -ENOTDIR;
    case RED_EISDIR:
        return -EISDIR;
    case RED_EINVAL:
        return -EINVAL;
    case RED_ENFILE:
        return -ENFILE;
    case RED_EMFILE:
        return -EMFILE;
    case RED_EFBIG:
        return -EFBIG;
    case RED_ENOSPC:
        return -ENOSPC;
    case RED_EROFS:
        return -EROFS;
    case RED_EMLINK:
        return -EMLINK;
    case RED_ERANGE:
        return -ERANGE;
    case RED_ENAMETOOLONG:
        return -ENAMETOOLONG;
    case RED_ENOSYS:
        return -ENOSYS;
    case RED_ENOTEMPTY:
        return -ENOTEMPTY;
    case RED_ELOOP:
        return -ELOOP;
    case RED_ENODATA:
        return -ENODATA;
    case RED_ENOTSUPP:
        return -ENOTSUP;
    /* No thread slot left: the File provider reports this as NO_RESOURCES. */
    case RED_EUSERS:
        return -ENOMEM;
    default:
        return -EIO;
    }
}

/* The error of the Reliance Edge call that just failed in this thread. */
static int crux_red_error(void)
{
    return crux_red_to_errno(red_errno);
}

static const char *crux_red_path(const struct fs_mount_t *mountp, const char *path)
{
    const char *rest = path + mountp->mountp_len;
    return (rest[0] == '\0') ? "/" : rest;
}

/* Called with the state lock held. */
static int crux_red_ready(void)
{
    if (!crux_red_initialised) {
        if (red_init() != 0) {
            return crux_red_error();
        }
        crux_red_initialised = true;
    }
    return 0;
}

static int crux_red_fill_entry(struct fs_dirent *entry, const char *name, size_t length,
                               const REDSTAT *stat)
{
    if (length > MAX_FILE_NAME) {
        return -ENAMETOOLONG;
    }
    memcpy(entry->name, name, length);
    entry->name[length] = '\0';
    if (RED_S_ISDIR(stat->st_mode)) {
        entry->type = FS_DIR_ENTRY_DIR;
        entry->size = 0;
        return 0;
    }
#if SIZE_MAX < UINT64_MAX
    if (stat->st_size > SIZE_MAX) {
        return -EOVERFLOW;
    }
#endif
    entry->type = FS_DIR_ENTRY_FILE;
    entry->size = (size_t)stat->st_size;
    return 0;
}

static struct crux_red_file *crux_red_file_of(struct fs_file_t *zfp)
{
    return (struct crux_red_file *)zfp->filep;
}

static int crux_red_open(struct fs_file_t *zfp, const char *path, fs_mode_t flags)
{
    uint32_t mode;

    if ((flags & ~FS_O_MASK) != 0) {
        return -EINVAL;
    }
    switch (flags & FS_O_MODE_MASK) {
    case FS_O_READ:
        mode = RED_O_RDONLY;
        break;
    case FS_O_WRITE:
        mode = RED_O_WRONLY;
        break;
    case FS_O_RDWR:
        mode = RED_O_RDWR;
        break;
    default:
        return -EINVAL;
    }
    if ((flags & FS_O_CREATE) != 0) {
        mode |= RED_O_CREAT;
    }
    if ((flags & FS_O_APPEND) != 0) {
        mode |= RED_O_APPEND;
    }
    /* FS_O_TRUNC is applied by the VFS, through truncate, after a successful open. */

    int rc = -EMFILE;
    crux_red_lock();
    for (size_t i = 0; i < ARRAY_SIZE(crux_red_files); ++i) {
        struct crux_red_file *slot = &crux_red_files[i];
        if (slot->fd != CRUX_RED_FD_FREE) {
            continue;
        }
        int32_t fd = red_open(crux_red_path(zfp->mp, path), mode);
        if (fd < 0) {
            rc = crux_red_error();
        } else {
            slot->fd = fd;
            slot->dirty = false;
            zfp->filep = slot;
            rc = 0;
        }
        break;
    }
    crux_red_unlock();
    return rc;
}

static int crux_red_close(struct fs_file_t *zfp)
{
    struct crux_red_file *slot = crux_red_file_of(zfp);
    int rc = 0;

    if (slot == NULL) {
        return -EBADF;
    }
    if (red_close(slot->fd) != 0) {
        rc = crux_red_error();
    }
    crux_red_lock();
    slot->fd = CRUX_RED_FD_FREE;
    slot->dirty = false;
    crux_red_unlock();
    zfp->filep = NULL;
    return rc;
}

static ssize_t crux_red_read(struct fs_file_t *zfp, void *dest, size_t nbytes)
{
    struct crux_red_file *slot = crux_red_file_of(zfp);

    if (slot == NULL) {
        return -EBADF;
    }
    if (nbytes > INT32_MAX) {
        return -EINVAL;
    }
    int32_t count = red_read(slot->fd, dest, (uint32_t)nbytes);
    return (count < 0) ? crux_red_error() : count;
}

static ssize_t crux_red_write(struct fs_file_t *zfp, const void *src, size_t nbytes)
{
    struct crux_red_file *slot = crux_red_file_of(zfp);

    if (slot == NULL) {
        return -EBADF;
    }
    if (nbytes > INT32_MAX) {
        return -EINVAL;
    }
    int32_t count = red_write(slot->fd, src, (uint32_t)nbytes);
    return (count < 0) ? crux_red_error() : count;
}

static int crux_red_lseek(struct fs_file_t *zfp, off_t off, int whence)
{
    struct crux_red_file *slot = crux_red_file_of(zfp);
    REDWHENCE from;

    if (slot == NULL) {
        return -EBADF;
    }
    switch (whence) {
    case FS_SEEK_SET:
        from = RED_SEEK_SET;
        break;
    case FS_SEEK_CUR:
        from = RED_SEEK_CUR;
        break;
    case FS_SEEK_END:
        from = RED_SEEK_END;
        break;
    default:
        return -EINVAL;
    }
    return (red_lseek(slot->fd, (int64_t)off, from) < 0) ? crux_red_error() : 0;
}

static off_t crux_red_tell(struct fs_file_t *zfp)
{
    struct crux_red_file *slot = crux_red_file_of(zfp);

    if (slot == NULL) {
        return -EBADF;
    }
    int64_t position = red_lseek(slot->fd, 0, RED_SEEK_CUR);
    if (position < 0) {
        return crux_red_error();
    }
    if ((int64_t)(off_t)position != position) {
        return -EOVERFLOW;
    }
    return (off_t)position;
}

static int crux_red_truncate(struct fs_file_t *zfp, off_t length)
{
    struct crux_red_file *slot = crux_red_file_of(zfp);

    if (slot == NULL) {
        return -EBADF;
    }
    if (length < 0) {
        return -EINVAL;
    }
    return (red_ftruncate(slot->fd, (uint64_t)length) != 0) ? crux_red_error() : 0;
}

static int crux_red_sync(struct fs_file_t *zfp)
{
    struct crux_red_file *slot = crux_red_file_of(zfp);

    if (slot == NULL) {
        return -EBADF;
    }
    return (red_fsync(slot->fd) != 0) ? crux_red_error() : 0;
}

static int crux_red_opendir(struct fs_dir_t *zdp, const char *path)
{
    REDDIR *dir = red_opendir(crux_red_path(zdp->mp, path));

    if (dir == NULL) {
        return crux_red_error();
    }
    zdp->dirp = dir;
    return 0;
}

static int crux_red_readdir(struct fs_dir_t *zdp, struct fs_dirent *entry)
{
    /* The end of the directory is a NULL entry with no error set. */
    red_errno = 0;
    REDDIRENT *found = red_readdir((REDDIR *)zdp->dirp);

    if (found == NULL) {
        if (red_errno != 0) {
            return crux_red_error();
        }
        entry->name[0] = '\0';
        return 0;
    }
    return crux_red_fill_entry(entry, found->d_name, strlen(found->d_name), &found->d_stat);
}

static int crux_red_closedir(struct fs_dir_t *zdp)
{
    return (red_closedir((REDDIR *)zdp->dirp) != 0) ? crux_red_error() : 0;
}

static int crux_red_mount(struct fs_mount_t *mountp)
{
    int rc;

    if (mountp->storage_dev == NULL) {
        return -EINVAL;
    }
    crux_red_lock();
    if (crux_red_mounted != NULL) {
        rc = -EBUSY;
    } else {
        rc = crux_red_ready();
        if (rc == 0) {
            uint32_t flags = 0;
            if ((mountp->flags & FS_MOUNT_FLAG_READ_ONLY) != 0) {
                flags |= RED_MOUNT_READONLY;
            }
            crux_red_bdev_select((const char *)mountp->storage_dev);
            if (red_mount2(CRUX_RED_VOLUME, flags) == 0) {
                crux_red_mounted = mountp;
            } else {
                rc = crux_red_error();
            }
        }
    }
    crux_red_unlock();
    return rc;
}

static int crux_red_unmount(struct fs_mount_t *mountp)
{
    int rc = 0;

    crux_red_lock();
    if (mountp != crux_red_mounted) {
        rc = -EINVAL;
    } else if (red_umount(CRUX_RED_VOLUME) != 0) {
        rc = crux_red_error();
    } else {
        crux_red_mounted = NULL;
    }
    crux_red_unlock();
    return rc;
}

static int crux_red_unlink(struct fs_mount_t *mountp, const char *name)
{
    return (red_unlink(crux_red_path(mountp, name)) != 0) ? crux_red_error() : 0;
}

static int crux_red_rename(struct fs_mount_t *mountp, const char *from, const char *to)
{
    return (red_rename(crux_red_path(mountp, from), crux_red_path(mountp, to)) != 0)
               ? crux_red_error()
               : 0;
}

static int crux_red_mkdir(struct fs_mount_t *mountp, const char *name)
{
    return (red_mkdir(crux_red_path(mountp, name)) != 0) ? crux_red_error() : 0;
}

static int crux_red_stat(struct fs_mount_t *mountp, const char *path, struct fs_dirent *entry)
{
    const char *local = crux_red_path(mountp, path);
    REDSTAT stat;

    if (red_stat(local, &stat) != 0) {
        return crux_red_error();
    }
    /* The entry name is the last path component, without trailing separators. */
    const char *end = local + strlen(local);
    while (end > local && end[-1] == '/') {
        --end;
    }
    const char *start = end;
    while (start > local && start[-1] != '/') {
        --start;
    }
    return crux_red_fill_entry(entry, start, (size_t)(end - start), &stat);
}

static int crux_red_statvfs(struct fs_mount_t *mountp, const char *path, struct fs_statvfs *stat)
{
    REDSTATFS volume;

    ARG_UNUSED(mountp);
    ARG_UNUSED(path);
    if (red_statvfs(CRUX_RED_VOLUME, &volume) != 0) {
        return crux_red_error();
    }
    stat->f_bsize = volume.f_bsize;
    stat->f_frsize = volume.f_frsize;
    stat->f_blocks = volume.f_blocks;
    stat->f_bfree = volume.f_bfree;
    return 0;
}

#if defined(CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT)
/* dev_id is the disk name; there is no configuration and no flag. */
static int crux_red_mkfs(uintptr_t dev_id, void *cfg, int flags)
{
    int rc;

    ARG_UNUSED(flags);
    if (dev_id == 0U || cfg != NULL) {
        return -EINVAL;
    }
    crux_red_lock();
    if (crux_red_mounted != NULL) {
        rc = -EBUSY;
    } else {
        rc = crux_red_ready();
        if (rc == 0) {
            crux_red_bdev_select((const char *)dev_id);
            if (red_format(CRUX_RED_VOLUME) != 0) {
                rc = crux_red_error();
            }
        }
    }
    crux_red_unlock();
    return rc;
}
#endif

static const struct fs_file_system_t crux_red_fs = {
    .open = crux_red_open,
    .read = crux_red_read,
    .write = crux_red_write,
    .lseek = crux_red_lseek,
    .tell = crux_red_tell,
    .truncate = crux_red_truncate,
    .sync = crux_red_sync,
    .close = crux_red_close,
    .opendir = crux_red_opendir,
    .readdir = crux_red_readdir,
    .closedir = crux_red_closedir,
    .mount = crux_red_mount,
    .unmount = crux_red_unmount,
    .unlink = crux_red_unlink,
    .rename = crux_red_rename,
    .mkdir = crux_red_mkdir,
    .stat = crux_red_stat,
    .statvfs = crux_red_statvfs,
#if defined(CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT)
    .mkfs = crux_red_mkfs,
#endif
};

static int crux_red_register(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(crux_red_files); ++i) {
        crux_red_files[i].fd = CRUX_RED_FD_FREE;
        crux_red_files[i].dirty = false;
    }
    return fs_register(CRUX_FS_RELIANCE_EDGE, &crux_red_fs);
}

SYS_INIT(crux_red_register, POST_KERNEL, CONFIG_FILE_SYSTEM_INIT_PRIORITY);
