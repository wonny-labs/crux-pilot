/*
 * A fake SSH provider for Host tests of the CLI, SFTP and access adapters, without any SSH library.
 * It runs every callback on the calling thread, so it cannot show a concurrency or timing property,
 * and a session callback runs inside channel_request. read() on empty input returns
 * CRUX_ERR_TIMEOUT at once, or CRUX_ERR_NO_DATA after send_eof. It is not thread safe, and only
 * tests can link it.
 *
 * Signature checks are modelled: key_login says whether the signature verifies.
 */
#ifndef CRUX_SSH_FAKE_PROVIDER_H
#define CRUX_SSH_FAKE_PROVIDER_H

#include "ssh_test_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Deliberate defects. The fault tests require the conformance cases to catch each one. */
typedef enum {
    CRUX_SSH_FAKE_FAULT_NONE = 0,
    CRUX_SSH_FAKE_FAULT_START_ACCEPTS_NO_HOST_KEY,
    CRUX_SSH_FAKE_FAULT_START_BORROWS_CALLBACKS,
    CRUX_SSH_FAKE_FAULT_RESTART_REFUSED,
    CRUX_SSH_FAKE_FAULT_UNKNOWN_RESULT_ACCEPTS,
    CRUX_SSH_FAKE_FAULT_NO_USER_CHECK,
    CRUX_SSH_FAKE_FAULT_NO_ATTEMPT_LIMIT,
    CRUX_SSH_FAKE_FAULT_NO_AUTH_TIME_LIMIT,
    CRUX_SSH_FAKE_FAULT_NO_CONNECTION_LIMIT,
    CRUX_SSH_FAKE_FAULT_ACCEPTS_RSA,
    CRUX_SSH_FAKE_FAULT_SESSION_FROM_PROBE,
    CRUX_SSH_FAKE_FAULT_SIGNATURE_NOT_CHECKED,
    CRUX_SSH_FAKE_FAULT_KEY_FAILURE_SILENT,
    CRUX_SSH_FAKE_FAULT_VERIFIED_RESULT_IGNORED,
    CRUX_SSH_FAKE_FAULT_REQUEST_BEFORE_AUTH,
    CRUX_SSH_FAKE_FAULT_ACCEPTS_OTHER_SUBSYSTEM,
    CRUX_SSH_FAKE_FAULT_ACCEPTS_FORWARDING,
    CRUX_SSH_FAKE_FAULT_ACCEPTS_SECOND_CHANNEL,
    CRUX_SSH_FAKE_FAULT_WRONG_ATTACHMENT,
    CRUX_SSH_FAKE_FAULT_NO_EXIT_STATUS,
    CRUX_SSH_FAKE_FAULT_EXIT_STATUS_NOT_CLAMPED,
    CRUX_SSH_FAKE_FAULT_COMMAND_NUL_PASSED,
    CRUX_SSH_FAKE_FAULT_READ_EOF_AS_TIMEOUT,
    CRUX_SSH_FAKE_FAULT_READ_AFTER_CLOSE,
    CRUX_SSH_FAKE_FAULT_WRITE_IGNORES_WINDOW,
    CRUX_SSH_FAKE_FAULT_WRITE_DROP_HIDDEN,
    CRUX_SSH_FAKE_FAULT_NO_EOF_ON_CLOSE,
    CRUX_SSH_FAKE_FAULT_FILE_HANDLE_NOT_SET,
    CRUX_SSH_FAKE_FAULT_NO_SFTP_CLOSE,
    CRUX_SSH_FAKE_FAULT_CLOSED_BEFORE_SFTP_CLOSE,
    CRUX_SSH_FAKE_FAULT_NO_CONNECTION_CLOSED,
    CRUX_SSH_FAKE_FAULT_CONNECTION_CLOSED_TWICE,
    CRUX_SSH_FAKE_FAULT_STOP_KEEPS_CONNECTIONS,
    CRUX_SSH_FAKE_FAULT_STOP_FROM_CALLBACK,
    CRUX_SSH_FAKE_FAULT_COUNT
} crux_ssh_fake_fault_t;

/** Forgets all state, stops without callbacks, and clears the fault. */
void crux_ssh_fake_reset(void);
void crux_ssh_fake_set_fault(crux_ssh_fake_fault_t fault);
const crux_ssh_provider_t *crux_ssh_fake_provider(void);
const crux_ssh_test_client_t *crux_ssh_fake_client(void);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_FAKE_PROVIDER_H */
