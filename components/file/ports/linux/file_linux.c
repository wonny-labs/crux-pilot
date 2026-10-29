#include "file_linux.h"
#include "file_port.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(sizeof(off_t) >= sizeof(int64_t), "Host requires 64-bit offsets");
static int root_fd = -1;
static int descriptors[CRUX_FILE_POOL_SIZE];
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

crux_file_result_t crux_file_linux_bind(const char *r)
{
    if (!r) {
        return CRUX_FILE_ERR_INVALID_ARGUMENT;
    }
    if (root_fd >= 0) {
        return CRUX_FILE_ERR_INVALID_STATE;
    }

    int fd = open(r, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return crux_file_linux_error(errno);
    }

    root_fd = fd;
    return CRUX_FILE_OK;
}

crux_file_result_t crux_file_impl_open(size_t s, const char *n, crux_file_mode_t m)
{
    if (root_fd < 0) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    int flags = m == CRUX_FILE_READ ? O_RDONLY : O_RDWR;
    if (m == CRUX_FILE_CREATE_REPLACE) {
        flags |= O_CREAT;
    }

    int fd = openat(root_fd, n, flags | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
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

crux_file_result_t crux_file_impl_remove(const char *n)
{
    if (root_fd < 0) {
        return CRUX_FILE_ERR_NOT_READY;
    }

    return unlinkat(root_fd, n, 0) == 0 ? CRUX_FILE_OK : crux_file_linux_error(errno);
}

void crux_file_record_cleanup_error(crux_file_result_t e)
{
    cleanup_error = e;
}

crux_file_result_t crux_file_linux_last_cleanup_error(void)
{
    return cleanup_error;
}
