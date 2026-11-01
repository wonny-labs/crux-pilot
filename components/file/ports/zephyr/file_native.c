#include "file_native.h"
#include "file_port.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

/* RT1060 selection compiles this source as an unlinked object only. */
#if defined(CONFIG_CRUX_FILE_FATFS_RT1060_COMPILE_CHECK)
#if !defined(CONFIG_BOARD_MIMXRT1060_EVK) || !defined(CONFIG_SOC_MIMXRT1062) ||                    \
    !defined(CONFIG_FAT_FILESYSTEM_ELM) || !defined(CONFIG_DISK_DRIVER_SDMMC) ||                   \
    !defined(CONFIG_IMX_USDHC) || !defined(CONFIG_FS_FATFS_LFN) ||                                 \
    !defined(CONFIG_FS_FATFS_REENTRANT) || defined(CONFIG_FS_FATFS_MOUNT_MKFS)
#error "RT1060 File compile check requires the guarded FatFs/SDHC composition"
#endif
#elif !defined(CONFIG_BOARD_NATIVE_SIM) ||                                                         \
    (!defined(CONFIG_CRUX_FILE_SIMULATOR) && !defined(CONFIG_CRUX_FILE_FATFS_SIMULATOR))
#error "File provider requires an approved simulator composition"
#endif
#if defined(CONFIG_CRUX_FILE_SIMULATOR) && defined(CONFIG_CRUX_FILE_FATFS_SIMULATOR)
#error "File simulator compositions must be mutually exclusive"
#endif
/* One selector for every FatFs-specific behaviour, so a compile check builds what runs. */
#if defined(CONFIG_CRUX_FILE_FATFS_SIMULATOR) ||                                                   \
    defined(CONFIG_CRUX_FILE_FATFS_RT1060_COMPILE_CHECK)
#define CRUX_FILE_NATIVE_FATFS 1
#endif
#if defined(CRUX_FILE_NATIVE_FATFS)
#if !defined(CONFIG_FS_FATFS_LFN) || !defined(CONFIG_FS_FATFS_REENTRANT) ||                        \
    defined(CONFIG_FS_FATFS_MOUNT_MKFS)
#error "FatFs File composition requires long names, reentrancy and no auto-format"
#endif
#define CRUX_FILE_NATIVE_LEAF_MAX (1 + 2 * CRUX_FILE_NAME_MAX)
_Static_assert(CONFIG_FS_FATFS_MAX_LFN >= CRUX_FILE_NATIVE_LEAF_MAX,
               "FatFs long filenames must accommodate encoded File names");
#else
#define CRUX_FILE_NATIVE_LEAF_MAX CRUX_FILE_NAME_MAX
#endif
K_MUTEX_DEFINE(file_lock);
static struct fs_file_t handles[CRUX_FILE_POOL_SIZE];
static char root[64];
static char paths[CRUX_FILE_POOL_SIZE][sizeof(root) + CRUX_FILE_NATIVE_LEAF_MAX + 1];
static crux_file_result_t cleanup_error;

static crux_file_result_t result(int rc)
{
    switch (rc) {
    case 0:
        return CRUX_FILE_OK;
    case -ENOENT:
        return CRUX_FILE_ERR_NOT_FOUND;
    case -ENOSPC:
        return CRUX_FILE_ERR_NO_SPACE;
    case -ENOMEM:
    case -EMFILE:
        return CRUX_FILE_ERR_NO_RESOURCES;
    case -ENODEV:
        return CRUX_FILE_ERR_NOT_READY;
    case -EACCES:
    case -EPERM:
    case -EROFS:
        return CRUX_FILE_ERR_ACCESS;
    case -EINVAL:
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    case -EFBIG:
    case -EOVERFLOW:
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    case -ENOTSUP:
    case -ENOSYS:
        return CRUX_FILE_ERR_NOT_SUPPORTED;
    default:
        return CRUX_FILE_ERR_IO;
    }
}

void crux_file_lock(void)
{
    __ASSERT_NO_MSG(!k_is_in_isr());
    if (k_mutex_lock(&file_lock, K_FOREVER) != 0) {
        k_panic();
    }
}

void crux_file_unlock(void)
{
    if (k_mutex_unlock(&file_lock) != 0) {
        k_panic();
    }
}

crux_file_result_t crux_file_zephyr_bind(const char *r)
{
    if (!r || r[0] != '/') {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    size_t length = strlen(r);
    if (length < 2 || length >= sizeof(root) || r[length - 1] == '/') {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (root[0]) {
        return CRUX_FILE_ERR_INVALID_STATE;
    }
    struct fs_dirent st;
    int rc = fs_stat(r, &st);
    if (rc < 0) {
        return result(rc);
    }
    if (st.type != FS_DIR_ENTRY_DIR) {
        return CRUX_FILE_ERR_NOT_SUPPORTED;
    }
    memcpy(root, r, length + 1);
    return CRUX_FILE_OK;
}

static crux_file_result_t ready(void)
{
    struct fs_dirent st;
    if (!root[0]) {
        return CRUX_FILE_ERR_NOT_READY;
    }
    int rc = fs_stat(root, &st);
    if (rc == -ENOENT || rc == -ENODEV) {
        return CRUX_FILE_ERR_NOT_READY;
    }
    if (rc < 0) {
        return result(rc);
    }
    return st.type == FS_DIR_ENTRY_DIR ? CRUX_FILE_OK : CRUX_FILE_ERR_NOT_READY;
}

static crux_file_result_t make_path(char *path, size_t capacity, const char *name)
{
#if defined(CRUX_FILE_NATIVE_FATFS)
    /* FatFs ignores case and trailing dots. Encode bytes to preserve File identity. */
    static const char hex[] = "0123456789abcdef";
    int n = snprintf(path, capacity, "%s/f", root);
    if (n < 0 || (size_t)n >= capacity) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }
    size_t pos = (size_t)n;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (capacity - pos <= 2) {
            return CRUX_FILE_ERR_OUT_OF_RANGE;
        }
        path[pos++] = hex[*p >> 4];
        path[pos++] = hex[*p & 0x0f];
    }
    path[pos] = '\0';
#else
    int n = snprintf(path, capacity, "%s/%s", root, name);
    if (n < 0 || (size_t)n >= capacity) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }
#endif
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_open(size_t s, const char *name, crux_file_mode_t mode)
{
    crux_file_result_t rc = ready();
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    rc = make_path(paths[s], sizeof(paths[s]), name);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    fs_mode_t flags = mode == CRUX_FILE_READ ? FS_O_READ : FS_O_READ | FS_O_WRITE;
    if (mode == CRUX_FILE_CREATE_REPLACE) {
        flags |= FS_O_CREATE;
    }
    fs_file_t_init(&handles[s]);
    int fs_rc = fs_open(&handles[s], paths[s], flags);
#if defined(CRUX_FILE_NATIVE_FATFS)
    if (fs_rc == -EACCES && (flags & FS_O_CREATE) != 0) {
        /* FatFs refuses a new entry with FR_DENIED when the directory cannot grow on a full volume.
         */
        struct fs_statvfs volume;
        if (fs_statvfs(root, &volume) == 0 && volume.f_bfree == 0) {
            return CRUX_FILE_ERR_NO_SPACE;
        }
    }
#endif
    return result(fs_rc);
}

crux_file_result_t crux_file_impl_truncate(size_t s)
{
    return result(fs_truncate(&handles[s], 0));
}

crux_file_result_t crux_file_impl_read(size_t s, void *b, size_t l, size_t *n)
{
    if (l > INT32_MAX) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }
    ssize_t rc = fs_read(&handles[s], b, l);
    if (rc < 0) {
        return result((int)rc);
    }
    *n = (size_t)rc;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_write(size_t s, const void *b, size_t l, size_t *n)
{
    if (l > INT32_MAX) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }
    ssize_t rc = fs_write(&handles[s], b, l);
    if (rc < 0) {
        return result((int)rc);
    }
#if defined(CRUX_FILE_NATIVE_FATFS)
    if (rc == 0) {
        /* FatFs reports a full volume as zero accepted bytes without an error. */
        return CRUX_FILE_ERR_NO_SPACE;
    }
#endif
    *n = (size_t)rc;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_seek(size_t s, uint64_t offset)
{
    _Static_assert(sizeof(off_t) == 4 || sizeof(off_t) == 8, "supported VFS offsets");
    /* LittleFS uses signed 32-bit offsets/counts, even with 64-bit off_t. */
    if (offset > INT32_MAX) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }
    off_t original = fs_tell(&handles[s]);
    if (original < 0) {
        return result((int)original);
    }
    int rc = fs_seek(&handles[s], 0, FS_SEEK_END);
    if (rc < 0) {
        return result(rc);
    }
    off_t eof = fs_tell(&handles[s]);
    int restored = fs_seek(&handles[s], original, FS_SEEK_SET);
    if (eof < 0) {
        return result((int)eof);
    }
    if (restored < 0) {
        return result(restored);
    }
    if (offset > (uint64_t)eof) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }
    return result(fs_seek(&handles[s], (off_t)offset, FS_SEEK_SET));
}

crux_file_result_t crux_file_impl_sync(size_t s)
{
    return result(fs_sync(&handles[s]));
}

crux_file_result_t crux_file_impl_close(size_t s)
{
    int rc = fs_close(&handles[s]);
    fs_file_t_init(&handles[s]); /* backend may release data even on failure */
    return result(rc);
}

crux_file_result_t crux_file_impl_remove(const char *name)
{
    crux_file_result_t rc = ready();
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    char path[sizeof(root) + CRUX_FILE_NATIVE_LEAF_MAX + 1];
    rc = make_path(path, sizeof(path), name);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    return result(fs_unlink(path));
}

void crux_file_record_cleanup_error(crux_file_result_t error)
{
    cleanup_error = error;
}

crux_file_result_t crux_file_zephyr_last_cleanup_error(void)
{
    return cleanup_error;
}
