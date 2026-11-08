/*
 * The WOLFSSH_USER_FILESYSTEM port, which wolfssh/port.h includes by this name. Each macro that the
 * SFTP server uses is a call to one operation in crux_ssh_sftp_ops.h, with the session handle that
 * wolfSSH passes as `fs`. See docs/sftp-design.md.
 *
 * WOLFSSH_MAX_FILENAME sizes the path buffers of each request on the worker's stack. A path of that
 * length or more makes wolfSSH end the connection and not refuse the request, so it stays well
 * above the longest path the file service accepts, which is 80 bytes.
 */
#ifndef CRUX_SSH_SFTP_FILESYSTEM_H
#define CRUX_SSH_SFTP_FILESYSTEM_H

#include "crux_ssh_sftp_ops.h"

#include <stddef.h>
#include <stdint.h>

#define WOLFSSH_SFTP_NAME_READDIR

#define WS_DELIM '/'
#define WOLFSSH_MAX_FILENAME 256
#define WOLFSSH_O_RDWR CRUX_SSH_SFTP_O_RDWR
#define WOLFSSH_O_RDONLY CRUX_SSH_SFTP_O_RDONLY
#define WOLFSSH_O_WRONLY CRUX_SSH_SFTP_O_WRONLY
#define WOLFSSH_O_APPEND CRUX_SSH_SFTP_O_APPEND
#define WOLFSSH_O_CREAT CRUX_SSH_SFTP_O_CREAT
#define WOLFSSH_O_TRUNC CRUX_SSH_SFTP_O_TRUNC
#define WOLFSSH_O_EXCL CRUX_SSH_SFTP_O_EXCL

#define WFILE int
#define WFD int
#define WBADFILE (-1)
#define WDIR int
#define WSEEK_END 2
#define WSTAT_T int

/* The server's file operations. */
#define WOPEN(fs, path, flags, mode) crux_ssh_sftp_file_open((fs), (path), (flags))
#define WCLOSE(fs, fd) crux_ssh_sftp_file_close((fs), (fd))
#define WPREAD(fs, fd, buffer, size, offset)                                                       \
    crux_ssh_sftp_file_pread((fs), (fd), (buffer), (size), (offset))
#define WPWRITE(fs, fd, buffer, size, offset)                                                      \
    crux_ssh_sftp_file_pwrite((fs), (fd), (buffer), (size), (offset))
/* wolfSSH uses this for a handle that the client opened with SSH_FXF_APPEND. */
#define WWRITE(fs, fd, buffer, size) crux_ssh_sftp_file_append((fs), (fd), (buffer), (size))
#define WREMOVE(fs, path) crux_ssh_sftp_remove((fs), (path))
#define WRENAME(fs, from, to) crux_ssh_sftp_rename((fs), (from), (to))
#define WOPENDIR(fs, heap, dir, path) crux_ssh_sftp_dir_open((fs), (path), (dir))
#define WCLOSEDIR(fs, dir) crux_ssh_sftp_dir_close((fs), *(dir))
#define WGETCWD(fs, buffer, size) crux_ssh_sftp_start_directory((buffer), (size))

/* Only these two status paths let a port say "permission denied". There are no directories to make
 * or remove, whatever the session holds, so the result does not depend on it. */
#define WMKDIR(fs, path, mode) ((void)crux_ssh_sftp_directory_refused((fs), (path)), WS_PERMISSIONS)
#define WRMDIR(fs, path) ((void)crux_ssh_sftp_directory_refused((fs), (path)), WS_PERMISSIONS)

/* Accepted without effect: there are no permission bits or times (F6). The two macros for a size
 * and the two for an owner stay undefined, so wolfSSH answers those with "unsupported". */
#define WCHMOD(fs, path, mode) crux_ssh_sftp_set_attributes((fs), (path))
#define WFCHMOD(fs, fd, mode) crux_ssh_sftp_file_set_attributes((fs), (fd))
#define WSETTIME(fs, path, access, modified) crux_ssh_sftp_set_attributes((fs), (path))
#define WFSETTIME(fs, fd, access, modified) crux_ssh_sftp_file_set_attributes((fs), (fd))

/* What only the SFTP client role calls, or what WOLFSSH_SFTP_NAME_READDIR replaces. A Crux image
 * has no use for it, and it must still compile. Nothing here reaches a file. */
int crux_ssh_sftp_port_refuse(void);
#define CRUX_SSH_SFTP_REFUSE(...) crux_ssh_sftp_port_refuse()
#define WFOPEN CRUX_SSH_SFTP_REFUSE
#define WFCLOSE CRUX_SSH_SFTP_REFUSE
#define WFREAD CRUX_SSH_SFTP_REFUSE
#define WFWRITE CRUX_SSH_SFTP_REFUSE
#define WFSEEK CRUX_SSH_SFTP_REFUSE
#define WFTELL CRUX_SSH_SFTP_REFUSE
#define WREWIND CRUX_SSH_SFTP_REFUSE
#define WSTAT CRUX_SSH_SFTP_REFUSE
#define WLSTAT CRUX_SSH_SFTP_REFUSE
#define WFSTAT CRUX_SSH_SFTP_REFUSE
#define WREADDIR CRUX_SSH_SFTP_REFUSE

/* The port's functions, which wolfsftp.c calls and does not define under WOLFSSH_USER_FILESYSTEM.
 */
typedef struct WS_SFTP_FILEATRB WS_SFTP_FILEATRB;
typedef struct WS_SFTPNAME WS_SFTPNAME;
typedef struct WOLFSSH WOLFSSH;
int SFTP_GetAttributes(void *fs, const char *fileName, WS_SFTP_FILEATRB *atr, byte noFollow,
                       void *heap);
int SFTP_GetAttributes_Handle(WOLFSSH *ssh, WFD fd, char *name, WS_SFTP_FILEATRB *atr);
int SFTP_Name_readdir(void *fs, WDIR *dir, WS_SFTPNAME *out);

#endif /* CRUX_SSH_SFTP_FILESYSTEM_H */
