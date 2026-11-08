/*
 * A Host double for the attachment that the access adapter of #192 defines, so that CLI and SFTP
 * tests can hand a session through crux_ssh_attachment_session() with no access adapter.
 *
 * This double defines crux_ssh_attachment_session(), as components/ssh/access/ does, so a test
 * links one or the other. Linking it with crux_ssh_callbacks, which brings the access adapter in,
 * fails with "multiple definition of crux_ssh_attachment_session".
 */
#ifndef CRUX_SSH_FAKE_ATTACHMENT_H
#define CRUX_SSH_FAKE_ATTACHMENT_H

#include "crux_ssh_attachment.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An attachment that holds this session, from a small pool. NULL when the pool is used up. */
crux_ssh_attachment_t *crux_ssh_fake_attachment(crux_access_session_t session);
/** Frees the whole pool. Earlier attachments must not be used afterwards. */
void crux_ssh_fake_attachment_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_FAKE_ATTACHMENT_H */
