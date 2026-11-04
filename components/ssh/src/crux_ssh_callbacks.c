#include "crux_ssh_callbacks.h"

static const crux_ssh_callbacks_t table = {
    .context = NULL,
    .password = crux_ssh_access_password,
    .key_check = crux_ssh_access_key_check,
    .key_verified = crux_ssh_access_key_verified,
    .key_failed = crux_ssh_access_key_failed,
    .shell = crux_ssh_cli_shell,
    .exec = crux_ssh_cli_exec,
    .sftp_open = crux_ssh_sftp_open,
    .sftp_close = crux_ssh_sftp_close,
    .session_alive = crux_ssh_access_session_alive,
    .connection_closed = crux_ssh_access_connection_closed,
};

const crux_ssh_callbacks_t *crux_ssh_callback_table(void)
{
    return &table;
}
