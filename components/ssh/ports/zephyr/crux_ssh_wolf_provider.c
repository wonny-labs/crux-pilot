/*
 * The private SSH interface on wolfSSH and Zephyr sockets. See docs/ssh-provider-contract.md for
 * the rules it keeps and docs/ssh-wolf-provider.md for what it chose.
 *
 * One listener thread, and one worker thread per connection. A WOLFSSH belongs to its worker. The
 * sockets are used with non-blocking calls and a bounded poll, so that a worker sees its limits and
 * a stop however quiet the client is. No lock is held across a callback.
 */
#include "crux_ssh_wolf_provider.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>
#ifdef CRUX_SSH_WOLF_TESTING
#include <zephyr/sys/printk.h>
#endif

#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/sha256.h>

#include <errno.h>
#include <string.h>

/* The design leaves these out, so that their code is not exposed to the network. */
#if defined(WOLFSSH_SCP) || defined(WOLFSSH_FWD) || defined(WOLFSSH_AGENT) || defined(WOLFSSH_CERTS)
#error "SCP, forwarding, agent forwarding and certificates stay disabled"
#endif
#ifndef WOLFSSH_NO_RSA
#error "Owner decision: no RSA in SSH"
#endif

/* Workers share a priority, so without a time slice one long verification holds the others. */
#if !defined(CONFIG_TIMESLICING) || !defined(CONFIG_TIMESLICE_SIZE) || CONFIG_TIMESLICE_SIZE <= 0
#error "The SSH workers need CONFIG_TIMESLICING and a CONFIG_TIMESLICE_SIZE above zero"
#endif

/* The longest a worker or the listener goes without looking at its limits and for a stop. */
#define WOLF_TICK_MS 50

/*
 * What one channel can hold, and so most of a connection's heap: see DEFAULT_WINDOW_SZ in
 * wolfssh/internal.h, which is 128 KiB per channel. The measured cost is in the design document.
 */
#ifndef CRUX_SSH_WOLF_WINDOW_BYTES
#define CRUX_SSH_WOLF_WINDOW_BYTES 16384u
#endif
#ifndef CRUX_SSH_WOLF_PACKET_BYTES
#define CRUX_SSH_WOLF_PACKET_BYTES 16384u
#endif
#ifndef CRUX_SSH_WOLF_WORKER_STACK_BYTES
#define CRUX_SSH_WOLF_WORKER_STACK_BYTES 16384
#endif
#ifndef CRUX_SSH_WOLF_LISTENER_STACK_BYTES
#define CRUX_SSH_WOLF_LISTENER_STACK_BYTES 4096
#endif
#ifndef CRUX_SSH_WOLF_THREAD_PRIORITY
#define CRUX_SSH_WOLF_THREAD_PRIORITY K_LOWEST_APPLICATION_THREAD_PRIO
#endif
/* The longest a command can be, which is more than the CLI's line. */
#ifndef CRUX_SSH_WOLF_COMMAND_MAX
#define CRUX_SSH_WOLF_COMMAND_MAX 1023u
#endif
/* How long the client gets to answer the end of its session. */
#define WOLF_DRAIN_MS 2000
/* How often a login with no session yet asks whether its access session still stands. */
#define WOLF_PRESESSION_POLL_MS 30000

/* How much a worker receives before it looks at its limits again. */
#define WOLF_RECEIVE_BUDGET_BYTES 16384u

/* How many refused sockets wait to close, and for how long. */
#define WOLF_REFUSALS_HELD 2
#define WOLF_REFUSAL_HOLD_MS 500

#define HOST_KEY_ED25519_BYTES 64u
#define HOST_KEY_P256_BYTES 97u
#define HOST_KEY_PRIVATE_BYTES 32u

static const char algorithm_ed25519[] = "ssh-ed25519";
static const char algorithm_p256[] = "ecdsa-sha2-nistp256";
static const char curve_p256[] = "nistp256";

#ifdef CRUX_SSH_WOLF_OFFER_MLKEM
#define WOLF_KEX_LIST "mlkem768x25519-sha256,curve25519-sha256,ecdh-sha2-nistp256"
#else
#define WOLF_KEX_LIST "curve25519-sha256,ecdh-sha2-nistp256"
#endif
#define WOLF_CIPHER_LIST "aes128-gcm@openssh.com,aes256-gcm@openssh.com,aes128-ctr"
#define WOLF_MAC_LIST "hmac-sha2-256"
#define WOLF_USER_KEY_LIST "ssh-ed25519,ecdsa-sha2-nistp256"
#define WOLF_PROTO_ID "SSH-2.0-Crux\r\n"

typedef enum { SLOT_FREE = 0, SLOT_RUNNING, SLOT_FINISHED } slot_state_t;
typedef enum { REQUEST_NONE = 0, REQUEST_SHELL, REQUEST_EXEC, REQUEST_SFTP } request_t;

struct crux_ssh_channel {
    bool closed;
};

typedef struct {
    struct k_thread thread;
    atomic_t state;
    int socket;
    WOLFSSH *ssh;
    crux_ssh_peer_t peer;
    uint16_t peer_port;
    crux_ssh_attachment_t *attachment;
    crux_ssh_channel_t channel;
    request_t request;
    bool terminal;
    bool sftp_opened;
    crux_ssh_file_handle_t *file_handle;
    bool session_running;
    bool transport_failed;
    size_t received_since_wait;
    int64_t flush_until;
    int64_t auth_deadline;
    int64_t last_poll;
#ifdef CRUX_SSH_WOLF_TESTING
    bool drop_on_write;
#endif
} connection_t;

static K_THREAD_STACK_ARRAY_DEFINE(worker_stacks, CRUX_SSH_WOLF_CONNECTIONS_MAX,
                                   CRUX_SSH_WOLF_WORKER_STACK_BYTES);
static K_THREAD_STACK_DEFINE(listener_stack, CRUX_SSH_WOLF_LISTENER_STACK_BYTES);
static K_MUTEX_DEFINE(server_lock);

typedef struct {
    int socket;
    int64_t close_at;
} refused_socket_t;

static struct {
    bool started;
    atomic_t stopping;
    crux_ssh_config_t config;
    crux_ssh_callbacks_t callbacks;
    WOLFSSH_CTX *ctx;
    int listener_socket;
    struct k_thread listener;
    connection_t connections[CRUX_SSH_WOLF_CONNECTIONS_MAX];
    refused_socket_t refused[WOLF_REFUSALS_HELD];
#ifdef CRUX_SSH_WOLF_TESTING
    size_t worker_stack_used;
    size_t listener_stack_used;
#endif
} server;

static void wipe(void *buffer, size_t length)
{
    volatile uint8_t *bytes = buffer;

    for (size_t i = 0; i < length; ++i) {
        bytes[i] = 0u;
    }
}

static bool is_stopping(void)
{
    return atomic_get(&server.stopping) != 0;
}

static connection_t *connection_of(crux_ssh_channel_t *channel)
{
    return CONTAINER_OF(channel, connection_t, channel);
}

static bool on_worker_thread(void)
{
    k_tid_t current = k_current_get();

    for (size_t i = 0; i < ARRAY_SIZE(server.connections); ++i) {
        if (current == &server.connections[i].thread) {
            return true;
        }
    }
    return false;
}

#if defined(CRUX_SSH_WOLF_TESTING) && defined(CONFIG_ARCH_POSIX)
/*
 * On native_sim each thread runs on a host stack, and its Zephyr stack sees almost nothing, so the
 * kernel's stack report says nothing about use. This paints the host stack below the caller and
 * finds the deepest change. It measures a 64-bit build, so it is a simulation result only.
 */
#define STACK_PROBE_BYTES (512u * 1024u)
#define STACK_PROBE_FILL 0xa5u
#define STACK_PROBE_GAP 512u

__attribute__((noinline)) static void stack_probe_paint(const uint8_t *base)
{
    volatile uint8_t *at = (volatile uint8_t *)(uintptr_t)base - STACK_PROBE_BYTES;

    for (size_t i = 0; i < STACK_PROBE_BYTES - STACK_PROBE_GAP; ++i) {
        at[i] = STACK_PROBE_FILL;
    }
}

__attribute__((noinline)) static size_t stack_probe_used(const uint8_t *base)
{
    const volatile uint8_t *at = (const volatile uint8_t *)(uintptr_t)base - STACK_PROBE_BYTES;
    size_t untouched = 0;

    while (untouched < STACK_PROBE_BYTES - STACK_PROBE_GAP && at[untouched] == STACK_PROBE_FILL) {
        ++untouched;
    }
    return STACK_PROBE_BYTES - untouched;
}
#define STACK_PROBE_START()                                                                        \
    const uint8_t *stack_base = __builtin_frame_address(0);                                        \
    stack_probe_paint(stack_base)
#define STACK_PROBE_RECORD(peak) (peak) = MAX((peak), stack_probe_used(stack_base))
#else
#define STACK_PROBE_START()
#define STACK_PROBE_RECORD(peak)
#endif

/* ---- Socket callbacks. Non-blocking, so that wolfSSH hands back to the worker. ---- */

static int io_result(void)
{
    switch (errno) {
    case EAGAIN:
#if EAGAIN != EWOULDBLOCK
    case EWOULDBLOCK:
#endif
        return WS_CBIO_ERR_WANT_READ;
    case EINTR:
        return WS_CBIO_ERR_ISR;
    case ECONNRESET:
        return WS_CBIO_ERR_CONN_RST;
    case EPIPE:
    case ECONNABORTED:
    case ENOTCONN:
        return WS_CBIO_ERR_CONN_CLOSE;
    default:
        return WS_CBIO_ERR_GENERAL;
    }
}

/* Waits until the socket may have changed, for no longer than a tick or wait_ms. */
static void wait_socket(const connection_t *connection, bool for_write, int32_t wait_ms)
{
    struct zsock_pollfd poll_entry = {
        .fd = connection->socket,
        .events = for_write ? ZSOCK_POLLOUT : ZSOCK_POLLIN,
    };

    (void)zsock_poll(&poll_entry, 1, MIN(MAX(wait_ms, 0), WOLF_TICK_MS));
}

/*
 * wolfSSH goes on receiving for as long as the socket has data, and a client may send
 * SSH_MSG_IGNORE from before the keys. So this hands back after a budget of data, as if the socket
 * were empty. Each loop of the worker then sees its limits and a stop, and the sleep lets other
 * connections run.
 */
static int io_receive(WOLFSSH *ssh, void *buffer, word32 size, void *context)
{
    connection_t *connection = context;
    ssize_t received;

    ARG_UNUSED(ssh);
    if (connection->received_since_wait >= WOLF_RECEIVE_BUDGET_BYTES) {
        connection->received_since_wait = 0u;
        k_msleep(1);
        return WS_CBIO_ERR_WANT_READ;
    }
    received = zsock_recv(connection->socket, buffer, size, ZSOCK_MSG_DONTWAIT);
    if (received > 0) {
        connection->received_since_wait += (size_t)received;
        return (int)received;
    }
    if (received == 0) {
        return WS_CBIO_ERR_CONN_CLOSE;
    }
    connection->received_since_wait = 0u;
    return io_result();
}

#ifdef CRUX_SSH_WOLF_TESTING
static atomic_t send_block_period;
static atomic_t send_count;

void crux_ssh_wolf_test_block_sends(unsigned period)
{
    atomic_set(&send_block_period, (atomic_val_t)period);
}
#endif

static int send_once(const connection_t *connection, void *buffer, word32 size)
{
    ssize_t sent;

#ifdef CRUX_SSH_WOLF_TESTING
    if (connection->drop_on_write && connection->session_running) {
        (void)zsock_shutdown(connection->socket, ZSOCK_SHUT_RDWR);
        return WS_CBIO_ERR_CONN_RST;
    }
    if (atomic_get(&send_block_period) != 0 &&
        (atomic_inc(&send_count) + 1) % atomic_get(&send_block_period) == 0) {
        return WS_CBIO_ERR_WANT_WRITE;
    }
#endif
    sent = zsock_send(connection->socket, buffer, size, ZSOCK_MSG_DONTWAIT);
    return sent >= 0 ? (int)sent : io_result();
}

/*
 * The end of a session is several messages in one wolfSSH call, which stops at the first that would
 * block and cannot be repeated. So during that call a send waits for room, for a bounded time.
 */
static int io_send(WOLFSSH *ssh, void *buffer, word32 size, void *context)
{
    connection_t *connection = context;
    int result = send_once(connection, buffer, size);

    ARG_UNUSED(ssh);
    while (result == WS_CBIO_ERR_WANT_WRITE && connection->flush_until != 0 && !is_stopping() &&
           k_uptime_get() < connection->flush_until) {
        wait_socket(connection, true, WOLF_TICK_MS);
        result = send_once(connection, buffer, size);
    }
    return result;
}

/* ---- Authentication. These run while a packet is parsed, on the connection's worker. ---- */

static bool copy_user_name(const WS_UserAuthData *data, char user[CRUX_SSH_USER_NAME_MAX + 1u])
{
    /* wolfSSH has already refused an empty name, and any name but the first on a connection. */
    if (data->usernameSz == 0u || data->usernameSz > CRUX_SSH_USER_NAME_MAX ||
        memchr(data->username, '\0', data->usernameSz) != NULL) {
        return false;
    }
    memcpy(user, data->username, data->usernameSz);
    user[data->usernameSz] = '\0';
    return true;
}

static bool key_type_of(const WS_UserAuthData_PublicKey *key, crux_ssh_key_type_t *type)
{
    const char *name;
    size_t name_length;
    uint32_t blob_name_length;

    if (key->publicKeyTypeSz == sizeof(algorithm_ed25519) - 1u &&
        memcmp(key->publicKeyType, algorithm_ed25519, key->publicKeyTypeSz) == 0) {
        *type = CRUX_SSH_KEY_ED25519;
        name = algorithm_ed25519;
        name_length = sizeof(algorithm_ed25519) - 1u;
    } else if (key->publicKeyTypeSz == sizeof(algorithm_p256) - 1u &&
               memcmp(key->publicKeyType, algorithm_p256, key->publicKeyTypeSz) == 0) {
        *type = CRUX_SSH_KEY_ECDSA_P256;
        name = algorithm_p256;
        name_length = sizeof(algorithm_p256) - 1u;
    } else {
        return false;
    }
    /* The blob must say the same. A mismatch would be refused later, with a less exact result. */
    if (key->publicKeySz < sizeof(uint32_t) + name_length) {
        return false;
    }
    blob_name_length = sys_get_be32(key->publicKey);
    return blob_name_length == name_length &&
           memcmp(key->publicKey + sizeof(uint32_t), name, name_length) == 0;
}

static int check_password(connection_t *connection, const char *user,
                          const WS_UserAuthData_Password *password)
{
    crux_ssh_attachment_t *attachment = NULL;
    crux_ssh_auth_result_t result =
        server.callbacks.password(server.callbacks.context, &connection->peer, user,
                                  password->password, password->passwordSz, &attachment);

    /* It is plain text in wolfSSH's input buffer, which nothing reads again and which keeps it. */
    wipe((void *)(uintptr_t)password->password, password->passwordSz);
    if (result != CRUX_SSH_AUTH_ACCEPTED || attachment == NULL) {
        return WOLFSSH_USERAUTH_FAILURE;
    }
    connection->attachment = attachment;
    return WOLFSSH_USERAUTH_SUCCESS;
}

static int user_auth(byte type, WS_UserAuthData *data, void *context)
{
    connection_t *connection = context;
    char user[CRUX_SSH_USER_NAME_MAX + 1u];
    crux_ssh_key_type_t key_type;

    if (connection->attachment != NULL || !copy_user_name(data, user)) {
        return WOLFSSH_USERAUTH_FAILURE;
    }
    if (type == WOLFSSH_USERAUTH_PASSWORD) {
        return check_password(connection, user, &data->sf.password);
    }
    if (type != WOLFSSH_USERAUTH_PUBLICKEY || !key_type_of(&data->sf.publicKey, &key_type)) {
        return WOLFSSH_USERAUTH_FAILURE;
    }
    /* Only the signature decides, and it has not been checked yet. So nothing is created here. */
    return server.callbacks.key_check(server.callbacks.context, &connection->peer, user, key_type,
                                      data->sf.publicKey.publicKey, data->sf.publicKey.publicKeySz,
                                      data->sf.publicKey.hasSignature != 0u) ==
                   CRUX_SSH_AUTH_ACCEPTED
               ? WOLFSSH_USERAUTH_SUCCESS
               : WOLFSSH_USERAUTH_FAILURE;
}

/* wolfSSH reads any value but WS_SUCCESS as an override of a good signature. */
static int user_auth_result(byte result, WS_UserAuthData *data, void *context)
{
    connection_t *connection = context;
    char user[CRUX_SSH_USER_NAME_MAX + 1u];
    crux_ssh_attachment_t *attachment = NULL;
    crux_ssh_key_type_t key_type;

    if (data->type != WOLFSSH_USERAUTH_PUBLICKEY || !copy_user_name(data, user) ||
        !key_type_of(&data->sf.publicKey, &key_type)) {
        return WS_FATAL_ERROR;
    }
    if (result != WOLFSSH_USERAUTH_SUCCESS) {
        server.callbacks.key_failed(server.callbacks.context, &connection->peer, user);
        return WS_SUCCESS;
    }
    if (server.callbacks.key_verified(server.callbacks.context, &connection->peer, user, key_type,
                                      data->sf.publicKey.publicKey, data->sf.publicKey.publicKeySz,
                                      &attachment) != CRUX_SSH_AUTH_ACCEPTED ||
        attachment == NULL) {
        return WS_FATAL_ERROR;
    }
    connection->attachment = attachment;
    return WS_SUCCESS;
}

/* ---- Requests. These only record: wolfSSH warns against re-entering it from here. ---- */

static bool request_is(const byte *type, word32 type_length, const char *name)
{
    return type_length == strlen(name) && memcmp(type, name, type_length) == 0;
}

/* The string at the start of a request's data, which must fill it. */
static bool parse_string(const byte *data, word32 data_length, const byte **text, word32 *length)
{
    if (data_length < sizeof(uint32_t)) {
        return false;
    }
    *length = sys_get_be32(data);
    *text = data + sizeof(uint32_t);
    return *length == data_length - sizeof(uint32_t);
}

static int session_request(connection_t *connection, request_t request, const byte *data,
                           word32 data_length)
{
    const byte *text = NULL;
    word32 length = 0;

    if (request == REQUEST_EXEC &&
        (!parse_string(data, data_length, &text, &length) || length > CRUX_SSH_WOLF_COMMAND_MAX ||
         memchr(text, '\0', length) != NULL)) {
        return WOLFSSH_REQ_REJECT;
    }
    if (request == REQUEST_SFTP && (!parse_string(data, data_length, &text, &length) ||
                                    length != 4u || memcmp(text, "sftp", 4u) != 0)) {
        return WOLFSSH_REQ_REJECT;
    }
    connection->request = request;
    return WOLFSSH_REQ_ACCEPT;
}

static int channel_request(WOLFSSH_CHANNEL *channel, const byte *type, word32 type_length,
                           const byte *data, word32 data_length, int want_reply, void *context)
{
    connection_t *connection = context;

    ARG_UNUSED(channel);
    ARG_UNUSED(want_reply);
    if (request_is(type, type_length, "window-change")) {
        return WOLFSSH_REQ_ACCEPT;
    }
    if (connection->request != REQUEST_NONE) {
        return WOLFSSH_REQ_REJECT; /* A connection carries one session. */
    }
    if (request_is(type, type_length, "pty-req")) {
        connection->terminal = true;
        return WOLFSSH_REQ_ACCEPT;
    }
    if (request_is(type, type_length, "shell")) {
        return session_request(connection, REQUEST_SHELL, data, data_length);
    }
    if (request_is(type, type_length, "exec")) {
        return session_request(connection, REQUEST_EXEC, data, data_length);
    }
    if (request_is(type, type_length, "subsystem")) {
        return session_request(connection, REQUEST_SFTP, data, data_length);
    }
    return WOLFSSH_REQ_REJECT;
}

/* ---- Channel functions, on the worker during a session callback. ---- */

static crux_status_t wait_for_room_or_data(const connection_t *connection, int64_t deadline,
                                           bool for_write)
{
    int64_t remaining = deadline - k_uptime_get();

    if (remaining <= 0) {
        return CRUX_ERR_TIMEOUT;
    }
    wait_socket(connection, for_write, (int32_t)MIN(remaining, WOLF_TICK_MS));
    return CRUX_OK;
}

static crux_status_t channel_state(const crux_ssh_channel_t *channel)
{
    if (channel->closed || is_stopping()) {
        return CRUX_ERR_INVALID_STATE;
    }
    return CRUX_OK;
}

/* The worker handled something, and the caller's own call decides what that means for it. */
static bool worker_made_progress(int result)
{
    return result >= 0 || result == WS_CHAN_RXD || result == WS_EOF || result == WS_REKEYING ||
           result == WS_CHANNEL_CLOSED;
}

static crux_status_t channel_read(crux_ssh_channel_t *channel, void *buffer, size_t length,
                                  size_t *transferred)
{
    connection_t *connection;
    int64_t deadline;

    if (transferred != NULL) {
        *transferred = 0;
    }
    if (channel == NULL || buffer == NULL || length == 0u || transferred == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    connection = connection_of(channel);
    deadline = k_uptime_get() + server.config.io_timeout_ms;
    while (true) {
        crux_status_t status = channel_state(channel);
        int result;
        int error;

        if (status != CRUX_OK) {
            return status;
        }
        if (connection->transport_failed) {
            return CRUX_ERR_IO;
        }
        result = wolfSSH_stream_read(connection->ssh, buffer, (word32)MIN(length, UINT32_MAX));
        if (result > 0) {
            *transferred = (size_t)result;
            return CRUX_OK;
        }
        error = wolfSSH_get_error(connection->ssh);
        if (error == WS_EOF || result == WS_EOF || error == WS_CHANNEL_CLOSED ||
            result == WS_CHANNEL_CLOSED || result == WS_BAD_ARGUMENT) {
            return CRUX_ERR_NO_DATA;
        }
        if (error == WS_REKEYING || result == WS_REKEYING) {
            /* Only the client's next message can finish it, and a finished worker call is no error.
             */
            result = wolfSSH_worker(connection->ssh, NULL);
            error = wolfSSH_get_error(connection->ssh);
            if (worker_made_progress(result)) {
                continue;
            }
        }
        if (error != WS_WANT_READ && error != WS_WANT_WRITE) {
            connection->transport_failed = true;
            return CRUX_ERR_IO;
        }
        status = wait_for_room_or_data(connection, deadline, error == WS_WANT_WRITE);
        if (status != CRUX_OK) {
            return status;
        }
    }
}

static crux_status_t channel_write(crux_ssh_channel_t *channel, const void *buffer, size_t length,
                                   size_t *transferred)
{
    connection_t *connection;
    int64_t deadline;

    if (transferred != NULL) {
        *transferred = 0;
    }
    if (channel == NULL || buffer == NULL || length == 0u || transferred == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    connection = connection_of(channel);
    deadline = k_uptime_get() + server.config.io_timeout_ms;
    while (true) {
        crux_status_t status = channel_state(channel);
        int result;
        int error;

        if (status != CRUX_OK) {
            return status;
        }
        if (connection->transport_failed) {
            return CRUX_ERR_IO;
        }
        /* It takes what the window allows, and a short count is success. */
        result =
            wolfSSH_stream_send(connection->ssh, (byte *)buffer, (word32)MIN(length, UINT32_MAX));
        if (result > 0) {
            *transferred = (size_t)result;
            return CRUX_OK;
        }
        error = wolfSSH_get_error(connection->ssh);
        if (result == WS_WINDOW_FULL || error == WS_WINDOW_FULL || result == WS_REKEYING ||
            error == WS_REKEYING) {
            /* Only the client's next message can open the window or finish a rekey. */
            result = wolfSSH_worker(connection->ssh, NULL);
            error = wolfSSH_get_error(connection->ssh);
            if (result == WS_CHANNEL_CLOSED) {
                connection->transport_failed = true;
                return CRUX_ERR_IO;
            }
            if (worker_made_progress(result)) {
                continue;
            }
        }
        if (result != WS_WANT_WRITE && error != WS_WANT_READ && error != WS_WANT_WRITE) {
            connection->transport_failed = true;
            return CRUX_ERR_IO;
        }
        status = wait_for_room_or_data(connection, deadline,
                                       result == WS_WANT_WRITE || error == WS_WANT_WRITE);
        if (status != CRUX_OK) {
            return status;
        }
    }
}

static crux_status_t channel_close(crux_ssh_channel_t *channel)
{
    if (channel == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!channel->closed) {
        channel->closed = true;
        (void)wolfSSH_stream_send_eof(connection_of(channel)->ssh);
    }
    return CRUX_OK;
}

/* ---- One connection, on its worker. ---- */

static bool close_requested_by_poll(connection_t *connection, int64_t period_ms)
{
    int64_t now = k_uptime_get();

    if (connection->attachment == NULL || now - connection->last_poll < period_ms) {
        return false;
    }
    connection->last_poll = now;
    return !server.callbacks.session_alive(server.callbacks.context, connection->attachment);
}

static bool open_sftp(connection_t *connection)
{
    crux_ssh_file_handle_t *handle = NULL;
    crux_status_t status =
        server.callbacks.sftp_open(server.callbacks.context, connection->attachment, &handle);

    connection->sftp_opened = true;
    if (status != CRUX_OK || handle == NULL) {
        connection->sftp_opened = false;
        return false;
    }
    connection->file_handle = handle;
    (void)wolfSSH_SetFilesystemHandle(connection->ssh, handle);
    connection->last_poll = k_uptime_get();
    return true;
}

/* Up to the end of wolfSSH_accept(): the login, the session request, and SFTP's opening exchange.
 */
static bool run_handshake(connection_t *connection)
{
    bool asked_for_sftp = false;

    connection->auth_deadline = k_uptime_get() + server.config.auth_timeout_ms;
    connection->last_poll = k_uptime_get();
    while (true) {
        int result = wolfSSH_accept(connection->ssh);
        int error = wolfSSH_get_error(connection->ssh);

        /* The request is granted by now, so this can only end the connection. */
        if (connection->request == REQUEST_SFTP && !asked_for_sftp) {
            asked_for_sftp = true;
            if (!open_sftp(connection)) {
                return false;
            }
        }
        if (result == WS_SUCCESS || result == WS_SFTP_COMPLETE) {
            return true;
        }
        if (error != WS_WANT_READ && error != WS_WANT_WRITE) {
            return false;
        }
        if (is_stopping()) {
            return false;
        }
        if (connection->attachment == NULL && k_uptime_get() >= connection->auth_deadline) {
            return false;
        }
        if (close_requested_by_poll(connection, connection->sftp_opened
                                                    ? (int64_t)server.config.io_timeout_ms
                                                    : WOLF_PRESESSION_POLL_MS)) {
            return false;
        }
        wait_socket(connection, error == WS_WANT_WRITE, WOLF_TICK_MS);
    }
}

static void serve_sftp(connection_t *connection)
{
    connection->last_poll = k_uptime_get();
    while (!is_stopping()) {
        int result = wolfSSH_SFTP_read(connection->ssh);
        int error = wolfSSH_get_error(connection->ssh);

        if (result >= 0 && error != WS_WANT_READ && error != WS_WANT_WRITE) {
            connection->last_poll = k_uptime_get();
            continue;
        }
        if (error == WS_WINDOW_FULL || error == WS_REKEYING || result == WS_REKEYING) {
            /* A reply is waiting on the client, and SFTP does not read while it waits. */
            result = wolfSSH_worker(connection->ssh, NULL);
            error = wolfSSH_get_error(connection->ssh);
            if (result == WS_CHANNEL_CLOSED) {
                return;
            }
            if (worker_made_progress(result)) {
                continue;
            }
            if (error == WS_WINDOW_FULL || error == WS_REKEYING) {
                wait_socket(connection, false, WOLF_TICK_MS);
                continue;
            }
        }
        if (error != WS_WANT_READ && error != WS_WANT_WRITE) {
            return; /* The client closed the channel or left, or the session failed. */
        }
        if (close_requested_by_poll(connection, (int64_t)server.config.io_timeout_ms)) {
            return;
        }
        wait_socket(connection, error == WS_WANT_WRITE, WOLF_TICK_MS);
    }
}

/* Lets what is queued reach the client, and lets its answer come back, for a bounded time. */
static void drain(connection_t *connection)
{
    int64_t deadline = k_uptime_get() + MAX((int64_t)server.config.io_timeout_ms, WOLF_DRAIN_MS);

    while (!is_stopping() && k_uptime_get() < deadline) {
        int result = wolfSSH_worker(connection->ssh, NULL);
        int error = wolfSSH_get_error(connection->ssh);

        if (result == WS_CHANNEL_CLOSED) {
            return;
        }
        if (result >= 0 || result == WS_CHAN_RXD || result == WS_EOF || error == WS_WINDOW_FULL ||
            error == WS_REKEYING) {
            continue;
        }
        if (error != WS_WANT_READ && error != WS_WANT_WRITE) {
            return;
        }
        wait_socket(connection, error == WS_WANT_WRITE, WOLF_TICK_MS);
    }
}

static void run_session(connection_t *connection)
{
    int status;

    connection->session_running = true;
    if (connection->request == REQUEST_SHELL) {
        status = server.callbacks.shell(server.callbacks.context, connection->attachment,
                                        &connection->channel, connection->terminal);
    } else if (wolfSSH_GetSessionCommand(connection->ssh) == NULL) {
        status = 255;
    } else {
        status =
            server.callbacks.exec(server.callbacks.context, connection->attachment,
                                  &connection->channel, wolfSSH_GetSessionCommand(connection->ssh),
                                  wolfSSH_GetSessionCommandSz(connection->ssh));
    }
    connection->session_running = false;
    if (status < 0 || status > 255) {
        status = 255;
    }
    if (!connection->transport_failed) {
        /* The client can lose the exit status if the socket closes under it, so wait for its close.
         */
        connection->flush_until = k_uptime_get() + WOLF_DRAIN_MS;
        (void)wolfSSH_stream_exit(connection->ssh, status);
        connection->flush_until = 0;
        drain(connection);
    }
}

static void close_sftp(connection_t *connection)
{
    /* This closes what the client left open, and is the last file operation of the session. */
    (void)wolfSSH_SFTP_free(connection->ssh);
    (void)wolfSSH_SetFilesystemHandle(connection->ssh, NULL);
    server.callbacks.sftp_close(server.callbacks.context, connection->attachment,
                                connection->file_handle);
    connection->file_handle = NULL;
}

static void serve_connection(connection_t *connection)
{
    bool established = run_handshake(connection);

    if (established && connection->request == REQUEST_SFTP) {
        serve_sftp(connection);
    } else if (established) {
        run_session(connection);
    }
    if (connection->file_handle != NULL) {
        close_sftp(connection);
    }
}

static void connection_thread(void *first, void *second, void *third)
{
    connection_t *connection = first;

    ARG_UNUSED(second);
    ARG_UNUSED(third);
    STACK_PROBE_START();
    connection->ssh = wolfSSH_new(server.ctx);
    if (connection->ssh != NULL) {
        /* In this order: setting the socket points the I/O contexts at wolfSSH's own copy of it. */
        (void)wolfSSH_set_fd(connection->ssh, connection->socket);
        wolfSSH_SetIOReadCtx(connection->ssh, connection);
        wolfSSH_SetIOWriteCtx(connection->ssh, connection);
        wolfSSH_SetUserAuthCtx(connection->ssh, connection);
        wolfSSH_SetUserAuthResultCtx(connection->ssh, connection);
        (void)wolfSSH_SetChannelReqCtx(connection->ssh, connection);
        serve_connection(connection);
#ifdef CRUX_SSH_WOLF_TESTING
        printk("SSH worker ended: %s\n", wolfSSH_get_error_name(connection->ssh));
#endif
        (void)wolfSSH_SendDisconnect(connection->ssh, WOLFSSH_DISCONNECT_BY_APPLICATION);
    }
    if (connection->attachment != NULL) {
        server.callbacks.connection_closed(server.callbacks.context, connection->attachment);
        connection->attachment = NULL;
    }
    wolfSSH_free(connection->ssh);
    connection->ssh = NULL;
    STACK_PROBE_RECORD(server.worker_stack_used);
    /* The slot is free before the client can see the close, so it can connect again at once. */
    atomic_set(&connection->state, SLOT_FINISHED);
    (void)zsock_close(connection->socket);
}

/* ---- The listener. ---- */

/*
 * Past max_connections there is no worker to spend on the client, so the listener answers in one
 * non-blocking send: the version line and a disconnect in the clear, which RFC 4253 allows before
 * keys. Reason 12 is "too many connections". The packet is 48 bytes, with 10 of padding.
 *
 * The client is writing its own version and key exchange by then, and a write to a closed socket
 * would reset the connection before it reads this. So the socket stays half closed for a moment, in
 * one of a few places, and a flood only shortens that moment for the oldest.
 */
static void refuse_connection(int socket)
{
    static const uint8_t refusal[] = WOLF_PROTO_ID "\x00\x00\x00\x2c\x0a"
                                                   "\x01\x00\x00\x00\x0c\x00\x00\x00\x14"
                                                   "too many connections"
                                                   "\x00\x00\x00\x00"
                                                   "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
    refused_socket_t *place = &server.refused[0];

    (void)zsock_send(socket, refusal, sizeof(refusal) - 1u, ZSOCK_MSG_DONTWAIT);
    (void)zsock_shutdown(socket, ZSOCK_SHUT_WR);
    for (size_t i = 0; i < ARRAY_SIZE(server.refused); ++i) {
        if (server.refused[i].socket < 0) {
            place = &server.refused[i];
            break;
        }
        if (server.refused[i].close_at < place->close_at) {
            place = &server.refused[i];
        }
    }
    if (place->socket >= 0) {
        (void)zsock_close(place->socket);
    }
    place->socket = socket;
    place->close_at = k_uptime_get() + WOLF_REFUSAL_HOLD_MS;
}

static void close_refused_sockets(bool all)
{
    int64_t now = k_uptime_get();

    for (size_t i = 0; i < ARRAY_SIZE(server.refused); ++i) {
        if (server.refused[i].socket >= 0 && (all || now >= server.refused[i].close_at)) {
            (void)zsock_close(server.refused[i].socket);
            server.refused[i].socket = -1;
        }
    }
}

static connection_t *take_slot(void)
{
    for (size_t i = 0; i < server.config.max_connections; ++i) {
        connection_t *connection = &server.connections[i];

        if (atomic_get(&connection->state) == SLOT_FINISHED) {
            (void)k_thread_join(&connection->thread, K_FOREVER);
            atomic_set(&connection->state, SLOT_FREE);
        }
        if (atomic_get(&connection->state) == SLOT_FREE) {
            return connection;
        }
    }
    return NULL;
}

static void record_peer(connection_t *connection, const struct net_sockaddr_storage *address)
{
    memset(&connection->peer, 0, sizeof(connection->peer));
    if (address->ss_family == NET_AF_INET) {
        const struct net_sockaddr_in *ipv4 = (const struct net_sockaddr_in *)address;

        memcpy(connection->peer.address, &ipv4->sin_addr, 4u);
        connection->peer.address_length = 4u;
        connection->peer_port = net_ntohs(ipv4->sin_port);
    }
#ifdef CONFIG_NET_IPV6
    else if (address->ss_family == NET_AF_INET6) {
        const struct net_sockaddr_in6 *ipv6 = (const struct net_sockaddr_in6 *)address;

        memcpy(connection->peer.address, &ipv6->sin6_addr, 16u);
        connection->peer.address_length = 16u;
        connection->peer_port = net_ntohs(ipv6->sin6_port);
    }
#endif
}

static void start_connection(int socket, const struct net_sockaddr_storage *address)
{
    connection_t *connection = take_slot();
    size_t index;

    if (connection == NULL) {
        refuse_connection(socket);
        return;
    }
    index = (size_t)(connection - server.connections);
    connection->socket = socket;
    connection->attachment = NULL;
    connection->channel.closed = false;
    connection->request = REQUEST_NONE;
    connection->terminal = false;
    connection->sftp_opened = false;
    connection->file_handle = NULL;
    connection->session_running = false;
    connection->transport_failed = false;
    connection->received_since_wait = 0u;
    connection->flush_until = 0;
#ifdef CRUX_SSH_WOLF_TESTING
    connection->drop_on_write = false;
#endif
    record_peer(connection, address);
    atomic_set(&connection->state, SLOT_RUNNING);
    (void)k_thread_create(&connection->thread, worker_stacks[index],
                          K_THREAD_STACK_SIZEOF(worker_stacks[index]), connection_thread,
                          connection, NULL, NULL, CRUX_SSH_WOLF_THREAD_PRIORITY, 0, K_NO_WAIT);
}

static void listener_thread(void *first, void *second, void *third)
{
    ARG_UNUSED(first);
    ARG_UNUSED(second);
    ARG_UNUSED(third);
    STACK_PROBE_START();
    while (!is_stopping()) {
        struct zsock_pollfd poll_entry = {.fd = server.listener_socket, .events = ZSOCK_POLLIN};
        struct net_sockaddr_storage address;
        net_socklen_t address_length = sizeof(address);
        int socket;

        close_refused_sockets(false);
        if (zsock_poll(&poll_entry, 1, WOLF_TICK_MS) <= 0) {
            continue;
        }
        socket =
            zsock_accept(server.listener_socket, (struct net_sockaddr *)&address, &address_length);
        if (socket >= 0) {
            start_connection(socket, &address);
        } else {
            k_msleep(WOLF_TICK_MS); /* Out of sockets, perhaps, so do not spin on it. */
        }
    }
    close_refused_sockets(true);
    STACK_PROBE_RECORD(server.listener_stack_used);
}

/* ---- Start and stop. ---- */

/*
 * The key in the form wolfSSH reads. These are fixed DER around the store's parts, so that there is
 * nothing to parse: PKCS#8 for Ed25519 (RFC 8410), and SEC 1 ECPrivateKey for P-256 (RFC 5915).
 */
static int load_ed25519_host_key(const crux_ssh_config_t *config)
{
    static const uint8_t prefix[] = {0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06,
                                     0x03, 0x2b, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20};
    uint8_t der[sizeof(prefix) + HOST_KEY_PRIVATE_BYTES];
    int result;

    memcpy(der, prefix, sizeof(prefix));
    memcpy(der + sizeof(prefix), config->host_key, HOST_KEY_PRIVATE_BYTES);
    result = wolfSSH_CTX_UsePrivateKey_buffer(server.ctx, der, sizeof(der), WOLFSSH_FORMAT_ASN1);
    wipe(der, sizeof(der));
    return result;
}

static int load_p256_host_key(const crux_ssh_config_t *config)
{
    static const uint8_t prefix[] = {0x30, 0x77, 0x02, 0x01, 0x01, 0x04, 0x20};
    static const uint8_t middle[] = {0xa0, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d,
                                     0x03, 0x01, 0x07, 0xa1, 0x44, 0x03, 0x42, 0x00};
    uint8_t der[sizeof(prefix) + HOST_KEY_PRIVATE_BYTES + sizeof(middle) + 65u];
    uint8_t *at = der;
    int result;

    memcpy(at, prefix, sizeof(prefix));
    at += sizeof(prefix);
    memcpy(at, config->host_key, HOST_KEY_PRIVATE_BYTES);
    at += HOST_KEY_PRIVATE_BYTES;
    memcpy(at, middle, sizeof(middle));
    at += sizeof(middle);
    memcpy(at, config->host_key + HOST_KEY_PRIVATE_BYTES, 65u);
    result = wolfSSH_CTX_UsePrivateKey_buffer(server.ctx, der, sizeof(der), WOLFSSH_FORMAT_ASN1);
    wipe(der, sizeof(der));
    return result;
}

/*
 * wolfSSH derives what it sends from the private part, and the fingerprint comes from the public
 * part. So a record whose parts disagree would show users a fingerprint that the server never
 * presents.
 */
static bool ed25519_parts_agree(const crux_ssh_config_t *config)
{
    ed25519_key key;
    uint8_t derived[ED25519_PUB_KEY_SIZE];
    bool agree = false;

    if (wc_ed25519_init(&key) == 0) {
        agree =
            wc_ed25519_import_private_only(config->host_key, HOST_KEY_PRIVATE_BYTES, &key) == 0 &&
            wc_ed25519_make_public(&key, derived, sizeof(derived)) == 0 &&
            memcmp(derived, config->host_key + HOST_KEY_PRIVATE_BYTES, sizeof(derived)) == 0;
        wc_ed25519_free(&key);
    }
    return agree;
}

static bool p256_parts_agree(const crux_ssh_config_t *config)
{
    ecc_key key;
    WC_RNG rng;
    uint8_t derived[65];
    word32 derived_length = sizeof(derived);
    bool agree = false;

    if (wc_InitRng(&rng) != 0) {
        return false;
    }
    if (wc_ecc_init(&key) == 0) {
        agree = wc_ecc_import_private_key_ex(config->host_key, HOST_KEY_PRIVATE_BYTES, NULL, 0u,
                                             &key, ECC_SECP256R1) == 0 &&
                wc_ecc_make_pub_ex(&key, NULL, &rng) == 0 &&
                wc_ecc_export_x963(&key, derived, &derived_length) == 0 &&
                derived_length == sizeof(derived) &&
                memcmp(derived, config->host_key + HOST_KEY_PRIVATE_BYTES, sizeof(derived)) == 0;
        wc_ecc_free(&key);
    }
    (void)wc_FreeRng(&rng);
    return agree;
}

static bool load_host_key(const crux_ssh_config_t *config)
{
    if (config->host_key_type == CRUX_SSH_KEY_ED25519) {
        return config->host_key_length == HOST_KEY_ED25519_BYTES && ed25519_parts_agree(config) &&
               load_ed25519_host_key(config) == WS_SUCCESS &&
               wolfSSH_CTX_SetAlgoListKey(server.ctx, algorithm_ed25519) == WS_SUCCESS;
    }
    return config->host_key_length == HOST_KEY_P256_BYTES && p256_parts_agree(config) &&
           load_p256_host_key(config) == WS_SUCCESS &&
           wolfSSH_CTX_SetAlgoListKey(server.ctx, algorithm_p256) == WS_SUCCESS;
}

static bool create_context(const crux_ssh_config_t *config)
{
    server.ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL);
    if (server.ctx == NULL) {
        return false;
    }
    wolfSSH_SetIORecv(server.ctx, io_receive);
    wolfSSH_SetIOSend(server.ctx, io_send);
    wolfSSH_SetUserAuth(server.ctx, user_auth);
    wolfSSH_SetUserAuthResult(server.ctx, user_auth_result);
    return wolfSSH_CTX_SetStrictKex(server.ctx, 1) == WS_SUCCESS &&
           wolfSSH_CTX_SetAlgoListKex(server.ctx, WOLF_KEX_LIST) == WS_SUCCESS &&
           wolfSSH_CTX_SetAlgoListKeyAccepted(server.ctx, WOLF_USER_KEY_LIST) == WS_SUCCESS &&
           wolfSSH_CTX_SetAlgoListCipher(server.ctx, WOLF_CIPHER_LIST) == WS_SUCCESS &&
           wolfSSH_CTX_SetAlgoListMac(server.ctx, WOLF_MAC_LIST) == WS_SUCCESS &&
           wolfSSH_CTX_SetChannelReqAnyCb(server.ctx, channel_request) == WS_SUCCESS &&
           wolfSSH_CTX_SetMaxAuthAttempts(server.ctx, config->max_auth_attempts) == WS_SUCCESS &&
           wolfSSH_CTX_SetWindowPacketSize(server.ctx, CRUX_SSH_WOLF_WINDOW_BYTES,
                                           CRUX_SSH_WOLF_PACKET_BYTES) == WS_SUCCESS &&
           wolfSSH_CTX_SetSshProtoIdStr(server.ctx, WOLF_PROTO_ID) == WS_SUCCESS &&
           load_host_key(config);
}

static bool open_listener(uint16_t port)
{
    static const int enable = 1;
    struct net_sockaddr_in address = {
        .sin_family = NET_AF_INET,
        .sin_port = net_htons(port),
        .sin_addr = NET_INADDR_ANY_INIT,
    };

    server.listener_socket = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    if (server.listener_socket < 0) {
        return false;
    }
    (void)zsock_setsockopt(server.listener_socket, ZSOCK_SOL_SOCKET, ZSOCK_SO_REUSEADDR, &enable,
                           sizeof(enable));
    if (zsock_bind(server.listener_socket, (struct net_sockaddr *)&address, sizeof(address)) < 0 ||
        zsock_listen(server.listener_socket, CRUX_SSH_WOLF_CONNECTIONS_MAX) < 0) {
        (void)zsock_close(server.listener_socket);
        return false;
    }
    return true;
}

static crux_status_t check_arguments(const crux_ssh_config_t *config,
                                     const crux_ssh_callbacks_t *callbacks)
{
    if (config == NULL || callbacks == NULL || callbacks->password == NULL ||
        callbacks->key_check == NULL || callbacks->key_verified == NULL ||
        callbacks->key_failed == NULL || callbacks->shell == NULL || callbacks->exec == NULL ||
        callbacks->sftp_open == NULL || callbacks->sftp_close == NULL ||
        callbacks->session_alive == NULL || callbacks->connection_closed == NULL ||
        config->max_connections == 0u || config->max_auth_attempts == 0u ||
        config->auth_timeout_ms == 0u || config->io_timeout_ms == 0u || config->host_key == NULL ||
        config->host_key_length == 0u ||
        (config->host_key_type != CRUX_SSH_KEY_ED25519 &&
         config->host_key_type != CRUX_SSH_KEY_ECDSA_P256)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    return config->max_connections > CRUX_SSH_WOLF_CONNECTIONS_MAX ? CRUX_ERR_OUT_OF_RANGE
                                                                   : CRUX_OK;
}

static void free_context(void)
{
    if (server.ctx != NULL) {
        wolfSSH_CTX_free(server.ctx); /* It wipes the key it holds. */
        server.ctx = NULL;
    }
    (void)wolfSSH_Cleanup();
}

static crux_status_t provider_start(const crux_ssh_config_t *config,
                                    const crux_ssh_callbacks_t *callbacks)
{
    crux_status_t status = check_arguments(config, callbacks);

    if (status != CRUX_OK) {
        return status;
    }
    (void)k_mutex_lock(&server_lock, K_FOREVER);
    if (server.started) {
        status = CRUX_ERR_INVALID_STATE;
    } else if (wolfSSH_Init() != WS_SUCCESS) {
        status = CRUX_ERR_IO;
    } else if (!create_context(config) || !open_listener(config->port)) {
        free_context();
        status = CRUX_ERR_IO;
    } else {
        server.config = *config;
        server.callbacks = *callbacks;
        atomic_set(&server.stopping, 0);
        for (size_t i = 0; i < ARRAY_SIZE(server.connections); ++i) {
            atomic_set(&server.connections[i].state, SLOT_FREE);
        }
        for (size_t i = 0; i < ARRAY_SIZE(server.refused); ++i) {
            server.refused[i].socket = -1;
        }
        (void)k_thread_create(&server.listener, listener_stack,
                              K_THREAD_STACK_SIZEOF(listener_stack), listener_thread, NULL, NULL,
                              NULL, CRUX_SSH_WOLF_THREAD_PRIORITY, 0, K_NO_WAIT);
        server.started = true;
    }
    (void)k_mutex_unlock(&server_lock);
    return status;
}

static crux_status_t provider_stop(void)
{
    if (on_worker_thread()) {
        return CRUX_ERR_INVALID_STATE;
    }
    (void)k_mutex_lock(&server_lock, K_FOREVER);
    if (!server.started) {
        (void)k_mutex_unlock(&server_lock);
        return CRUX_ERR_INVALID_STATE;
    }
    atomic_set(&server.stopping, 1);
    (void)k_thread_join(&server.listener, K_FOREVER);
    (void)zsock_close(server.listener_socket);
    for (size_t i = 0; i < ARRAY_SIZE(server.connections); ++i) {
        if (atomic_get(&server.connections[i].state) != SLOT_FREE) {
            (void)k_thread_join(&server.connections[i].thread, K_FOREVER);
            atomic_set(&server.connections[i].state, SLOT_FREE);
        }
    }
    free_context();
    server.started = false;
    (void)k_mutex_unlock(&server_lock);
    return CRUX_OK;
}

static const crux_ssh_provider_t provider = {
    .start = provider_start,
    .stop = provider_stop,
    .read = channel_read,
    .write = channel_write,
    .close = channel_close,
};

const crux_ssh_provider_t *crux_ssh_wolf_provider(void)
{
    return &provider;
}

/* ---- Fingerprint. ---- */

static uint8_t *put_string(uint8_t *at, const void *text, size_t length)
{
    sys_put_be32((uint32_t)length, at);
    memcpy(at + sizeof(uint32_t), text, length);
    return at + sizeof(uint32_t) + length;
}

static void base64_without_padding(const uint8_t *data, size_t length, char *out)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t bits = 0u;
    unsigned bit_count = 0u;

    for (size_t i = 0; i < length; ++i) {
        bits = (bits << 8u) | data[i];
        bit_count += 8u;
        while (bit_count >= 6u) {
            bit_count -= 6u;
            *out++ = alphabet[(bits >> bit_count) & 0x3fu];
        }
    }
    if (bit_count > 0u) {
        *out++ = alphabet[(bits << (6u - bit_count)) & 0x3fu];
    }
    *out = '\0';
}

crux_status_t crux_ssh_wolf_host_key_fingerprint(crux_ssh_key_type_t type, const uint8_t *host_key,
                                                 size_t host_key_length, char *fingerprint,
                                                 size_t size)
{
    uint8_t blob[4u + sizeof(algorithm_p256) - 1u + 4u + sizeof(curve_p256) - 1u + 4u + 65u];
    uint8_t digest[WC_SHA256_DIGEST_SIZE];
    uint8_t *at = blob;
    static const char label[] = "SHA256:";

    if (host_key == NULL || fingerprint == NULL || size < CRUX_SSH_WOLF_FINGERPRINT_SIZE) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (type == CRUX_SSH_KEY_ED25519 && host_key_length == HOST_KEY_ED25519_BYTES) {
        at = put_string(at, algorithm_ed25519, sizeof(algorithm_ed25519) - 1u);
        at = put_string(at, host_key + HOST_KEY_PRIVATE_BYTES, 32u);
    } else if (type == CRUX_SSH_KEY_ECDSA_P256 && host_key_length == HOST_KEY_P256_BYTES) {
        at = put_string(at, algorithm_p256, sizeof(algorithm_p256) - 1u);
        at = put_string(at, curve_p256, sizeof(curve_p256) - 1u);
        at = put_string(at, host_key + HOST_KEY_PRIVATE_BYTES, 65u);
    } else {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (wc_Sha256Hash(blob, (word32)(at - blob), digest) != 0) {
        return CRUX_ERR_IO;
    }
    memcpy(fingerprint, label, sizeof(label) - 1u);
    base64_without_padding(digest, sizeof(digest), fingerprint + sizeof(label) - 1u);
    return CRUX_OK;
}

#ifdef CRUX_SSH_WOLF_TESTING
static connection_t *running_connection(uint16_t peer_port)
{
    for (size_t i = 0; i < ARRAY_SIZE(server.connections); ++i) {
        if (atomic_get(&server.connections[i].state) == SLOT_RUNNING &&
            server.connections[i].peer_port == peer_port) {
            return &server.connections[i];
        }
    }
    return NULL;
}

void crux_ssh_wolf_test_drop_on_write(uint16_t peer_port)
{
    connection_t *connection = running_connection(peer_port);

    if (connection != NULL) {
        connection->drop_on_write = true;
    }
}

crux_ssh_file_handle_t *crux_ssh_wolf_test_file_handle(uint16_t peer_port)
{
    connection_t *connection = running_connection(peer_port);

    if (connection == NULL || connection->ssh == NULL || connection->file_handle == NULL) {
        return NULL;
    }
    return wolfSSH_GetFilesystemHandle(connection->ssh);
}

void crux_ssh_wolf_test_stack_used(size_t *worker, size_t *listener)
{
    *worker = server.worker_stack_used;
    *listener = server.listener_stack_used;
}
#endif /* CRUX_SSH_WOLF_TESTING */
