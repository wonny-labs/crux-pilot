/*
 * What the conformance cases need from the far end of a provider: a client they can script, and key
 * and address material fit for that provider. The fake provider supplies one. A real provider
 * supplies its own: tests/zephyr/ssh_provider has the one for wolfSSH on native_sim.
 */
#ifndef CRUX_SSH_TEST_CLIENT_H
#define CRUX_SSH_TEST_CLIENT_H

#include "crux_ssh_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const crux_ssh_provider_t *provider;
    /** The largest max_connections this provider accepts. */
    uint8_t connection_capacity;

    /** A host key this provider can load. The material a real library will parse. */
    crux_ssh_key_type_t host_key_type;
    const uint8_t *host_key;
    size_t host_key_length;
    /** SSH public key blobs that this client holds the private keys of. */
    const uint8_t *ed25519_key;
    size_t ed25519_key_length;
    const uint8_t *ecdsa_key;
    size_t ecdsa_key_length;
    /** Two addresses that connect() can come from, and that the provider will report as given. */
    crux_ssh_peer_t peer_a;
    crux_ssh_peer_t peer_b;

    /** Opens a connection. False means the server refused it, and *connection is not set. */
    bool (*connect)(const crux_ssh_peer_t *peer, int *connection);
    /** Each returns true when the server accepted the attempt. A user name is length-delimited. */
    bool (*password)(int connection, const char *user, size_t user_length, const char *password);
    /** Sends the `none` request that clients open with. No provider ever accepts it. */
    bool (*none_request)(int connection, const char *user);
    /** Asks whether a key would do, with no signature. The algorithm is the name on the wire. */
    bool (*key_probe)(int connection, const char *user, const char *algorithm, const uint8_t *key,
                      size_t key_length);
    /** Sends a signed request, with a signature that verifies or one that does not. */
    bool (*key_login)(int connection, const char *user, const char *algorithm, const uint8_t *key,
                      size_t key_length, bool good_signature);
    bool (*is_connected)(int connection);
    /** Lets the unauthenticated time limit pass. */
    void (*pass_auth_time)(int connection);
    /** Lets SFTP wait out one io_timeout_ms with no request. */
    void (*pass_read_timeout)(int connection);

    /** Each returns true when the server accepted the channel or request. */
    bool (*open_channel)(int connection, const char *type);
    /**
     * A channel request of any type. For a session request the session runs to its end before this
     * returns, or the client waits for it to end. Data queued with send() is there for it to read.
     * Its answer means nothing once the session has started, so a case checks is_connected instead.
     */
    bool (*channel_request)(int connection, const char *type, const char *data, size_t data_length);
    bool (*global_request)(int connection, const char *type);

    /** False means the client could not queue it, and nothing was queued. */
    bool (*send)(int connection, const void *data, size_t length);
    void (*send_eof)(int connection);
    /** Limits what the server can write from now on, in bytes in total. */
    void (*set_window)(int connection, size_t bytes);
    /** The next write fails as if the connection had just dropped. */
    void (*drop_on_write)(int connection);
    /** Copies what the server has written so far and returns its length. */
    size_t (*received)(int connection, void *buffer, size_t size);
    bool (*received_eof)(int connection);
    /** False if no exit status has arrived. */
    bool (*exit_status)(int connection, int *status);

    /** The handle the SSH library's file operations would receive, or NULL. */
    crux_ssh_file_handle_t *(*file_handle)(int connection);
    /** Ends the SFTP session from the client side. */
    void (*end_sftp)(int connection);
    void (*disconnect)(int connection);
} crux_ssh_test_client_t;

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_TEST_CLIENT_H */
