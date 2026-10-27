#include "file_native.h"
#include "file_port.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

/* Target writable storage is intentionally not enabled by this provider PoC. */
#if !defined(CONFIG_BOARD_NATIVE_SIM) || !defined(CONFIG_FLASH_SIMULATOR)
#error "File provider is qualified for simulated Flash only"
#endif
K_MUTEX_DEFINE(file_lock);
static struct fs_file_t handles[CRUX_FILE_POOL_SIZE];
static char root[64];
static char paths[CRUX_FILE_POOL_SIZE][sizeof(root) + CRUX_FILE_NAME_MAX + 1];
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

void crux_file_port_lock(void)
{
    __ASSERT_NO_MSG(!k_is_in_isr());
    if (k_mutex_lock(&file_lock, K_FOREVER) != 0)
        k_panic();
}

void crux_file_port_unlock(void)
{
    if (k_mutex_unlock(&file_lock) != 0)
        k_panic();
}

crux_file_result_t crux_file_zephyr_bind(const char *r)
{
    if (!r || r[0] != '/')
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    size_t length = strlen(r);
    if (length < 2 || length >= sizeof(root) || r[length - 1] == '/')
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    if (root[0])
        return CRUX_FILE_ERR_INVALID_STATE;
    struct fs_dirent st;
    int rc = fs_stat(r, &st);
    if (rc < 0)
        return result(rc);
    if (st.type != FS_DIR_ENTRY_DIR)
        return CRUX_FILE_ERR_NOT_SUPPORTED;
    memcpy(root, r, length + 1);
    return CRUX_FILE_OK;
}

static crux_file_result_t ready(void)
{
    struct fs_dirent st;
    if (!root[0])
        return CRUX_FILE_ERR_NOT_READY;
    int rc = fs_stat(root, &st);
    if (rc == -ENOENT)
        return CRUX_FILE_ERR_NOT_READY;
    if (rc < 0)
        return result(rc);
    return st.type == FS_DIR_ENTRY_DIR ? CRUX_FILE_OK : CRUX_FILE_ERR_NOT_READY;
}

crux_file_result_t crux_file_port_open(size_t s, const char *name, crux_file_mode_t mode)
{
    crux_file_result_t rc = ready();
    if (rc != CRUX_FILE_OK)
        return rc;
    int n = snprintf(paths[s], sizeof(paths[s]), "%s/%s", root, name);
    if (n < 0 || (size_t)n >= sizeof(paths[s]))
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    fs_mode_t flags = mode == CRUX_FILE_READ ? FS_O_READ : FS_O_READ | FS_O_WRITE;
    if (mode == CRUX_FILE_CREATE_REPLACE)
        flags |= FS_O_CREATE;
    fs_file_t_init(&handles[s]);
    return result(fs_open(&handles[s], paths[s], flags));
}

crux_file_result_t crux_file_port_truncate(size_t s)
{
    return result(fs_truncate(&handles[s], 0));
}

crux_file_result_t crux_file_port_read(size_t s, void *b, size_t l, size_t *n)
{
    if (l > INT32_MAX)
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    ssize_t rc = fs_read(&handles[s], b, l);
    if (rc < 0)
        return result((int)rc);
    *n = (size_t)rc;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_port_write(size_t s, const void *b, size_t l, size_t *n)
{
    if (l > INT32_MAX)
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    ssize_t rc = fs_write(&handles[s], b, l);
    if (rc < 0)
        return result((int)rc);
    *n = (size_t)rc;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_port_seek(size_t s, uint64_t offset)
{
    _Static_assert(sizeof(off_t) == 4 || sizeof(off_t) == 8, "supported VFS offsets");
    /* LittleFS uses signed 32-bit offsets/counts, even with 64-bit off_t. */
    if (offset > INT32_MAX)
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    off_t original = fs_tell(&handles[s]);
    if (original < 0)
        return result((int)original);
    int rc = fs_seek(&handles[s], 0, FS_SEEK_END);
    if (rc < 0)
        return result(rc);
    off_t eof = fs_tell(&handles[s]);
    int restored = fs_seek(&handles[s], original, FS_SEEK_SET);
    if (eof < 0)
        return result((int)eof);
    if (restored < 0)
        return result(restored);
    if (offset > (uint64_t)eof)
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    return result(fs_seek(&handles[s], (off_t)offset, FS_SEEK_SET));
}

crux_file_result_t crux_file_port_sync(size_t s)
{
    return result(fs_sync(&handles[s]));
}

crux_file_result_t crux_file_port_close(size_t s)
{
    int rc = fs_close(&handles[s]);
    fs_file_t_init(&handles[s]); /* backend may release data even on failure */
    return result(rc);
}

crux_file_result_t crux_file_port_remove(const char *name)
{
    crux_file_result_t rc = ready();
    if (rc != CRUX_FILE_OK)
        return rc;
    char path[sizeof(root) + CRUX_FILE_NAME_MAX + 1];
    int n = snprintf(path, sizeof(path), "%s/%s", root, name);
    if (n < 0 || (size_t)n >= sizeof(path))
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    return result(fs_unlink(path));
}

void crux_file_port_cleanup_error(crux_file_result_t error)
{
    cleanup_error = error;
}

crux_file_result_t crux_file_zephyr_last_cleanup_error(void)
{
    return cleanup_error;
}
