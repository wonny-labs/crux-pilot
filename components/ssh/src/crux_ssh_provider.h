/*
 * Private SSH provider interface. Crux code only: a project never includes this header, and it is
 * not a stable interface. See docs/ssh-provider-contract.md for the ownership rules behind it.
 *
 * This header names no SSH library type, so the CLI, SFTP and access adapters do not depend on one.
 */
#ifndef CRUX_SSH_PROVIDER_H
#define CRUX_SSH_PROVIDER_H

#include <crux/status.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest user name a provider passes on, in bytes, without its terminator. */
#define CRUX_SSH_USER_NAME_MAX 31u
/** Largest peer address, in bytes. */
#define CRUX_SSH_PEER_ADDRESS_MAX 16u

/** One session channel. Defined by each provider, and valid only during its session callback. */
typedef struct crux_ssh_channel crux_ssh_channel_t;

/**
 * What ties a connection to the rest of Crux. Defined by the access adapter of Issue #192. The
 * provider only stores the pointer and hands it back, so this header needs no access control type.
 */
typedef struct crux_ssh_attachment crux_ssh_attachment_t;

/**
 * The per-session file handle. Defined by the SFTP port of Issue #194. The provider gives it to the
 * SSH library, which passes it unchanged to every file operation of the session.
 */
typedef struct crux_ssh_file_handle crux_ssh_file_handle_t;

/** The key types Crux accepts. There is deliberately no RSA value, by owner decision. */
typedef enum {
    CRUX_SSH_KEY_ED25519 = 1,   /**< ssh-ed25519 */
    CRUX_SSH_KEY_ECDSA_P256 = 2 /**< ecdsa-sha2-nistp256 */
} crux_ssh_key_type_t;

/**
 * The outcome of a credential callback. Zero denies, so a callback that returns zero by accident
 * fails closed. A provider treats every value other than CRUX_SSH_AUTH_ACCEPTED as denied.
 */
typedef enum { CRUX_SSH_AUTH_DENIED = 0, CRUX_SSH_AUTH_ACCEPTED = 1 } crux_ssh_auth_result_t;

/** The address a connection came from, as received. The access adapter derives its delay key from
 * it. */
typedef struct {
    uint8_t address[CRUX_SSH_PEER_ADDRESS_MAX];
    uint8_t address_length;
} crux_ssh_peer_t;

/**
 * Server configuration. start() copies this structure. The host key bytes stay borrowed until
 * stop() returns. All limits must be non-zero.
 */
typedef struct {
    uint16_t port;
    uint8_t max_connections;   /**< Connections at once, authenticated or not. */
    uint8_t max_auth_attempts; /**< Failed attempts on one connection before it is closed. */
    uint32_t auth_timeout_ms;  /**< A connection not authenticated by then is closed. */
    uint32_t io_timeout_ms;    /**< The longest that read() or write() waits. */
    crux_ssh_key_type_t host_key_type;
    const uint8_t *host_key; /**< Private key in the form Issue #192 documents. Required. */
    size_t host_key_length;
    /** Dotted IPv4 text of the one local address to listen on, or NULL for every IPv4 address.
     * The address must already be on an interface, and a text that is not an IPv4 address makes
     * start() fail, so that a mistyped address never widens the listener. */
    const char *bind_address;
} crux_ssh_config_t;

/**
 * Callbacks into Crux. start() copies this structure, and every member is required.
 *
 * Every callback runs on the thread of its connection, never with a provider lock held, and may
 * block that thread only. Pointers it receives are borrowed for the call, unless stated otherwise.
 *
 * User names. The first authentication request binds the name to the connection. An empty name, or
 * a later request with another name, ends the connection without a callback, so a client that wants
 * another user reconnects. A name longer than CRUX_SSH_USER_NAME_MAX or with an embedded NUL is
 * refused without a callback. An authentication request after a successful login ends the
 * connection without a callback.
 *
 * Keys. A key reaches key_check only if its algorithm name is one of the types above and its blob
 * is exactly that type's form, with nothing after it: crux_ssh_wire_user_key() is the check.
 *
 * Attempts. Every refused attempt counts towards max_auth_attempts: a bad name, a key of any type
 * other than those above or with a malformed blob, a refused key with or without a signature, a
 * failed signature, a refusal from key_verified and a wrong password. An accepted attempt and the
 * opening `none` request, which every client sends to learn the methods, do not count.
 *
 * Requests. The provider accepts `pty-req` and `window-change` with no callback, and only those two
 * besides shell, exec and the `sftp` subsystem below. It refuses every other request and channel
 * type with no callback. A connection carries one session: the first shell, exec or SFTP request
 * starts it, and the provider ends the connection when it ends. Once a provider has granted a
 * request, failure of its callback appears to the client as the connection ending.
 */
typedef struct {
    void *context; /**< Passed unchanged to every callback. */

    /**
     * Checks a password. On CRUX_SSH_AUTH_ACCEPTED it must set *attachment to a non-NULL value, and
     * the connection is then authenticated. The password is not terminated and must not be kept.
     */
    crux_ssh_auth_result_t (*password)(void *context, const crux_ssh_peer_t *peer, const char *user,
                                       const uint8_t *password, size_t password_length,
                                       crux_ssh_attachment_t **attachment);

    /**
     * Says whether this key belongs to this user. It runs before any signature is checked, also
     * when the client only asks whether a key would do (signed_request false), so it must not
     * create a session. The key is the SSH public key blob, as on the wire.
     */
    crux_ssh_auth_result_t (*key_check)(void *context, const crux_ssh_peer_t *peer,
                                        const char *user, crux_ssh_key_type_t key_type,
                                        const uint8_t *key, size_t key_length, bool signed_request);

    /**
     * The signature on a key that key_check accepted has been verified. This is the only place
     * where public-key login may create a session. The accepted and attachment rules are those of
     * password.
     */
    crux_ssh_auth_result_t (*key_verified)(void *context, const crux_ssh_peer_t *peer,
                                           const char *user, crux_ssh_key_type_t key_type,
                                           const uint8_t *key, size_t key_length,
                                           crux_ssh_attachment_t **attachment);

    /** The signature on a key that key_check accepted did not verify. */
    void (*key_failed)(void *context, const crux_ssh_peer_t *peer, const char *user);

    /**
     * Runs an interactive shell until it returns. It returns the exit status, from 0 to 255, and
     * the provider sends 255 for anything else. The provider then ends the connection. terminal is
     * true when the client sent `pty-req` on this channel before the shell request, so that the CLI
     * knows whether to echo.
     */
    int (*shell)(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                 bool terminal);

    /**
     * Runs one command until it returns, with the same rules as shell. The command is terminated,
     * holds no embedded NUL and has command_length bytes before its terminator.
     */
    int (*exec)(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                const char *command, size_t command_length);

    /**
     * The client asked for the SFTP subsystem. On CRUX_OK it must set *handle to a non-NULL value,
     * and the provider serves SFTP with it on this thread. On any other status, or with no handle,
     * the connection ends and sftp_close does not run.
     */
    crux_status_t (*sftp_open)(void *context, crux_ssh_attachment_t *attachment,
                               crux_ssh_file_handle_t **handle);

    /** SFTP has ended. Runs once for each successful sftp_open, and no file operation follows it.
     */
    void (*sftp_close)(void *context, crux_ssh_attachment_t *attachment,
                       crux_ssh_file_handle_t *handle);

    /**
     * Polled each time SFTP has waited io_timeout_ms with no request, because SFTP has no session
     * callback of its own to notice an expired session. False ends the connection, with sftp_close
     * and connection_closed as usual. It must not wait on storage or derivation.
     */
    bool (*session_alive)(void *context, crux_ssh_attachment_t *attachment);

    /**
     * The connection has ended, for any reason. Runs exactly once for each attachment, after every
     * other callback of the connection has returned, and the attachment is not used again. A
     * connection that never authenticated has no attachment and gets no call.
     */
    void (*connection_closed)(void *context, crux_ssh_attachment_t *attachment);
} crux_ssh_callbacks_t;

/**
 * A provider. Its functions run in thread context. A channel is used only on the thread of the
 * session callback that received it, and must not be used after that callback returns.
 */
typedef struct {
    /**
     * Validates the arguments and starts serving.
     *
     * @retval CRUX_OK Serving. Connections are now accepted.
     * @retval CRUX_ERR_INVALID_ARGUMENT A pointer or callback is NULL, a limit is zero, or the host
     * key is missing or of an unknown type.
     * @retval CRUX_ERR_OUT_OF_RANGE max_connections is more than this provider can serve.
     * @retval CRUX_ERR_INVALID_STATE Already started.
     * @retval CRUX_ERR_IO The provider could not start, for example because the host key is
     * unusable.
     */
    crux_status_t (*start)(const crux_ssh_config_t *config, const crux_ssh_callbacks_t *callbacks);

    /**
     * Ends every connection and stops serving. It returns after each connection_closed has
     * returned.
     *
     * @retval CRUX_OK Stopped. The provider can be started again.
     * @retval CRUX_ERR_INVALID_STATE Not started, or called from one of its callbacks.
     */
    crux_status_t (*stop)(void);

    /**
     * Reads what has arrived, waiting up to io_timeout_ms for at least one byte. A slow or idle
     * client holds only this thread. The wait is fixed for the server, and a caller cannot shorten
     * it. *transferred is 0 after any failure, if it is not NULL.
     *
     * @retval CRUX_OK *transferred is from 1 to length.
     * @retval CRUX_ERR_INVALID_ARGUMENT A pointer is NULL or length is zero.
     * @retval CRUX_ERR_NO_DATA The client sent EOF or closed the channel, and all data has been
     * read.
     * @retval CRUX_ERR_TIMEOUT Nothing arrived. The channel stays usable.
     * @retval CRUX_ERR_IO The connection failed.
     * @retval CRUX_ERR_INVALID_STATE close() was called, or the provider is stopping.
     */
    crux_status_t (*read)(crux_ssh_channel_t *channel, void *buffer, size_t length,
                          size_t *transferred);

    /**
     * Writes what the client's window allows, waiting up to io_timeout_ms for room. *transferred is
     * 0 after any failure, if it is not NULL.
     *
     * @retval CRUX_OK *transferred is from 1 to length, and can be less than length.
     * @retval CRUX_ERR_INVALID_ARGUMENT A pointer is NULL or length is zero.
     * @retval CRUX_ERR_TIMEOUT No room appeared, and nothing was written. The channel stays usable.
     * @retval CRUX_ERR_IO The connection failed, or the client closed the channel.
     * @retval CRUX_ERR_INVALID_STATE close() was called, or the provider is stopping.
     */
    crux_status_t (*write)(crux_ssh_channel_t *channel, const void *buffer, size_t length,
                           size_t *transferred);

    /**
     * Ends this side of the channel: EOF goes to the client, and read() and write() then return
     * CRUX_ERR_INVALID_STATE. The exit status still goes out when the session callback returns. A
     * second call does nothing.
     *
     * @retval CRUX_OK Closed.
     * @retval CRUX_ERR_INVALID_ARGUMENT The channel is NULL.
     */
    crux_status_t (*close)(crux_ssh_channel_t *channel);
} crux_ssh_provider_t;

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_PROVIDER_H */
