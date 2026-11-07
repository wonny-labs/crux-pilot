/*
 * The SSH services on the native_sim services composition (Issue #197): the wolfSSH provider with
 * the command line over SSH and SFTP, beside the HTTPS listener, on the same Access core,
 * credential store and file service. Test only, as crux_native_management.h states. The host key is
 * held in test form: see ssh_test_host_key_guard.h and docs/ssh-integrated-image.md.
 */
#ifndef CRUX_NATIVE_MANAGEMENT_SSH_H
#define CRUX_NATIVE_MANAGEMENT_SSH_H

#include <crux/access.h>

#include <stdbool.h>
#include <stddef.h>

/* Where the start stopped, or 0 once the provider serves. */
typedef enum {
    CRUX_NATIVE_MANAGEMENT_SSH_STARTED = 0,
    CRUX_NATIVE_MANAGEMENT_SSH_FAILED_HOST_KEY,
    CRUX_NATIVE_MANAGEMENT_SSH_FAILED_CLI,
    CRUX_NATIVE_MANAGEMENT_SSH_FAILED_PROVIDER
} crux_native_management_ssh_start_t;

/*
 * Gives each named account the SSH public key that the image was started with, as
 * -testargs key=<hex of an SSH public key blob>. A key already on the account is not an error. With
 * no key argument it does nothing and succeeds, and only password logins work. The session needs
 * access.manage. Call it after the services composition has started, from one thread.
 */
bool crux_native_management_ssh_add_keys(crux_access_session_t administrator,
                                         const char *const *users, size_t user_count);

/*
 * Starts the provider after crux_native_management_services_start(), which has initialised the CLI
 * core and started the file service. The host key comes from the credential store, which generates
 * it at the first boot and keeps it on the disk. Prints "SSH-HOST-KEY SHA256:..." and then
 * "SSH_READY <port>" once the listener runs.
 */
crux_native_management_ssh_start_t crux_native_management_ssh_start(void);

/*
 * Prints "SSH-MEASURE", when it changed, with the heap's peak and current bytes and the deepest
 * host stack seen on a worker and on the listener. Simulation results only: a 64-bit image, whose
 * threads run on host stacks. See docs/ssh-wolf-provider.md#resource-use.
 */
void crux_native_management_ssh_report(void);

#endif
