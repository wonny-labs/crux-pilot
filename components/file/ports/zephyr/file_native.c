#include "file_native.h"
#include "file_port.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

/* Selections: Reliance Edge on any board, an RT1060 provider or compile check, or a simulator. */
#if defined(CONFIG_CRUX_FILE_RELIANCE_EDGE)
#if defined(CONFIG_CRUX_FILE_SIMULATOR) || defined(CONFIG_CRUX_FILE_FATFS_SIMULATOR) ||            \
    defined(CONFIG_CRUX_FILE_FATFS_RT1060) || defined(CONFIG_CRUX_FILE_FATFS_RT1060_COMPILE_CHECK)
#error "The Reliance Edge File provider cannot be combined with another File selection"
#endif
#elif defined(CONFIG_CRUX_FILE_FATFS_RT1060_COMPILE_CHECK) || defined(CONFIG_CRUX_FILE_FATFS_RT1060)
#if !defined(CONFIG_BOARD_MIMXRT1060_EVK) || !defined(CONFIG_SOC_MIMXRT1062) ||                    \
    !defined(CONFIG_BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI) || !defined(CONFIG_BOARD_REVISION_B) ||  \
    !defined(CONFIG_FAT_FILESYSTEM_ELM) || !defined(CONFIG_DISK_DRIVER_SDMMC) ||                   \
    !defined(CONFIG_IMX_USDHC) || !defined(CONFIG_FS_FATFS_LFN) ||                                 \
    !defined(CONFIG_FS_FATFS_REENTRANT) || defined(CONFIG_FS_FATFS_MOUNT_MKFS)
#error "RT1060 File selections require the Rev B QSPI board and the guarded FatFs/SDHC composition"
#endif
#elif !defined(CONFIG_BOARD_NATIVE_SIM) ||                                                         \
    (!defined(CONFIG_CRUX_FILE_SIMULATOR) && !defined(CONFIG_CRUX_FILE_FATFS_SIMULATOR))
#error "File provider requires an approved simulator composition"
#endif
#if defined(CONFIG_SDHC) && defined(CONFIG_DCACHE)
/* The USDHC HAL invalidates a read target by whole cache lines on completion, so a buffer that
 * shares a line with another object can drop that object's write made during the transfer. The SD
 * layer bounces every buffer that is not aligned to this value. The symbol exists with the SD host
 * driver only, so a selection on a board without one has nothing to check. */
_Static_assert(CONFIG_SDHC_BUFFER_ALIGNMENT >= CONFIG_DCACHE_LINE_SIZE,
               "SDHC DMA buffers must be aligned to a whole D-cache line");
#endif
#if defined(CONFIG_CRUX_FILE_SIMULATOR) && defined(CONFIG_CRUX_FILE_FATFS_SIMULATOR)
#error "File simulator compositions must be mutually exclusive"
#endif
/* One selector for every FatFs-specific behaviour, so a compile check builds what runs. */
#if defined(CONFIG_CRUX_FILE_FATFS_SIMULATOR) || defined(CONFIG_CRUX_FILE_FATFS_RT1060) ||         \
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
_Static_assert(CONFIG_FS_FATFS_NUM_DIRS >= CRUX_FILE_DIR_POOL_SIZE,
               "FatFs must hold every File listing handle");
#else
#define CRUX_FILE_NATIVE_LEAF_MAX CRUX_FILE_NAME_MAX
#endif
#if defined(CONFIG_CRUX_FILE_RELIANCE_EDGE)
/* Names are stored as they are, and Zephyr's directory entries must hold the longest one. With only
 * an external file system Zephyr's limit falls back to 12, so the setting is required. */
BUILD_ASSERT(CONFIG_FILE_SYSTEM_MAX_FILE_NAME >= CRUX_FILE_NATIVE_LEAF_MAX,
             "set CONFIG_FILE_SYSTEM_MAX_FILE_NAME to at least the File name limit");
/* Every open file and every open listing takes a Reliance Edge handle, and the File pools hold
 * them all at once. REDCONF_HANDLE_COUNT is this Kconfig value in the repository's configuration,
 * and this file includes no Reliance Edge header. */
BUILD_ASSERT(CONFIG_CRUX_FS_RELIANCE_EDGE_HANDLE_COUNT >=
                 CRUX_FILE_POOL_SIZE + CRUX_FILE_DIR_POOL_SIZE,
             "CONFIG_CRUX_FS_RELIANCE_EDGE_HANDLE_COUNT must cover the File handle pools");
#endif
K_MUTEX_DEFINE(file_lock);
static struct fs_file_t handles[CRUX_FILE_POOL_SIZE];
static struct fs_dir_t listings[CRUX_FILE_DIR_POOL_SIZE];
static char roots[CRUX_FILE_ROOTS_MAX][64];
static char paths[CRUX_FILE_POOL_SIZE][sizeof(roots[0]) + CRUX_FILE_NATIVE_LEAF_MAX + 1];
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

crux_file_result_t crux_file_zephyr_bind_root(crux_file_root_t index, const char *r)
{
    if (index >= CRUX_FILE_ROOTS_MAX || !r || r[0] != '/') {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    size_t length = strlen(r);
    if (length < 2 || length >= sizeof(roots[0]) || r[length - 1] == '/') {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (roots[index][0]) {
        return CRUX_FILE_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < CRUX_FILE_ROOTS_MAX; i++) {
        if (roots[i][0] && crux_file_paths_overlap(roots[i], r)) {
            return CRUX_FILE_ERR_INVALID_STATE;
        }
    }
    struct fs_dirent st;
    int rc = fs_stat(r, &st);
    if (rc < 0) {
        return result(rc);
    }
    if (st.type != FS_DIR_ENTRY_DIR) {
        return CRUX_FILE_ERR_NOT_SUPPORTED;
    }
    memcpy(roots[index], r, length + 1);
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_zephyr_bind(const char *r)
{
    return crux_file_zephyr_bind_root(CRUX_FILE_ROOT_DEFAULT, r);
}

static crux_file_result_t ready(crux_file_root_t index)
{
    struct fs_dirent st;
    if (!roots[index][0]) {
        return CRUX_FILE_ERR_NOT_READY;
    }
    int rc = fs_stat(roots[index], &st);
    if (rc == -ENOENT || rc == -ENODEV) {
        return CRUX_FILE_ERR_NOT_READY;
    }
    if (rc < 0) {
        return result(rc);
    }
    return st.type == FS_DIR_ENTRY_DIR ? CRUX_FILE_OK : CRUX_FILE_ERR_NOT_READY;
}

static crux_file_result_t make_path(char *path, size_t capacity, crux_file_root_t index,
                                    const char *name)
{
#if defined(CRUX_FILE_NATIVE_FATFS)
    /* FatFs ignores case and trailing dots. Encode bytes to preserve File identity. */
    static const char hex[] = "0123456789abcdef";
    int n = snprintf(path, capacity, "%s/f", roots[index]);
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
    int n = snprintf(path, capacity, "%s/%s", roots[index], name);
    if (n < 0 || (size_t)n >= capacity) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }
#endif
    return CRUX_FILE_OK;
}

#if defined(CRUX_FILE_NATIVE_FATFS)
static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
#endif

/* The inverse of make_path's leaf. False for a name this provider cannot have written, so that one
 * file has one identity: only the lower-case encoding decodes, and no name holds a NUL. */
static bool decode_leaf(const char *leaf, char *name, size_t capacity)
{
#if defined(CRUX_FILE_NATIVE_FATFS)
    size_t length = strlen(leaf);
    if (leaf[0] != 'f' || length % 2 == 0 || (length - 1) / 2 >= capacity) {
        return false;
    }
    size_t out = 0;
    for (size_t i = 1; i < length; i += 2) {
        int high = hex_digit(leaf[i]);
        int low = hex_digit(leaf[i + 1]);
        if (high < 0 || low < 0 || (high == 0 && low == 0)) {
            return false;
        }
        name[out++] = (char)(high << 4 | low);
    }
    name[out] = '\0';
    return true;
#else
    size_t length = strlen(leaf);
    if (length >= capacity) {
        return false;
    }
    memcpy(name, leaf, length + 1);
    return true;
#endif
}

crux_file_result_t crux_file_impl_open(crux_file_root_t index, size_t s, const char *name,
                                       crux_file_mode_t mode)
{
    crux_file_result_t rc = ready(index);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    rc = make_path(paths[s], sizeof(paths[s]), index, name);
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
        if (fs_statvfs(roots[index], &volume) == 0 && volume.f_bfree == 0) {
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

crux_file_result_t crux_file_impl_remove(crux_file_root_t index, const char *name)
{
    crux_file_result_t rc = ready(index);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    char path[sizeof(roots[0]) + CRUX_FILE_NATIVE_LEAF_MAX + 1];
    rc = make_path(path, sizeof(path), index, name);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    return result(fs_unlink(path));
}

crux_file_result_t crux_file_impl_stat(crux_file_root_t index, const char *name,
                                       crux_file_info_t *out)
{
    crux_file_result_t rc = ready(index);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    char path[sizeof(roots[0]) + CRUX_FILE_NATIVE_LEAF_MAX + 1];
    rc = make_path(path, sizeof(path), index, name);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    struct fs_dirent st;
    int fs_rc = fs_stat(path, &st);
    if (fs_rc < 0) {
        return result(fs_rc);
    }
    if (st.type != FS_DIR_ENTRY_FILE) {
        return CRUX_FILE_ERR_NOT_FOUND;
    }
    out->size = st.size;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_size(size_t s, uint64_t *size)
{
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
    *size = (uint64_t)eof;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_rename(crux_file_root_t index, const char *from, const char *to)
{
    crux_file_info_t source;
    crux_file_result_t rc = crux_file_impl_stat(index, from, &source);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    char from_path[sizeof(roots[0]) + CRUX_FILE_NATIVE_LEAF_MAX + 1];
    char to_path[sizeof(roots[0]) + CRUX_FILE_NATIVE_LEAF_MAX + 1];
    rc = make_path(from_path, sizeof(from_path), index, from);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    rc = make_path(to_path, sizeof(to_path), index, to);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    /* fatfs_rename unlinks an existing destination before its f_rename, so this check under the
     * File lock is what keeps the destination. Any entry counts, because an empty directory would
     * go too. */
    struct fs_dirent st;
    int fs_rc = fs_stat(to_path, &st);
    if (fs_rc == 0) {
        return CRUX_FILE_ERR_EXISTS;
    }
    if (fs_rc != -ENOENT) {
        return result(fs_rc);
    }
    return result(fs_rename(from_path, to_path));
}

crux_file_result_t crux_file_impl_dir_open(size_t d, crux_file_root_t index)
{
    crux_file_result_t rc = ready(index);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    fs_dir_t_init(&listings[d]);
    return result(fs_opendir(&listings[d], roots[index]));
}

crux_file_result_t crux_file_impl_dir_read(size_t d, char *name, size_t capacity,
                                           crux_file_info_t *info, bool *end)
{
    for (;;) {
        struct fs_dirent entry;
        int rc = fs_readdir(&listings[d], &entry);
        if (rc < 0) {
            return result(rc);
        }
        if (entry.name[0] == '\0') {
            *end = true;
            return CRUX_FILE_OK;
        }
        if (entry.type == FS_DIR_ENTRY_FILE && decode_leaf(entry.name, name, capacity)) {
            info->size = entry.size;
            return CRUX_FILE_OK;
        }
    }
}

crux_file_result_t crux_file_impl_dir_close(size_t d)
{
    int rc = fs_closedir(&listings[d]);
    fs_dir_t_init(&listings[d]); /* backend may release data even on failure */
    return result(rc);
}

void crux_file_record_cleanup_error(crux_file_result_t error)
{
    cleanup_error = error;
}

crux_file_result_t crux_file_zephyr_last_cleanup_error(void)
{
    return cleanup_error;
}
