/* Compiled as Crux-owned code is, to show that a provider can include wolfSSH under that policy. */
#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>

#ifndef WOLFSSH_USER_FILESYSTEM
#error "Crux code must see the same wolfSSH settings as wolfSSH"
#endif

int crux_ssh_probe_owned(WOLFSSH *ssh, void *handle);

int crux_ssh_probe_owned(WOLFSSH *ssh, void *handle)
{
    return wolfSSH_SetFilesystemHandle(ssh, handle);
}
