/*
 * How the CLI (#193) and SFTP (#194) adapters reach the access session of a connection. Crux code
 * only: a project never includes this header. The access adapter of #192 defines the attachment and
 * this function, in components/ssh/access/. Host tests use the double in
 * tests/ssh/fake_attachment.h.
 */
#ifndef CRUX_SSH_ATTACHMENT_H
#define CRUX_SSH_ATTACHMENT_H

#include "crux_ssh_provider.h"

#include <crux/access.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The access session that the attachment holds. It does not check the session, and its result stays
 * correct until connection_closed for this attachment. Any thread context.
 *
 * @param attachment As a session callback or sftp_open received it. NULL gives a zero value, which
 * is never a valid session, so every access check on it fails.
 */
crux_access_session_t crux_ssh_attachment_session(const crux_ssh_attachment_t *attachment);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_ATTACHMENT_H */
