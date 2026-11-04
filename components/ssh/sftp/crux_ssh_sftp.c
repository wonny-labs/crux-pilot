/* Refuses SFTP until #194 replaces it. It is here so that the table links from the start. */
#include "crux_ssh_callbacks.h"

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
