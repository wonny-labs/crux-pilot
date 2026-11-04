/*
 * The entry points behind the provider's callbacks, and the table that holds them. Crux code only.
 * Each session implements its own entry points in its own directory, and none of them needs to
 * change this header or the table: see docs/ssh-provider-contract.md. Each signature is that of its
 * callback.
 */
#ifndef CRUX_SSH_CALLBACKS_H
#define CRUX_SSH_CALLBACKS_H

#include "crux_ssh_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* In components/ssh/access/, owned by #192. */
crux_ssh_auth_result_t crux_ssh_access_password(void *context, const crux_ssh_peer_t *peer,
                                                const char *user, const uint8_t *password,
                                                size_t password_length,
                                                crux_ssh_attachment_t **attachment);
crux_ssh_auth_result_t crux_ssh_access_key_check(void *context, const crux_ssh_peer_t *peer,
                                                 const char *user, crux_ssh_key_type_t key_type,
                                                 const uint8_t *key, size_t key_length,
                                                 bool signed_request);
crux_ssh_auth_result_t crux_ssh_access_key_verified(void *context, const crux_ssh_peer_t *peer,
                                                    const char *user, crux_ssh_key_type_t key_type,
                                                    const uint8_t *key, size_t key_length,
                                                    crux_ssh_attachment_t **attachment);
void crux_ssh_access_key_failed(void *context, const crux_ssh_peer_t *peer, const char *user);
bool crux_ssh_access_session_alive(void *context, crux_ssh_attachment_t *attachment);
void crux_ssh_access_connection_closed(void *context, crux_ssh_attachment_t *attachment);

/* In components/ssh/cli/, owned by #193. */
int crux_ssh_cli_shell(void *context, crux_ssh_attachment_t *attachment,
                       crux_ssh_channel_t *channel, bool terminal);
int crux_ssh_cli_exec(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                      const char *command, size_t command_length);

/* In components/ssh/sftp/, owned by #194. */
crux_status_t crux_ssh_sftp_open(void *context, crux_ssh_attachment_t *attachment,
                                 crux_ssh_file_handle_t **handle);
void crux_ssh_sftp_close(void *context, crux_ssh_attachment_t *attachment,
                         crux_ssh_file_handle_t *handle);

/** The table of the entry points above, for the provider's start(). It lives for the program's
 * life. */
const crux_ssh_callbacks_t *crux_ssh_callback_table(void);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_CALLBACKS_H */
