#include "file_linux.h"
#include "file_port.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(sizeof(off_t) >= sizeof(int64_t), "Host requires 64-bit offsets");
static struct {
    bool bound;
    int fd;
    char path[PATH_MAX];
} roots[CRUX_FILE_ROOTS_MAX];
static int descriptors[CRUX_FILE_POOL_SIZE];
static DIR *listings[CRUX_FILE_DIR_POOL_SIZE];
static crux_file_result_t cleanup_error;

crux_file_result_t crux_file_linux_error(int e)
{
    switch (e) {
    case 0:
        return CRUX_FILE_OK;
    case EINVAL:
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    case ENOENT:
        return CRUX_FILE_ERR_NOT_FOUND;
    case ENOSPC:
    case EDQUOT:
        return CRUX_FILE_ERR_NO_SPACE;
    case EMFILE:
    case ENFILE:
    case ENOMEM:
        return CRUX_FILE_ERR_NO_RESOURCES;
    case EACCES:
    case EPERM:
    case EROFS:
        return CRUX_FILE_ERR_ACCESS;
    case EOVERFLOW:
    case EFBIG:
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    case ENOSYS:
    case EOPNOTSUPP:
        return CRUX_FILE_ERR_NOT_SUPPORTED;
    default:
        return CRUX_FILE_ERR_IO;
    }
}

crux_file_result_t crux_file_linux_bind_root(crux_file_root_t root, const char *r)
{
    if (!r || root >= CRUX_FILE_ROOTS_MAX) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (roots[root].bound) {
        return CRUX_FILE_ERR_INVALID_STATE;
    }
    for (size_t i = 0; i < CRUX_FILE_ROOTS_MAX; i++) {
        if (roots[i].bound && crux_file_paths_overlap(roots[i].path, r)) {
            return CRUX_FILE_ERR_INVALID_STATE;
        }
    }

    int fd = open(r, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return crux_file_linux_error(errno);
    }

    /* A path that opened is shorter than PATH_MAX. */
    size_t length = strlen(r);
    if (length >= sizeof(roots[root].path)) {
        (void)close(fd);
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }
    memcpy(roots[root].path, r, length + 1);
    roots[root].fd = fd;
    roots[root].bound = true;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_linux_bind(const char *r)
{
    return crux_file_linux_bind_root(CRUX_FILE_ROOT_DEFAULT, r);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a port signature, named for its callers
crux_file_result_t crux_file_impl_open(crux_file_root_t r, size_t s, const char *n,
                                       crux_file_mode_t m)
{
    if (!roots[r].bound) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    int flags = m == CRUX_FILE_READ ? O_RDONLY : O_RDWR;
    if (m == CRUX_FILE_CREATE_REPLACE) {
        flags |= O_CREAT;
    }

    int fd = openat(roots[r].fd, n, flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) {
        return crux_file_linux_error(errno);
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        int e = errno;
        (void)close(fd);
        return crux_file_linux_error(e);
    }

    if (!S_ISREG(st.st_mode)) {
        (void)close(fd);
        return CRUX_FILE_ERR_NOT_SUPPORTED;
    }

    descriptors[s] = fd;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_truncate(size_t s)
{
    return ftruncate(descriptors[s], 0) == 0 ? CRUX_FILE_OK : crux_file_linux_error(errno);
}

crux_file_result_t crux_file_impl_read(size_t s, void *b, size_t l, size_t *c)
{
    if (l > (size_t)SSIZE_MAX) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }

    ssize_t n = read(descriptors[s], b, l);
    if (n < 0) {
        return crux_file_linux_error(errno);
    }

    *c = (size_t)n;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_write(size_t s, const void *b, size_t l, size_t *c)
{
    if (l > (size_t)SSIZE_MAX) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }

    ssize_t n = write(descriptors[s], b, l);
    if (n < 0) {
        return crux_file_linux_error(errno);
    }

    *c = (size_t)n;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_seek(size_t s, uint64_t o)
{
    if (o > INT64_MAX) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }

    struct stat st;
    if (fstat(descriptors[s], &st) < 0) {
        return crux_file_linux_error(errno);
    }
    if (st.st_size < 0 || o > (uint64_t)st.st_size) {
        return CRUX_FILE_ERR_OUT_OF_RANGE;
    }

    return lseek(descriptors[s], (off_t)o, SEEK_SET) < 0 ? crux_file_linux_error(errno)
                                                         : CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_sync(size_t s)
{
    return fsync(descriptors[s]) == 0 ? CRUX_FILE_OK : crux_file_linux_error(errno);
}

crux_file_result_t crux_file_impl_close(size_t s)
{
    int fd = descriptors[s];
    descriptors[s] = -1;
    return close(fd) == 0 ? CRUX_FILE_OK : crux_file_linux_error(errno);
}

crux_file_result_t crux_file_impl_remove(crux_file_root_t r, const char *n)
{
    if (!roots[r].bound) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    return unlinkat(roots[r].fd, n, 0) == 0 ? CRUX_FILE_OK : crux_file_linux_error(errno);
}

crux_file_result_t crux_file_impl_stat(crux_file_root_t r, const char *n, crux_file_info_t *out)
{
    if (!roots[r].bound) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    struct stat st;
    if (fstatat(roots[r].fd, n, &st, AT_SYMLINK_NOFOLLOW) < 0) {
        return crux_file_linux_error(errno);
    }
    if (!S_ISREG(st.st_mode)) {
        return CRUX_FILE_ERR_NOT_FOUND;
    }

    out->size = (uint64_t)st.st_size;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_size(size_t s, uint64_t *size)
{
    struct stat st;
    if (fstat(descriptors[s], &st) < 0) {
        return crux_file_linux_error(errno);
    }

    *size = (uint64_t)st.st_size;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_rename(crux_file_root_t r, const char *from, const char *to)
{
    crux_file_info_t source;
    crux_file_result_t result = crux_file_impl_stat(r, from, &source);
    if (result != CRUX_FILE_OK) {
        return result;
    }

    /* renameat replaces its destination, so this check under the File lock is what refuses one. A
     * link or directory counts as existing. */
    struct stat st;
    if (fstatat(roots[r].fd, to, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        return CRUX_FILE_ERR_EXISTS;
    }
    if (errno != ENOENT) {
        return crux_file_linux_error(errno);
    }

    return renameat(roots[r].fd, from, roots[r].fd, to) == 0 ? CRUX_FILE_OK
                                                             : crux_file_linux_error(errno);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a port signature, named for its callers
crux_file_result_t crux_file_impl_dir_open(size_t d, crux_file_root_t r)
{
    if (!roots[r].bound) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    int fd = openat(roots[r].fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return crux_file_linux_error(errno);
    }

    listings[d] = fdopendir(fd);
    if (!listings[d]) {
        int e = errno;
        (void)close(fd);
        return crux_file_linux_error(e);
    }
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_dir_read(size_t d, char *name, size_t capacity,
                                           crux_file_info_t *info, bool *end)
{
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(listings[d]);
        if (!entry) {
            if (errno != 0) {
                return crux_file_linux_error(errno);
            }
            *end = true;
            return CRUX_FILE_OK;
        }

        struct stat st;
        if (fstatat(dirfd(listings[d]), entry->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
            if (errno == ENOENT) {
                continue; /* Removed during the listing. */
            }
            return crux_file_linux_error(errno);
        }
        size_t length = strlen(entry->d_name);
        if (!S_ISREG(st.st_mode) || length >= capacity) {
            continue;
        }

        memcpy(name, entry->d_name, length + 1);
        info->size = (uint64_t)st.st_size;
        return CRUX_FILE_OK;
    }
}

crux_file_result_t crux_file_impl_dir_close(size_t d)
{
    DIR *listing = listings[d];
    listings[d] = NULL;
    return closedir(listing) == 0 ? CRUX_FILE_OK : crux_file_linux_error(errno);
}

void crux_file_record_cleanup_error(crux_file_result_t e)
{
    cleanup_error = e;
}

crux_file_result_t crux_file_linux_last_cleanup_error(void)
{
    return cleanup_error;
}
