/*
 * Stand-in for the WOLFSSH_USER_FILESYSTEM port of Issue #194, to prove that the settings
 * arrangement reaches this path in a combined image. Every operation fails. It is not a port and
 * proves nothing about the behaviour of SFTP. wolfssh/port.h includes it by this name.
 */
#ifndef CRUX_SSH_PROBE_FILESYSTEM_H
#define CRUX_SSH_PROBE_FILESYSTEM_H

#include <stddef.h>
#include <stdint.h>

#define WOLFSSH_SFTP_NAME_READDIR

#define WS_DELIM '/'
#define WOLFSSH_MAX_FILENAME 64
#define WOLFSSH_O_RDWR 0x01
#define WOLFSSH_O_RDONLY 0x02
#define WOLFSSH_O_WRONLY 0x04
#define WOLFSSH_O_APPEND 0x08
#define WOLFSSH_O_CREAT 0x10
#define WOLFSSH_O_TRUNC 0x20
#define WOLFSSH_O_EXCL 0x40

#define WFILE int
#define WFD int
#define WBADFILE (-1)
#define WDIR int
#define WSEEK_END 2
#define WSTAT_T int

int crux_ssh_probe_fs_fail(void);
#define CRUX_SSH_PROBE_FAIL(...) crux_ssh_probe_fs_fail()

#define WFOPEN CRUX_SSH_PROBE_FAIL
#define WFCLOSE CRUX_SSH_PROBE_FAIL
#define WFREAD CRUX_SSH_PROBE_FAIL
#define WFWRITE CRUX_SSH_PROBE_FAIL
#define WFSEEK CRUX_SSH_PROBE_FAIL
#define WFTELL CRUX_SSH_PROBE_FAIL
#define WREWIND CRUX_SSH_PROBE_FAIL
#define WOPEN CRUX_SSH_PROBE_FAIL
#define WCLOSE CRUX_SSH_PROBE_FAIL
#define WPREAD CRUX_SSH_PROBE_FAIL
#define WPWRITE CRUX_SSH_PROBE_FAIL
#define WMKDIR CRUX_SSH_PROBE_FAIL
#define WRMDIR CRUX_SSH_PROBE_FAIL
#define WREMOVE CRUX_SSH_PROBE_FAIL
#define WRENAME CRUX_SSH_PROBE_FAIL
#define WSTAT CRUX_SSH_PROBE_FAIL
#define WLSTAT CRUX_SSH_PROBE_FAIL
#define WFSTAT CRUX_SSH_PROBE_FAIL
#define WCHMOD CRUX_SSH_PROBE_FAIL
#define WFCHMOD CRUX_SSH_PROBE_FAIL
#define WCHOWN CRUX_SSH_PROBE_FAIL
#define WFCHOWN CRUX_SSH_PROBE_FAIL
#define WSETTIME CRUX_SSH_PROBE_FAIL
#define WFSETTIME CRUX_SSH_PROBE_FAIL
#define WTRUNCATE CRUX_SSH_PROBE_FAIL
#define WFTRUNCATE CRUX_SSH_PROBE_FAIL
#define WOPENDIR CRUX_SSH_PROBE_FAIL
#define WCLOSEDIR CRUX_SSH_PROBE_FAIL
#define WREADDIR CRUX_SSH_PROBE_FAIL
#define WGETCWD(fs, buf, size) ((char *)0)

/* The port's functions, which wolfsftp.c calls and does not define under WOLFSSH_USER_FILESYSTEM.
 */
typedef struct WS_SFTP_FILEATRB WS_SFTP_FILEATRB;
typedef struct WS_SFTPNAME WS_SFTPNAME;
typedef struct WOLFSSH WOLFSSH;
int SFTP_GetAttributes(void *fs, const char *fileName, WS_SFTP_FILEATRB *atr, byte noFollow,
                       void *heap);
int SFTP_GetAttributes_Handle(WOLFSSH *ssh, WFD fd, char *name, WS_SFTP_FILEATRB *atr);
int SFTP_Name_readdir(void *fs, WDIR *dir, WS_SFTPNAME *out);

#endif /* CRUX_SSH_PROBE_FILESYSTEM_H */
