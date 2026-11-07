/* The shape of the SSH adapter of Issue #193: it links crux_cli_adapter PRIVATE, and this file is
 * the only one that sees the transport. */
#include <crux_cli_adapter.h>

int cli_adapter_user_exec(crux_cli_context_t *context, const crux_cli_io_t *io,
                          crux_access_session_t session, const char *command, size_t length);

int cli_adapter_user_exec(crux_cli_context_t *context, const crux_cli_io_t *io,
                          crux_access_session_t session, const char *command, size_t length)
{
    int status = 1;
    (void)crux_cli_exec(context, io, CRUX_ACCESS_CHANNEL_SSH, session, command, length, &status);
    return status;
}
