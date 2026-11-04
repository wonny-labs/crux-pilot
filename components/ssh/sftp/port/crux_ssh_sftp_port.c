/* The functions of the refusing port in myFilesystem.h. #194 replaces both files. */
#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>

int crux_ssh_sftp_port_refuse(void)
{
    return -1;
}

int SFTP_GetAttributes(void *fs, const char *fileName, WS_SFTP_FILEATRB *atr, byte noFollow,
                       void *heap)
{
    (void)fs;
    (void)fileName;
    (void)atr;
    (void)noFollow;
    (void)heap;
    return WS_BAD_FILE_E;
}

int SFTP_GetAttributes_Handle(WOLFSSH *ssh, WFD fd, char *name, WS_SFTP_FILEATRB *atr)
{
    (void)ssh;
    (void)fd;
    (void)name;
    (void)atr;
    return WS_BAD_FILE_E;
}

int SFTP_Name_readdir(void *fs, WDIR *dir, WS_SFTPNAME *out)
{
    (void)fs;
    (void)dir;
    (void)out;
    return WS_NEXT_ERROR;
}
