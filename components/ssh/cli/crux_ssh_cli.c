/*
 * The shell and exec entry points: a channel of the SSH provider as the CLI's transport. See
 * docs/cli-over-ssh.md.
 *
 * Each runs on the thread of its connection, with everything it holds on that thread's stack. This
 * file has no lock and creates no thread, so a client that stops reading holds one thread only, and
 * the CLI's session checks end that wait at the session's limits.
 */
#include "crux_ssh_cli.h"

#include "crux_ssh_attachment.h"
#include "crux_ssh_callbacks.h"

#include <crux_cli_adapter.h>

/*
 * A session's context holds a line and lives on the worker's stack, whose size is fixed by the
 * provider. A composition that needs a longer line raises that stack first, then this bound. See
 * docs/cli-over-ssh.md.
 */
#ifndef CRUX_SSH_CLI_LINE_MAX_ON_STACK
#define CRUX_SSH_CLI_LINE_MAX_ON_STACK 1024
#endif
_Static_assert(CRUX_CLI_LINE_MAX <= CRUX_SSH_CLI_LINE_MAX_ON_STACK,
               "A longer line needs a larger SSH worker stack: raise it, then this bound");

/* The provider sends this for a session that never started, as for any status out of range. */
#define CRUX_SSH_CLI_REFUSED 255

typedef struct {
    const crux_ssh_provider_t *provider;
    crux_ssh_channel_t *channel;
    crux_cli_line_ends_t line_ends;
} ssh_transport_t;

/* Set before any session starts and only read afterwards, as crux_cli_init() does for the registry.
 */
static const crux_ssh_provider_t *session_provider;

crux_status_t crux_ssh_cli_set_provider(const crux_ssh_provider_t *provider)
{
    if (provider == NULL || provider->read == NULL || provider->write == NULL ||
        provider->close == NULL) {
        session_provider = NULL;
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    session_provider = provider;
    return CRUX_OK;
}

/*
 * The table in docs/cli-contract.md. OK with a count outside what was asked is a provider against
 * its contract: it ends the session, because no byte count can be trusted after it.
 */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): both are byte counts of one call.
static int transport_result(crux_status_t status, size_t transferred, size_t length)
{
    if (status == CRUX_OK) {
        return transferred >= 1u && transferred <= length ? (int)transferred : -1;
    }
    return status == CRUX_ERR_TIMEOUT ? 0 : -1;
}

/* The provider applies its own io_timeout_ms, so timeout_ms goes unused. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): crux_cli_io_t fixes this signature.
static int ssh_read(void *transport, void *buffer, size_t length, uint32_t timeout_ms)
{
    const ssh_transport_t *ssh = transport;
    size_t transferred = 0;
    (void)timeout_ms;
    crux_status_t status = ssh->provider->read(ssh->channel, buffer, length, &transferred);
    return transport_result(status, transferred, length);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): crux_cli_io_t fixes this signature.
static int ssh_write(void *transport, const void *buffer, size_t length, uint32_t timeout_ms)
{
    const ssh_transport_t *ssh = transport;
    size_t transferred = 0;
    (void)timeout_ms;
    crux_status_t status = ssh->provider->write(ssh->channel, buffer, length, &transferred);
    return transport_result(status, transferred, length);
}

/* On a channel with a terminal, the same write goes through the line discipline. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): crux_cli_io_t fixes this signature.
static int ssh_terminal_write(void *transport, const void *buffer, size_t length,
                              uint32_t timeout_ms)
{
    ssh_transport_t *ssh = transport;
    return crux_cli_write_terminal(&ssh->line_ends, ssh_write, ssh, buffer, length, timeout_ms);
}

/* A zero session is never a valid one. Refusing here means nothing is written for it. */
static bool session_usable(const crux_ssh_channel_t *channel, crux_access_session_t session)
{
    return session_provider != NULL && channel != NULL && session.value != 0;
}

int crux_ssh_cli_shell(void *context, crux_ssh_attachment_t *attachment,
                       crux_ssh_channel_t *channel, bool terminal)
{
    (void)context;
    crux_access_session_t session = crux_ssh_attachment_session(attachment);
    if (!session_usable(channel, session)) {
        return CRUX_SSH_CLI_REFUSED;
    }
    const crux_ssh_provider_t *provider = session_provider;
    ssh_transport_t transport = {provider, channel, {false}};
    const crux_cli_io_t io = {ssh_read, terminal ? ssh_terminal_write : ssh_write, &transport,
                              terminal};
    crux_cli_context_t cli;
    crux_cli_result_t result = crux_cli_run(&cli, &io, CRUX_ACCESS_CHANNEL_SSH, session);
    (void)provider->close(channel);
    return result == CRUX_CLI_OK ? 0 : 1;
}

int crux_ssh_cli_exec(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                      const char *command, size_t command_length)
{
    (void)context;
    crux_access_session_t session = crux_ssh_attachment_session(attachment);
    if (!session_usable(channel, session) || command == NULL) {
        return CRUX_SSH_CLI_REFUSED;
    }
    const crux_ssh_provider_t *provider = session_provider;
    ssh_transport_t transport = {provider, channel, {false}};
    /*
     * No read: exec has no prompt, and a command that wants one is refused. No terminal: the
     * callback does not say whether the client asked for one, so a line ends as LF. The client's
     * `ssh -t` is the limit that docs/cli-over-ssh.md declares.
     */
    const crux_cli_io_t io = {NULL, ssh_write, &transport, false};
    crux_cli_context_t cli;
    int exit_status = 1;
    (void)crux_cli_exec(&cli, &io, CRUX_ACCESS_CHANNEL_SSH, session, command, command_length,
                        &exit_status);
    (void)provider->close(channel);
    return exit_status;
}
