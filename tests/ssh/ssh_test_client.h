/*
 * What the conformance cases need from the far end of a provider: a client they can script. The
 * fake provider supplies one. A real provider can supply its own, for example on native_sim with a
 * client on a host-reachable link, and run the same cases.
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

    /** Opens a connection. False means the server refused it, and *connection is not set. */
    bool (*connect)(const crux_ssh_peer_t *peer, int *connection);
    /** Each returns true when the server accepted the attempt. A user name is length-delimited. */
    bool (*password)(int connection, const char *user, size_t user_length, const char *password);
    /** Asks whether a key would do, with no signature. The algorithm is the name on the wire. */
    bool (*key_probe)(int connection, const char *user, const char *algorithm, const uint8_t *key,
                      size_t key_length);
    /** Sends a signed request, with a signature that verifies or one that does not. */
    bool (*key_login)(int connection, const char *user, const char *algorithm, const uint8_t *key,
                      size_t key_length, bool good_signature);
    bool (*is_connected)(int connection);
    /** Lets the unauthenticated time limit pass. */
    void (*pass_auth_time)(int connection);

    /** Each returns true when the server accepted the channel or request. */
    bool (*open_channel)(int connection, const char *type);
    /**
     * A channel request of any type. For a session request the session runs to its end before this
     * returns, or the client waits for it to end. Data queued with send() is there for it to read.
     */
    bool (*channel_request)(int connection, const char *type, const char *data, size_t data_length);
    bool (*global_request)(int connection, const char *type);

    void (*send)(int connection, const void *data, size_t length);
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
