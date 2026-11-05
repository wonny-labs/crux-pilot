/*
 * The command line over SSH (Issue #193). Crux code only: a project never includes this header. The
 * entry points behind the shell and exec callbacks are declared in crux_ssh_callbacks.h. See
 * docs/cli-over-ssh.md.
 */
#ifndef CRUX_SSH_CLI_H
#define CRUX_SSH_CLI_H

#include "crux_ssh_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Names the provider whose channels the entry points read and write. The shared callback table
 * passes no context, so a composition states it here, before it calls the provider's start(). Takes
 * no lock, and must return before any session starts, as crux_cli_init() must.
 *
 * Until this succeeds, each session is refused with exit status 255, and nothing is written.
 *
 * @param provider Borrowed for the program's life.
 * @retval CRUX_OK Sessions use this provider.
 * @retval CRUX_ERR_INVALID_ARGUMENT It is NULL or lacks read, write or close. No provider is then
 * set, so sessions are refused.
 */
crux_status_t crux_ssh_cli_set_provider(const crux_ssh_provider_t *provider);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_CLI_H */
