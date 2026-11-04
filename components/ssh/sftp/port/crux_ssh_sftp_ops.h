/*
 * The file operations behind the WOLFSSH_USER_FILESYSTEM macros in myFilesystem.h. Crux code only.
 * They name no wolfSSH type, so that the Host tests run them with no SSH library, and every macro
 * is a thin call to one of them.
 *
 * Each runs on the worker of its connection, with the per-session handle that wolfSSH passes as its
 * `fs` argument. A negative result is a refusal, and what wolfSSH then sends for it is in
 * docs/sftp-design.md. The file service decides every refusal that concerns a session, a permission
 * or a limit: nothing here adds a rule of that kind.
 */
#ifndef CRUX_SSH_SFTP_OPS_H
#define CRUX_SSH_SFTP_OPS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The open flags. wolfSSH reads them through the WOLFSSH_O_ names in myFilesystem.h. */
#define CRUX_SSH_SFTP_O_RDWR 0x01
#define CRUX_SSH_SFTP_O_RDONLY 0x02
#define CRUX_SSH_SFTP_O_WRONLY 0x04
#define CRUX_SSH_SFTP_O_APPEND 0x08
#define CRUX_SSH_SFTP_O_CREAT 0x10
#define CRUX_SSH_SFTP_O_TRUNC 0x20
#define CRUX_SSH_SFTP_O_EXCL 0x40

/* The type bits of a mode, with the values SFTP version 3 gives them. */
#define CRUX_SSH_SFTP_MODE_FILE 0100000u
#define CRUX_SSH_SFTP_MODE_DIRECTORY 0040000u

/* The name buffer that crux_ssh_sftp_dir_read() needs, terminator included. */
#define CRUX_SSH_SFTP_NAME_CAPACITY 64u

/** What a client sees of a file or a directory. F6 leaves out any time. */
typedef struct {
    uint64_t size;
    uint32_t mode; /**< A type bit, and rights derived from the session. */
} crux_ssh_sftp_attributes_t;

/** Returns a descriptor that is zero or more, or -1. */
int crux_ssh_sftp_file_open(void *fs, const char *path, int flags);
int crux_ssh_sftp_file_close(void *fs, int fd);
/** Bytes read, 0 at or past the end of the file, or -1. offset[0] is the low word. */
int crux_ssh_sftp_file_pread(void *fs, int fd, void *buffer, uint32_t size,
                             const uint32_t offset[2]);
/** Bytes written, which can be fewer than size, or -1. offset[0] is the low word. */
int crux_ssh_sftp_file_pwrite(void *fs, int fd, const void *buffer, uint32_t size,
                              const uint32_t offset[2]);
/** The same at the current end of the file, for a handle that the client opened to append. */
int crux_ssh_sftp_file_append(void *fs, int fd, const void *buffer, uint32_t size);
int crux_ssh_sftp_file_attributes(void *fs, int fd, crux_ssh_sftp_attributes_t *attributes);
/** Accepted without effect, for an open file. */
int crux_ssh_sftp_file_set_attributes(void *fs, int fd);

int crux_ssh_sftp_remove(void *fs, const char *path);
int crux_ssh_sftp_rename(void *fs, const char *from, const char *to);
int crux_ssh_sftp_attributes(void *fs, const char *path, crux_ssh_sftp_attributes_t *attributes);
/** Accepted without effect, for a path in an area the session can see. */
int crux_ssh_sftp_set_attributes(void *fs, const char *path);

int crux_ssh_sftp_dir_open(void *fs, const char *path, int *dir);
/** Returns 0 with an entry, 1 at the end of the listing, or -1. */
int crux_ssh_sftp_dir_read(void *fs, int dir, char *name, size_t capacity,
                           crux_ssh_sftp_attributes_t *attributes);
int crux_ssh_sftp_dir_close(void *fs, int dir);

/** The directory a session starts in, the virtual root. Returns buffer, or NULL if it is short. */
char *crux_ssh_sftp_start_directory(char *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_SFTP_OPS_H */
