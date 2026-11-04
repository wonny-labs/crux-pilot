/* Refuses every session until #193 replaces it. It is here so that the table links from the start.
 */
#include "crux_ssh_callbacks.h"

#define CRUX_SSH_CLI_REFUSED 255

int crux_ssh_cli_shell(void *context, crux_ssh_attachment_t *attachment,
                       crux_ssh_channel_t *channel, bool terminal)
{
    (void)context;
    (void)attachment;
    (void)channel;
    (void)terminal;
    return CRUX_SSH_CLI_REFUSED;
}

int crux_ssh_cli_exec(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                      const char *command, size_t command_length)
{
    (void)context;
    (void)attachment;
    (void)channel;
    (void)command;
    (void)command_length;
    return CRUX_SSH_CLI_REFUSED;
}
