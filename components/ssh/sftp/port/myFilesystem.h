/*
 * The WOLFSSH_USER_FILESYSTEM port, which wolfssh/port.h includes by this name. Every operation
 * here refuses, so #192 and the probe build and link against it before #194 exists. It proves
 * nothing about the behaviour of SFTP. #194 owns this file and replaces it with the real port.
 */
#ifndef CRUX_SSH_SFTP_FILESYSTEM_H
#define CRUX_SSH_SFTP_FILESYSTEM_H

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

int crux_ssh_sftp_port_refuse(void);
#define CRUX_SSH_SFTP_REFUSE(...) crux_ssh_sftp_port_refuse()

#define WFOPEN CRUX_SSH_SFTP_REFUSE
#define WFCLOSE CRUX_SSH_SFTP_REFUSE
#define WFREAD CRUX_SSH_SFTP_REFUSE
#define WFWRITE CRUX_SSH_SFTP_REFUSE
#define WFSEEK CRUX_SSH_SFTP_REFUSE
#define WFTELL CRUX_SSH_SFTP_REFUSE
#define WREWIND CRUX_SSH_SFTP_REFUSE
#define WOPEN CRUX_SSH_SFTP_REFUSE
#define WCLOSE CRUX_SSH_SFTP_REFUSE
#define WPREAD CRUX_SSH_SFTP_REFUSE
#define WPWRITE CRUX_SSH_SFTP_REFUSE
#define WMKDIR CRUX_SSH_SFTP_REFUSE
#define WRMDIR CRUX_SSH_SFTP_REFUSE
#define WREMOVE CRUX_SSH_SFTP_REFUSE
#define WRENAME CRUX_SSH_SFTP_REFUSE
#define WSTAT CRUX_SSH_SFTP_REFUSE
#define WLSTAT CRUX_SSH_SFTP_REFUSE
#define WFSTAT CRUX_SSH_SFTP_REFUSE
#define WCHMOD CRUX_SSH_SFTP_REFUSE
#define WFCHMOD CRUX_SSH_SFTP_REFUSE
#define WCHOWN CRUX_SSH_SFTP_REFUSE
#define WFCHOWN CRUX_SSH_SFTP_REFUSE
#define WSETTIME CRUX_SSH_SFTP_REFUSE
#define WFSETTIME CRUX_SSH_SFTP_REFUSE
#define WTRUNCATE CRUX_SSH_SFTP_REFUSE
#define WFTRUNCATE CRUX_SSH_SFTP_REFUSE
#define WOPENDIR CRUX_SSH_SFTP_REFUSE
#define WCLOSEDIR CRUX_SSH_SFTP_REFUSE
#define WREADDIR CRUX_SSH_SFTP_REFUSE
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

#endif /* CRUX_SSH_SFTP_FILESYSTEM_H */
