/*
 * The port's macros, called as src/wolfsftp.c calls them at wolfSSH v1.6.0, with no wolfSSH header.
 * It holds the parts of the port that compile only where wolfSSH does: the argument order of each
 * macro, what WMKDIR gives, and which macros stay undefined. The operations behind them are in
 * sftp_test.c, and the real handlers with OpenSSH in tests/ssh/sftp/openssh.
 */
#include <stdint.h>

typedef unsigned char byte;
typedef uint32_t word32;
#define WS_PERMISSIONS (-1040)

#include "myFilesystem.h"
#include "ssh_check.h"

#include <string.h>

/* wolfSSH compiles SETSTAT with each of these only if it is defined, and otherwise it answers with
 * "unsupported". So a size or an owner cannot be accepted without effect by accident. */
#if defined(WTRUNCATE) || defined(WFTRUNCATE) || defined(WCHOWN) || defined(WFCHOWN)
#error "A size or an owner would be accepted and do nothing"
#endif
/* These select another port in wolfsftp.c. */
#ifndef WOLFSSH_SFTP_NAME_READDIR
#error "The listing hook is what gives the port its directories"
#endif
#if WOLFSSH_MAX_FILENAME <= 81
#error "The file service accepts 80 bytes, and wolfSSH ends the connection at this length"
#endif

static void *seen_fs;
static const void *seen_pointer;
static const void *seen_other;
static int seen_number;
static uint32_t seen_size;
static const uint32_t *seen_offset;
static unsigned seen_calls;

static void saw(void *fs)
{
    seen_fs = fs;
    seen_calls++;
}

int crux_ssh_sftp_file_open(void *fs, const char *path, int flags)
{
    saw(fs);
    seen_pointer = path;
    seen_number = flags;
    return 7;
}

int crux_ssh_sftp_file_close(void *fs, int fd)
{
    saw(fs);
    seen_number = fd;
    return 0;
}

int crux_ssh_sftp_file_pread(void *fs, int fd, void *buffer, uint32_t size,
                             const uint32_t offset[2])
{
    saw(fs);
    seen_number = fd;
    seen_pointer = buffer;
    seen_size = size;
    seen_offset = offset;
    return 1;
}

int crux_ssh_sftp_file_pwrite(void *fs, int fd, const void *buffer, uint32_t size,
                              const uint32_t offset[2])
{
    saw(fs);
    seen_number = fd;
    seen_pointer = buffer;
    seen_size = size;
    seen_offset = offset;
    return 2;
}

int crux_ssh_sftp_file_append(void *fs, int fd, const void *buffer, uint32_t size)
{
    saw(fs);
    seen_number = fd;
    seen_pointer = buffer;
    seen_size = size;
    return 3;
}

int crux_ssh_sftp_file_set_attributes(void *fs, int fd)
{
    saw(fs);
    seen_number = fd;
    return 0;
}

int crux_ssh_sftp_remove(void *fs, const char *path)
{
    saw(fs);
    seen_pointer = path;
    return 0;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the operation's own signature
int crux_ssh_sftp_rename(void *fs, const char *from, const char *to)
{
    saw(fs);
    seen_pointer = from;
    seen_other = to;
    return 0;
}

int crux_ssh_sftp_set_attributes(void *fs, const char *path)
{
    saw(fs);
    seen_pointer = path;
    return 0;
}

int crux_ssh_sftp_dir_open(void *fs, const char *path, int *dir)
{
    saw(fs);
    seen_pointer = path;
    *dir = 5;
    return 0;
}

int crux_ssh_sftp_dir_close(void *fs, int dir)
{
    saw(fs);
    seen_number = dir;
    return 0;
}

char *crux_ssh_sftp_start_directory(char *buffer, size_t size)
{
    seen_calls++;
    seen_size = (uint32_t)size;
    return buffer;
}

int main(void)
{
    static char session[1];
    void *fs = session;
    static char buffer[8];
    static const uint32_t offset[2] = {1, 2};
    WDIR dir = 0;
    WFD fd = WOPEN(fs, "/a/b", WOLFSSH_O_WRONLY | WOLFSSH_O_CREAT, 0644);
    SSH_CHECK(fd == 7);
    SSH_CHECK(seen_fs == fs);
    SSH_CHECK(strcmp(seen_pointer, "/a/b") == 0);
    SSH_CHECK(seen_number == (CRUX_SSH_SFTP_O_WRONLY | CRUX_SSH_SFTP_O_CREAT));

    SSH_CHECK(WPREAD(fs, fd, buffer, 4u, offset) == 1);
    SSH_CHECK(seen_number == 7 && seen_pointer == buffer && seen_size == 4u);
    SSH_CHECK(seen_offset == offset);
    SSH_CHECK(WPWRITE(fs, fd, buffer, 5u, offset) == 2);
    SSH_CHECK(seen_number == 7 && seen_pointer == buffer && seen_size == 5u);
    SSH_CHECK(seen_offset == offset);
    SSH_CHECK(WWRITE(fs, fd, buffer, 6u) == 3);
    SSH_CHECK(seen_number == 7 && seen_pointer == buffer && seen_size == 6u);
    SSH_CHECK(WCLOSE(fs, fd) == 0);
    SSH_CHECK(seen_number == 7);

    SSH_CHECK(WREMOVE(fs, "/a/c") == 0);
    SSH_CHECK(strcmp(seen_pointer, "/a/c") == 0);
    SSH_CHECK(WRENAME(fs, "/a/from", "/a/to") == 0);
    SSH_CHECK(strcmp(seen_pointer, "/a/from") == 0 && strcmp(seen_other, "/a/to") == 0);

    SSH_CHECK(WOPENDIR(fs, NULL, &dir, "/a") == 0);
    SSH_CHECK(strcmp(seen_pointer, "/a") == 0 && dir == 5);
    SSH_CHECK(WCLOSEDIR(fs, &dir) == 0);
    SSH_CHECK(seen_number == 5);

    /* Only here does wolfSSH let a port choose the permission status. */
    SSH_CHECK(WMKDIR(fs, "/a/d", 0755) == WS_PERMISSIONS);
    SSH_CHECK(WRMDIR(fs, "/a/d") == WS_PERMISSIONS);

    unsigned before = seen_calls;
    SSH_CHECK(WCHMOD(fs, "/a/e", 0600) == 0);
    SSH_CHECK(strcmp(seen_pointer, "/a/e") == 0);
    SSH_CHECK(WSETTIME(fs, "/a/f", 1u, 2u) == 0);
    SSH_CHECK(strcmp(seen_pointer, "/a/f") == 0);
    SSH_CHECK(WFCHMOD(fs, 9, 0600) == 0);
    SSH_CHECK(seen_number == 9);
    SSH_CHECK(WFSETTIME(fs, 8, 1u, 2u) == 0);
    SSH_CHECK(seen_number == 8);
    SSH_CHECK(seen_calls == before + 4u);

    SSH_CHECK(WGETCWD(fs, buffer, sizeof(buffer) - 1u) == buffer);
    SSH_CHECK(seen_size == sizeof(buffer) - 1u);

    printf("%u failures\n", ssh_check_failures);
    return ssh_check_failures == 0 ? 0 : 1;
}
