/*
 * The functions that wolfSSH expects from a WOLFSSH_USER_FILESYSTEM port, in its own types. Each is
 * a translation onto crux_ssh_sftp_ops.h, so that nothing to decide is left in code that the Host
 * tests cannot reach.
 */
#include <wolfssh/internal.h>
#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>

#include <string.h>

_Static_assert(CRUX_SSH_SFTP_MODE_FILE == FILEATRB_PER_FILE, "SFTP version 3 fixes this value");
_Static_assert(CRUX_SSH_SFTP_MODE_DIRECTORY == FILEATRB_PER_DIR, "SFTP version 3 fixes this value");

int crux_ssh_sftp_port_refuse(void)
{
    return -1;
}

static void attributes_to_wolf(const crux_ssh_sftp_attributes_t *from, WS_SFTP_FILEATRB *to)
{
    memset(to, 0, sizeof(*to));
    /* F6: no time, so that a client does not read one as true. */
    to->flags = WOLFSSH_FILEATRB_SIZE | WOLFSSH_FILEATRB_PERM;
    to->sz[0] = (word32)(from->size & 0xffffffffu);
    to->sz[1] = (word32)(from->size >> 32);
    to->per = from->mode;
}

int SFTP_GetAttributes(void *fs, const char *fileName, WS_SFTP_FILEATRB *atr, byte noFollow,
                       void *heap)
{
    crux_ssh_sftp_attributes_t found;

    /* There are no links to follow. */
    (void)noFollow;
    (void)heap;
    if (crux_ssh_sftp_attributes(fs, fileName, &found) != 0) {
        return WS_BAD_FILE_E;
    }
    attributes_to_wolf(&found, atr);
    return WS_SUCCESS;
}

int SFTP_GetAttributes_Handle(WOLFSSH *ssh, WFD fd, char *name, WS_SFTP_FILEATRB *atr)
{
    crux_ssh_sftp_attributes_t found;

    (void)name;
    if (crux_ssh_sftp_file_attributes(wolfSSH_GetFilesystemHandle(ssh), fd, &found) != 0) {
        return WS_BAD_FILE_E;
    }
    attributes_to_wolf(&found, atr);
    return WS_SUCCESS;
}

/*
 * wolfSSH puts every entry of a READDIR into one NAME reply, by calling this until it returns
 * something other than WS_SUCCESS. WS_NEXT_ERROR would add an empty entry, so the end of a listing
 * and a failure are both WS_FATAL_ERROR, and wolfSSH treats them alike: see docs/sftp-design.md.
 */
int SFTP_Name_readdir(void *fs, WDIR *dir, WS_SFTPNAME *out)
{
    char name[CRUX_SSH_SFTP_NAME_CAPACITY];
    crux_ssh_sftp_attributes_t found;
    size_t length;

    if (crux_ssh_sftp_dir_read(fs, *dir, name, sizeof(name), &found) != 0) {
        return WS_FATAL_ERROR;
    }
    length = strlen(name);
    out->fName = (char *)WMALLOC(length + 1u, out->heap, DYNTYPE_SFTP);
    if (out->fName == NULL) {
        return WS_MEMORY_E;
    }
    memcpy(out->fName, name, length + 1u);
    out->fSz = (word32)length;
    attributes_to_wolf(&found, &out->atrb);
    return WS_SUCCESS;
}
