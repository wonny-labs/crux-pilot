/*
 * SFTP for an image with no file service. It keeps the entry points that the callback table needs,
 * and the operations that the wolfSSH port calls, and it refuses all of them. A composition selects
 * the real ones with CRUX_SSH_SFTP_FILE_SERVICE: see sources.cmake.
 */
#include "crux_ssh_callbacks.h"
#include "crux_ssh_sftp_ops.h"

crux_status_t crux_ssh_sftp_open(void *context, crux_ssh_attachment_t *attachment,
                                 crux_ssh_file_handle_t **handle)
{
    (void)context;
    (void)attachment;
    (void)handle;
    return CRUX_ERR_NOT_SUPPORTED;
}

void crux_ssh_sftp_close(void *context, crux_ssh_attachment_t *attachment,
                         crux_ssh_file_handle_t *handle)
{
    (void)context;
    (void)attachment;
    (void)handle;
}

int crux_ssh_sftp_file_open(void *fs, const char *path, int flags)
{
    (void)fs;
    (void)path;
    (void)flags;
    return -1;
}

int crux_ssh_sftp_file_close(void *fs, int fd)
{
    (void)fs;
    (void)fd;
    return -1;
}

int crux_ssh_sftp_file_pread(void *fs, int fd, void *buffer, uint32_t size,
                             const uint32_t offset[2])
{
    (void)fs;
    (void)fd;
    (void)buffer;
    (void)size;
    (void)offset;
    return -1;
}

int crux_ssh_sftp_file_pwrite(void *fs, int fd, const void *buffer, uint32_t size,
                              const uint32_t offset[2])
{
    (void)fs;
    (void)fd;
    (void)buffer;
    (void)size;
    (void)offset;
    return -1;
}

int crux_ssh_sftp_file_append(void *fs, int fd, const void *buffer, uint32_t size)
{
    (void)fs;
    (void)fd;
    (void)buffer;
    (void)size;
    return -1;
}

int crux_ssh_sftp_file_attributes(void *fs, int fd, crux_ssh_sftp_attributes_t *attributes)
{
    (void)fs;
    (void)fd;
    (void)attributes;
    return -1;
}

int crux_ssh_sftp_file_set_attributes(void *fs, int fd)
{
    (void)fs;
    (void)fd;
    return -1;
}

int crux_ssh_sftp_remove(void *fs, const char *path)
{
    (void)fs;
    (void)path;
    return -1;
}

int crux_ssh_sftp_rename(void *fs, const char *from, const char *to)
{
    (void)fs;
    (void)from;
    (void)to;
    return -1;
}

int crux_ssh_sftp_attributes(void *fs, const char *path, crux_ssh_sftp_attributes_t *attributes)
{
    (void)fs;
    (void)path;
    (void)attributes;
    return -1;
}

int crux_ssh_sftp_set_attributes(void *fs, const char *path)
{
    (void)fs;
    (void)path;
    return -1;
}

int crux_ssh_sftp_dir_open(void *fs, const char *path, int *dir)
{
    (void)fs;
    (void)path;
    (void)dir;
    return -1;
}

int crux_ssh_sftp_dir_read(void *fs, int dir, char *name, size_t capacity,
                           crux_ssh_sftp_attributes_t *attributes)
{
    (void)fs;
    (void)dir;
    (void)name;
    (void)capacity;
    (void)attributes;
    return -1;
}

int crux_ssh_sftp_dir_close(void *fs, int dir)
{
    (void)fs;
    (void)dir;
    return -1;
}

char *crux_ssh_sftp_start_directory(char *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    return NULL;
}
