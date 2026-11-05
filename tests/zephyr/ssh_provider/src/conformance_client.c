/*
 * The scripted client that runs the conformance cases of tests/ssh on the wolfSSH provider, over a
 * real socket. TEST ONLY. It is a small SSH client on wolfCrypt, because the cases need requests
 * that a normal client never sends: a bad signature, an unknown key type, a name with a NUL in it.
 * It offers one set of algorithms: curve25519-sha256, ssh-ed25519, aes128-ctr and hmac-sha2-256,
 * with strict key exchange. It runs on one thread, and the provider's workers run while it waits on
 * a socket.
 *
 * Where a real socket cannot do what ssh_test_client.h describes:
 * - set_window. A window cannot shrink, so a channel opens with none and the client grants it just
 *   before a session request.
 * - send and send_eof before a session. wolfSSH_accept() ends the connection on channel data that
 *   comes before a session request, so the client holds it and sends it right after the grant. No
 *   client needs to send it sooner, and OpenSSH does not.
 * - drop_on_write and file_handle. These use the provider's test hooks.
 * - pass_read_timeout. A real wait, while the other SFTP connections keep sending requests, because
 *   otherwise each connection would poll on its own timer at the same moment.
 */
#include "conformance.h"
#include "crux_ssh_wolf_provider.h"
#include "test_support.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/curve25519.h>
#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/wolfcrypt/hmac.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/sha256.h>

#include <stdio.h>
#include <string.h>

#define SERVER_PORT 2222 /* The port of valid_config in tests/ssh/conformance.c. */
#define CONNECTIONS_MAX 24
#define OUTPUT_MAX 1024u
#define PACKET_MAX 20000u
#define SETTLE_MS 30
#define WAIT_MS 3000
#define CASE_AUTH_LIMIT_MS 1100  /* A little over auth_timeout_ms of valid_config. */
#define CASE_READ_TIMEOUT_MS 110 /* A little over io_timeout_ms of valid_config. */
#define BUSY_PERIOD_MS 30
#define DISCONNECT_SETTLE_MS 150
#define UNLIMITED_WINDOW 0x100000u

enum {
    MSG_DISCONNECT = 1,
    MSG_SERVICE_REQUEST = 5,
    MSG_SERVICE_ACCEPT = 6,
    MSG_KEXINIT = 20,
    MSG_NEWKEYS = 21,
    MSG_KEX_ECDH_INIT = 30,
    MSG_KEX_ECDH_REPLY = 31,
    MSG_USERAUTH_REQUEST = 50,
    MSG_USERAUTH_FAILURE = 51,
    MSG_USERAUTH_SUCCESS = 52,
    MSG_USERAUTH_PK_OK = 60,
    MSG_GLOBAL_REQUEST = 80,
    MSG_REQUEST_SUCCESS = 81,
    MSG_REQUEST_FAILURE = 82,
    MSG_CHANNEL_OPEN = 90,
    MSG_CHANNEL_OPEN_CONFIRMATION = 91,
    MSG_CHANNEL_OPEN_FAILURE = 92,
    MSG_CHANNEL_WINDOW_ADJUST = 93,
    MSG_CHANNEL_DATA = 94,
    MSG_CHANNEL_EOF = 96,
    MSG_CHANNEL_CLOSE = 97,
    MSG_CHANNEL_REQUEST = 98,
    MSG_CHANNEL_SUCCESS = 99,
    MSG_CHANNEL_FAILURE = 100,
};

typedef enum { ANSWER_NONE = 0, ANSWER_YES, ANSWER_NO } answer_t;

typedef struct {
    bool used;
    int socket;
    uint16_t local_port;
    bool closed;
    bool disconnect_seen;
    bool keyed;
    uint32_t send_sequence;
    uint32_t receive_sequence;
    Aes send_cipher;
    Aes receive_cipher;
    uint8_t send_mac[WC_SHA256_DIGEST_SIZE];
    uint8_t receive_mac[WC_SHA256_DIGEST_SIZE];
    uint8_t session_id[WC_SHA256_DIGEST_SIZE];
    answer_t service;
    answer_t auth;
    answer_t open;
    answer_t request;
    answer_t global;
    bool has_channel;
    uint32_t remote_channel;
    bool channel_closed;
    bool window_limited;
    size_t window;
    bool session_granted;
    bool sftp;
    bool sftp_init_sent;
    uint8_t output[OUTPUT_MAX];
    size_t output_length;
    bool output_eof;
    bool has_exit_status;
    int exit_status;
    uint8_t held[OUTPUT_MAX];
    size_t held_length;
    bool held_eof;
    uint8_t server_kexinit[1024];
    size_t server_kexinit_length;
    uint8_t kex_reply[512];
    size_t kex_reply_length;
    bool newkeys;
} connection_t;

static connection_t connections[CONNECTIONS_MAX];
static uint8_t send_buffer[PACKET_MAX];
static uint8_t receive_buffer[PACKET_MAX];
static test_key_t host_key;
static test_key_t ed25519_user;
static test_key_t ecdsa_user;
static WC_RNG rng;
static crux_ssh_test_client_t client;

/* ---- Writing fields ---- */

static size_t put_u32(uint8_t *out, uint32_t value)
{
    sys_put_be32(value, out);
    return 4u;
}

static size_t put_bytes(uint8_t *out, const void *data, size_t length)
{
    sys_put_be32((uint32_t)length, out);
    if (length > 0u) {
        memcpy(out + 4, data, length);
    }
    return 4u + length;
}

static size_t put_text(uint8_t *out, const char *text)
{
    return put_bytes(out, text, strlen(text));
}

static size_t put_mpint_of_bytes(uint8_t *out, const uint8_t *data, size_t length)
{
    size_t lead = 0;

    while (length > 0u && data[0] == 0u) {
        ++data;
        --length;
    }
    lead = (length > 0u && (data[0] & 0x80u) != 0u) ? 1u : 0u;
    sys_put_be32((uint32_t)(length + lead), out);
    out[4] = 0u;
    memcpy(out + 4 + lead, data, length);
    return 4u + lead + length;
}

/* ---- Packets ---- */

static bool write_all(connection_t *connection, const uint8_t *data, size_t length)
{
    while (length > 0u) {
        ssize_t sent = zsock_send(connection->socket, data, length, 0);

        if (sent <= 0) {
            return false;
        }
        data += sent;
        length -= (size_t)sent;
    }
    return true;
}

static void mark_closed(connection_t *connection)
{
    if (!connection->closed) {
        connection->closed = true;
        (void)zsock_close(connection->socket);
    }
}

static bool send_packet(connection_t *connection, const uint8_t *payload, size_t length)
{
    const size_t block = connection->keyed ? 16u : 8u;
    size_t padding = block - ((5u + length) % block);
    size_t total;

    if (connection->closed) {
        return false;
    }
    if (padding < 4u) {
        padding += block;
    }
    total = 5u + length + padding;
    if (total + WC_SHA256_DIGEST_SIZE > sizeof(send_buffer)) {
        return false;
    }
    sys_put_be32((uint32_t)(total - 4u), send_buffer);
    send_buffer[4] = (uint8_t)padding;
    memcpy(send_buffer + 5, payload, length);
    memset(send_buffer + 5 + length, 0, padding);
    if (connection->keyed) {
        Hmac hmac;
        uint8_t sequence[4];

        sys_put_be32(connection->send_sequence, sequence);
        (void)wc_HmacInit(&hmac, NULL, INVALID_DEVID);
        (void)wc_HmacSetKey(&hmac, WC_SHA256, connection->send_mac, WC_SHA256_DIGEST_SIZE);
        (void)wc_HmacUpdate(&hmac, sequence, sizeof(sequence));
        (void)wc_HmacUpdate(&hmac, send_buffer, (word32)total);
        (void)wc_HmacFinal(&hmac, send_buffer + total);
        wc_HmacFree(&hmac);
        (void)wc_AesCtrEncrypt(&connection->send_cipher, send_buffer, send_buffer, (word32)total);
        total += WC_SHA256_DIGEST_SIZE;
    }
    ++connection->send_sequence;
    return write_all(connection, send_buffer, total);
}

/* Reads exactly length bytes. The first byte may take up to wait_ms, and the rest are on their way.
 */
static int read_exact(connection_t *connection, uint8_t *data, size_t length, int32_t wait_ms)
{
    size_t have = 0;

    while (have < length) {
        struct zsock_pollfd entry = {.fd = connection->socket, .events = ZSOCK_POLLIN};
        ssize_t received;

        if (zsock_poll(&entry, 1, have == 0u ? wait_ms : WAIT_MS) <= 0) {
            return have == 0u ? 0 : -1;
        }
        received = zsock_recv(connection->socket, data + have, length - have, 0);
        if (received <= 0) {
            return -1;
        }
        have += (size_t)received;
    }
    return 1;
}

/* Returns the payload's length, 0 if nothing arrived in wait_ms, and -1 once the socket has ended.
 */
static int receive_packet(connection_t *connection, int32_t wait_ms, const uint8_t **payload)
{
    const size_t block = connection->keyed ? 16u : 8u;
    uint32_t packet_length;
    size_t rest;
    int result;

    if (connection->closed) {
        return -1;
    }
    result = read_exact(connection, receive_buffer, block, wait_ms);
    if (result <= 0) {
        if (result < 0) {
            mark_closed(connection);
        }
        return result;
    }
    if (connection->keyed) {
        (void)wc_AesCtrEncrypt(&connection->receive_cipher, receive_buffer, receive_buffer,
                               (word32)block);
    }
    packet_length = sys_get_be32(receive_buffer);
    if (packet_length < 1u || packet_length + 4u + WC_SHA256_DIGEST_SIZE > sizeof(receive_buffer) ||
        packet_length + 4u < block) {
        mark_closed(connection);
        return -1;
    }
    rest = packet_length + 4u - block;
    if (rest > 0u && read_exact(connection, receive_buffer + block, rest, WAIT_MS) <= 0) {
        mark_closed(connection);
        return -1;
    }
    if (connection->keyed) {
        uint8_t mac[WC_SHA256_DIGEST_SIZE];
        uint8_t expected[WC_SHA256_DIGEST_SIZE];
        uint8_t sequence[4];
        Hmac hmac;

        (void)wc_AesCtrEncrypt(&connection->receive_cipher, receive_buffer + block,
                               receive_buffer + block, (word32)rest);
        if (read_exact(connection, mac, sizeof(mac), WAIT_MS) <= 0) {
            mark_closed(connection);
            return -1;
        }
        sys_put_be32(connection->receive_sequence, sequence);
        (void)wc_HmacInit(&hmac, NULL, INVALID_DEVID);
        (void)wc_HmacSetKey(&hmac, WC_SHA256, connection->receive_mac, WC_SHA256_DIGEST_SIZE);
        (void)wc_HmacUpdate(&hmac, sequence, sizeof(sequence));
        (void)wc_HmacUpdate(&hmac, receive_buffer, packet_length + 4u);
        (void)wc_HmacFinal(&hmac, expected);
        wc_HmacFree(&hmac);
        if (memcmp(mac, expected, sizeof(mac)) != 0) {
            printf("client: bad MAC from the server\n");
            mark_closed(connection);
            return -1;
        }
    }
    ++connection->receive_sequence;
    *payload = receive_buffer + 5;
    return (int)(packet_length - 1u - receive_buffer[4]);
}

/* ---- Messages ---- */

static void send_channel_close(connection_t *connection)
{
    uint8_t message[5];

    message[0] = MSG_CHANNEL_CLOSE;
    (void)put_u32(message + 1, connection->remote_channel);
    (void)send_packet(connection, message, sizeof(message));
}

static void dispatch_channel_request(connection_t *connection, const uint8_t *payload, int length)
{
    /* channel, type, want reply, and for exit-status a number. */
    uint32_t type_length;

    if (length < 9) {
        return;
    }
    type_length = sys_get_be32(payload + 5);
    if (type_length == 11u && (size_t)length >= 9u + 11u + 1u + 4u &&
        memcmp(payload + 9, "exit-status", 11) == 0) {
        connection->has_exit_status = true;
        connection->exit_status = (int)sys_get_be32(payload + 9 + 11 + 1);
    }
}

static void dispatch(connection_t *connection, const uint8_t *payload, int length)
{
    if (length < 1) {
        return;
    }
    switch (payload[0]) {
    case MSG_DISCONNECT:
        connection->disconnect_seen = true;
        break;
    case MSG_SERVICE_ACCEPT:
        connection->service = ANSWER_YES;
        break;
    case MSG_USERAUTH_FAILURE:
        connection->auth = ANSWER_NO;
        break;
    case MSG_USERAUTH_SUCCESS:
    case MSG_USERAUTH_PK_OK:
        connection->auth = ANSWER_YES;
        break;
    case MSG_REQUEST_SUCCESS:
        connection->global = ANSWER_YES;
        break;
    case MSG_REQUEST_FAILURE:
        connection->global = ANSWER_NO;
        break;
    case MSG_CHANNEL_OPEN_CONFIRMATION:
        if (length >= 9) {
            connection->remote_channel = sys_get_be32(payload + 5);
            connection->has_channel = true;
            connection->open = ANSWER_YES;
        }
        break;
    case MSG_CHANNEL_OPEN_FAILURE:
        connection->open = ANSWER_NO;
        break;
    case MSG_CHANNEL_DATA:
        if (length >= 9) {
            size_t count = sys_get_be32(payload + 5);

            if (count > OUTPUT_MAX - connection->output_length) {
                count = OUTPUT_MAX - connection->output_length;
            }
            memcpy(connection->output + connection->output_length, payload + 9, count);
            connection->output_length += count;
        }
        break;
    case MSG_CHANNEL_EOF:
        connection->output_eof = true;
        break;
    case MSG_CHANNEL_CLOSE:
        if (!connection->channel_closed) {
            connection->channel_closed = true;
            send_channel_close(connection);
        }
        break;
    case MSG_CHANNEL_REQUEST:
        dispatch_channel_request(connection, payload, length);
        break;
    case MSG_CHANNEL_SUCCESS:
        connection->request = ANSWER_YES;
        break;
    case MSG_CHANNEL_FAILURE:
        connection->request = ANSWER_NO;
        break;
    case MSG_NEWKEYS:
        connection->newkeys = true;
        break;
    case MSG_KEXINIT:
        if ((size_t)length <= sizeof(connection->server_kexinit)) {
            memcpy(connection->server_kexinit, payload, (size_t)length);
            connection->server_kexinit_length = (size_t)length;
        }
        break;
    case MSG_KEX_ECDH_REPLY:
        if ((size_t)length <= sizeof(connection->kex_reply)) {
            memcpy(connection->kex_reply, payload, (size_t)length);
            connection->kex_reply_length = (size_t)length;
        }
        break;
    default:
        break;
    }
}

static void pump_once(connection_t *connection, int32_t wait_ms)
{
    const uint8_t *payload = NULL;
    int length = receive_packet(connection, wait_ms, &payload);

    if (length > 0) {
        dispatch(connection, payload, length);
    }
}

/* Pumps until the answer arrives or the socket ends. */
static answer_t wait_answer(connection_t *connection, const answer_t *answer)
{
    int64_t deadline = k_uptime_get() + WAIT_MS;

    while (*answer == ANSWER_NONE && !connection->closed && k_uptime_get() < deadline) {
        pump_once(connection, 50);
    }
    return *answer;
}

static void wait_closed(connection_t *connection, int32_t wait_ms)
{
    int64_t deadline = k_uptime_get() + wait_ms;

    while (!connection->closed && k_uptime_get() < deadline) {
        pump_once(connection, 50);
    }
}

/* ---- Key exchange ---- */

static size_t put_kexinit(uint8_t *out)
{
    size_t at = 0;

    out[at++] = MSG_KEXINIT;
    (void)wc_RNG_GenerateBlock(&rng, out + at, 16);
    at += 16u;
    at += put_text(out + at, "curve25519-sha256,kex-strict-c-v00@openssh.com");
    at += put_text(out + at, "ssh-ed25519");
    at += put_text(out + at, "aes128-ctr");
    at += put_text(out + at, "aes128-ctr");
    at += put_text(out + at, "hmac-sha2-256");
    at += put_text(out + at, "hmac-sha2-256");
    at += put_text(out + at, "none");
    at += put_text(out + at, "none");
    at += put_text(out + at, "");
    at += put_text(out + at, "");
    out[at++] = 0u;
    at += put_u32(out + at, 0u);
    return at;
}

static void derive_key(const uint8_t *k_mpint, size_t k_length, const uint8_t hash[32], char letter,
                       const uint8_t session_id[32], uint8_t out[32])
{
    wc_Sha256 sha;
    uint8_t single = (uint8_t)letter;

    (void)wc_InitSha256(&sha);
    (void)wc_Sha256Update(&sha, k_mpint, (word32)k_length);
    (void)wc_Sha256Update(&sha, hash, 32);
    (void)wc_Sha256Update(&sha, &single, 1);
    (void)wc_Sha256Update(&sha, session_id, 32);
    (void)wc_Sha256Final(&sha, out);
    wc_Sha256Free(&sha);
}

static bool set_keys(connection_t *connection, const uint8_t *k_mpint, size_t k_length,
                     const uint8_t hash[32])
{
    uint8_t iv_send[32];
    uint8_t iv_receive[32];
    uint8_t key_send[32];
    uint8_t key_receive[32];

    memcpy(connection->session_id, hash, 32);
    derive_key(k_mpint, k_length, hash, 'A', hash, iv_send);
    derive_key(k_mpint, k_length, hash, 'B', hash, iv_receive);
    derive_key(k_mpint, k_length, hash, 'C', hash, key_send);
    derive_key(k_mpint, k_length, hash, 'D', hash, key_receive);
    derive_key(k_mpint, k_length, hash, 'E', hash, connection->send_mac);
    derive_key(k_mpint, k_length, hash, 'F', hash, connection->receive_mac);
    return wc_AesInit(&connection->send_cipher, NULL, INVALID_DEVID) == 0 &&
           wc_AesInit(&connection->receive_cipher, NULL, INVALID_DEVID) == 0 &&
           wc_AesSetKey(&connection->send_cipher, key_send, 16, iv_send, AES_ENCRYPTION) == 0 &&
           wc_AesSetKey(&connection->receive_cipher, key_receive, 16, iv_receive, AES_ENCRYPTION) ==
               0;
}

static bool verify_host_key(const uint8_t *host_blob, uint32_t blob_length,
                            const uint8_t *signature, uint32_t signature_length,
                            const uint8_t hash[32])
{
    ed25519_key key;
    int verified = 0;
    /* The signature blob is the algorithm's name, then 64 bytes. */
    const uint32_t expected = 4u + 11u + 4u + 64u;

    if (blob_length != host_key.blob_length || memcmp(host_blob, host_key.blob, blob_length) != 0 ||
        signature_length != expected) {
        return false;
    }
    (void)wc_ed25519_init(&key);
    if (wc_ed25519_import_public(host_key.host_key + 32, 32, &key) != 0 ||
        wc_ed25519_verify_msg(signature + 4 + 11 + 4, 64, hash, 32, &verified, &key) != 0) {
        verified = 0;
    }
    wc_ed25519_free(&key);
    return verified == 1;
}

static bool exchange_keys(connection_t *connection, const char *server_version,
                          const char *client_version)
{
    static uint8_t client_kexinit[256];
    curve25519_key mine;
    curve25519_key theirs;
    uint8_t public_key[CURVE25519_KEYSIZE];
    uint8_t secret[CURVE25519_KEYSIZE];
    word32 public_length = sizeof(public_key);
    word32 secret_length = sizeof(secret);
    uint8_t message[64];
    uint8_t k_mpint[40];
    uint8_t hash[32];
    uint8_t scratch[8];
    size_t kexinit_length = put_kexinit(client_kexinit);
    size_t k_length;
    const uint8_t *host_blob;
    const uint8_t *server_public;
    const uint8_t *signature;
    uint32_t host_length;
    uint32_t server_public_length;
    uint32_t signature_length;
    wc_Sha256 sha;
    bool verified;

    if (!send_packet(connection, client_kexinit, kexinit_length)) {
        return false;
    }
    while (connection->server_kexinit_length == 0u && !connection->disconnect_seen &&
           !connection->closed) {
        pump_once(connection, WAIT_MS);
    }
    if (connection->server_kexinit_length == 0u) {
        return false;
    }
    (void)wc_curve25519_init(&mine);
    (void)wc_curve25519_make_key(&rng, CURVE25519_KEYSIZE, &mine);
    (void)wc_curve25519_export_public_ex(&mine, public_key, &public_length, EC25519_LITTLE_ENDIAN);
    message[0] = MSG_KEX_ECDH_INIT;
    if (!send_packet(connection, message,
                     1u + put_bytes(message + 1, public_key, sizeof(public_key)))) {
        wc_curve25519_free(&mine);
        return false;
    }
    while (connection->kex_reply_length == 0u && !connection->closed) {
        pump_once(connection, WAIT_MS);
    }
    if (connection->kex_reply_length == 0u) {
        wc_curve25519_free(&mine);
        return false;
    }
    host_length = sys_get_be32(connection->kex_reply + 1);
    host_blob = connection->kex_reply + 5;
    server_public_length = sys_get_be32(host_blob + host_length);
    server_public = host_blob + host_length + 4;
    signature_length = sys_get_be32(server_public + server_public_length);
    signature = server_public + server_public_length + 4;

    (void)wc_curve25519_init(&theirs);
    if (server_public_length != CURVE25519_KEYSIZE ||
        wc_curve25519_import_public_ex(server_public, server_public_length, &theirs,
                                       EC25519_LITTLE_ENDIAN) != 0 ||
        wc_curve25519_shared_secret_ex(&mine, &theirs, secret, &secret_length,
                                       EC25519_LITTLE_ENDIAN) != 0) {
        wc_curve25519_free(&mine);
        wc_curve25519_free(&theirs);
        return false;
    }
    wc_curve25519_free(&mine);
    wc_curve25519_free(&theirs);
    k_length = put_mpint_of_bytes(k_mpint, secret, secret_length);

    (void)wc_InitSha256(&sha);
#define HASH_STRING(data, length)                                                                  \
    do {                                                                                           \
        sys_put_be32((uint32_t)(length), scratch);                                                 \
        (void)wc_Sha256Update(&sha, scratch, 4);                                                   \
        (void)wc_Sha256Update(&sha, (const uint8_t *)(data), (word32)(length));                    \
    } while (0)
    HASH_STRING(client_version, strlen(client_version));
    HASH_STRING(server_version, strlen(server_version));
    HASH_STRING(client_kexinit, kexinit_length);
    HASH_STRING(connection->server_kexinit, connection->server_kexinit_length);
    HASH_STRING(host_blob, host_length);
    HASH_STRING(public_key, sizeof(public_key));
    HASH_STRING(server_public, server_public_length);
#undef HASH_STRING
    (void)wc_Sha256Update(&sha, k_mpint, (word32)k_length);
    (void)wc_Sha256Final(&sha, hash);
    wc_Sha256Free(&sha);

    verified = verify_host_key(host_blob, host_length, signature, signature_length, hash);
    if (!verified) {
        printf("client: host key signature did not verify\n");
        return false;
    }
    message[0] = MSG_NEWKEYS;
    if (!send_packet(connection, message, 1u)) {
        return false;
    }
    while (!connection->newkeys && !connection->closed) {
        pump_once(connection, WAIT_MS);
    }
    if (!connection->newkeys || !set_keys(connection, k_mpint, k_length, hash)) {
        return false;
    }
    /* Strict key exchange starts both counts again here. */
    connection->send_sequence = 0u;
    connection->receive_sequence = 0u;
    connection->keyed = true;
    return true;
}

/* Reads the server's version line, without the line end, into line. */
static bool read_version(connection_t *connection, char *line, size_t capacity)
{
    size_t at = 0;

    while (at + 1u < capacity) {
        uint8_t byte;

        if (read_exact(connection, &byte, 1, WAIT_MS) <= 0) {
            return false;
        }
        if (byte == '\n') {
            if (at > 0u && line[at - 1u] == '\r') {
                --at;
            }
            line[at] = '\0';
            return strncmp(line, "SSH-2.0-", 8) == 0;
        }
        line[at++] = (char)byte;
    }
    return false;
}

/* ---- The client interface ---- */

static connection_t *connection_at(int id)
{
    return id >= 0 && id < CONNECTIONS_MAX && connections[id].used ? &connections[id] : NULL;
}

static connection_t *open_connection(int id)
{
    connection_t *connection = connection_at(id);

    return connection != NULL && !connection->closed ? connection : NULL;
}

static bool client_connect(const crux_ssh_peer_t *peer, int *id)
{
    static const char client_version[] = "SSH-2.0-CruxConformance";
    struct net_sockaddr_in from = {.sin_family = NET_AF_INET};
    struct net_sockaddr_in to = {.sin_family = NET_AF_INET, .sin_port = net_htons(SERVER_PORT)};
    struct net_sockaddr_in local;
    net_socklen_t local_length = sizeof(local);
    char server_version[64];
    connection_t *connection = NULL;
    int index;
    uint8_t message[32];

    for (index = 0; index < CONNECTIONS_MAX; ++index) {
        if (!connections[index].used) {
            connection = &connections[index];
            break;
        }
    }
    if (connection == NULL) {
        printf("client: out of connection records\n");
        return false;
    }
    memset(connection, 0, sizeof(*connection));
    connection->socket = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    if (connection->socket < 0) {
        return false;
    }
    memcpy(&from.sin_addr, peer->address, 4);
    to.sin_addr.s4_addr[0] = 127;
    to.sin_addr.s4_addr[3] = 1;
    if (zsock_bind(connection->socket, (struct net_sockaddr *)&from, sizeof(from)) != 0 ||
        zsock_connect(connection->socket, (struct net_sockaddr *)&to, sizeof(to)) != 0 ||
        zsock_getsockname(connection->socket, (struct net_sockaddr *)&local, &local_length) != 0) {
        (void)zsock_close(connection->socket);
        return false;
    }
    connection->local_port = net_ntohs(local.sin_port);
    connection->used = true;
    if (!write_all(connection, (const uint8_t *)client_version, sizeof(client_version) - 1u) ||
        !write_all(connection, (const uint8_t *)"\r\n", 2u) ||
        !read_version(connection, server_version, sizeof(server_version)) ||
        !exchange_keys(connection, server_version, client_version)) {
        wait_closed(connection, SETTLE_MS);
        mark_closed(connection);
        connection->used = false;
        return false;
    }
    message[0] = MSG_SERVICE_REQUEST;
    if (!send_packet(connection, message, 1u + put_text(message + 1, "ssh-userauth")) ||
        wait_answer(connection, &connection->service) != ANSWER_YES) {
        mark_closed(connection);
        connection->used = false;
        return false;
    }
    *id = index;
    return true;
}

static bool auth_exchange(connection_t *connection, const uint8_t *message, size_t length)
{
    connection->auth = ANSWER_NONE;
    if (!send_packet(connection, message, length)) {
        return false;
    }
    return wait_answer(connection, &connection->auth) == ANSWER_YES;
}

static size_t put_auth_head(uint8_t *out, const char *user, size_t user_length, const char *method)
{
    size_t at = 0;

    out[at++] = MSG_USERAUTH_REQUEST;
    at += put_bytes(out + at, user, user_length);
    at += put_text(out + at, "ssh-connection");
    at += put_text(out + at, method);
    return at;
}

static bool client_password(int id, const char *user, size_t user_length, const char *password)
{
    connection_t *connection = open_connection(id);
    uint8_t message[256];
    size_t at;

    if (connection == NULL) {
        return false;
    }
    at = put_auth_head(message, user, user_length, "password");
    message[at++] = 0u;
    at += put_text(message + at, password);
    return auth_exchange(connection, message, at);
}

static bool client_none_request(int id, const char *user)
{
    connection_t *connection = open_connection(id);
    uint8_t message[128];

    if (connection == NULL) {
        return false;
    }
    return auth_exchange(connection, message, put_auth_head(message, user, strlen(user), "none"));
}

static bool client_key_probe(int id, const char *user, const char *algorithm, const uint8_t *key,
                             size_t key_length)
{
    connection_t *connection = open_connection(id);
    uint8_t message[384];
    size_t at;

    if (connection == NULL) {
        return false;
    }
    at = put_auth_head(message, user, strlen(user), "publickey");
    message[at++] = 0u;
    at += put_text(message + at, algorithm);
    at += put_bytes(message + at, key, key_length);
    return auth_exchange(connection, message, at);
}

static bool client_key_login(int id, const char *user, const char *algorithm, const uint8_t *key,
                             size_t key_length, bool good_signature)
{
    connection_t *connection = open_connection(id);
    const test_key_t *signer = key == ed25519_user.blob ? &ed25519_user : &ecdsa_user;
    static uint8_t signed_data[512];
    static uint8_t message[768];
    uint8_t signature[160];
    size_t signature_length = 0;
    size_t head;
    size_t at;

    if (connection == NULL) {
        return false;
    }
    /* The message, as signed: the session identifier in front, and the signature left off. */
    head = put_bytes(signed_data, connection->session_id, sizeof(connection->session_id));
    at = put_auth_head(signed_data + head, user, strlen(user), "publickey");
    signed_data[head + at] = 1u;
    ++at;
    at += put_text(signed_data + head + at, algorithm);
    at += put_bytes(signed_data + head + at, key, key_length);
    if (!test_key_sign(signer,
                       signer->type == CRUX_SSH_KEY_ED25519 ? "ssh-ed25519" : "ecdsa-sha2-nistp256",
                       signed_data, head + at, good_signature, signature, sizeof(signature),
                       &signature_length)) {
        printf("client: could not sign\n");
        return false;
    }
    memcpy(message, signed_data + head, at);
    at += put_bytes(message + at, signature, signature_length);
    return auth_exchange(connection, message, at);
}

static bool client_is_connected(int id)
{
    connection_t *connection = connection_at(id);

    if (connection == NULL || connection->closed) {
        return false;
    }
    pump_once(connection, SETTLE_MS);
    /* A disconnect is on its way to the close, and everything owed runs before the close. */
    if (connection->disconnect_seen) {
        wait_closed(connection, WAIT_MS);
    }
    return !connection->closed;
}

static bool client_open_channel(int id, const char *type)
{
    connection_t *connection = open_connection(id);
    uint8_t message[96];
    size_t at = 0;

    if (connection == NULL) {
        return false;
    }
    connection->open = ANSWER_NONE;
    message[at++] = MSG_CHANNEL_OPEN;
    at += put_text(message + at, type);
    at += put_u32(message + at, 0u);
    at += put_u32(message + at, 0u); /* No window yet: see the note at the top. */
    at += put_u32(message + at, 16384u);
    if (!send_packet(connection, message, at)) {
        return false;
    }
    return wait_answer(connection, &connection->open) == ANSWER_YES;
}

static void grant_window(connection_t *connection)
{
    const size_t bytes = connection->window_limited ? connection->window : UNLIMITED_WINDOW;
    uint8_t message[9];

    if (bytes > 0u) {
        message[0] = MSG_CHANNEL_WINDOW_ADJUST;
        (void)put_u32(message + 1, connection->remote_channel);
        (void)put_u32(message + 5, (uint32_t)bytes);
        (void)send_packet(connection, message, sizeof(message));
    }
}

static size_t put_request_data(uint8_t *out, const char *type, const char *data, size_t data_length)
{
    size_t at = 0;

    if (strcmp(type, "pty-req") == 0) {
        at += put_text(out + at, "xterm");
        at += put_u32(out + at, 80u);
        at += put_u32(out + at, 24u);
        at += put_u32(out + at, 0u);
        at += put_u32(out + at, 0u);
        at += put_text(out + at, "");
    } else if (strcmp(type, "window-change") == 0) {
        at += put_u32(out + at, 100u);
        at += put_u32(out + at, 40u);
        at += put_u32(out + at, 0u);
        at += put_u32(out + at, 0u);
    } else if (data != NULL) {
        at += put_bytes(out + at, data, data_length);
    }
    return at;
}

static bool send_channel_data(connection_t *connection, const void *data, size_t length)
{
    static uint8_t message[OUTPUT_MAX + 16u];
    size_t at = 0;

    if (length > OUTPUT_MAX) {
        return false;
    }
    message[at++] = MSG_CHANNEL_DATA;
    at += put_u32(message + at, connection->remote_channel);
    at += put_bytes(message + at, data, length);
    return send_packet(connection, message, at);
}

static void send_channel_eof(connection_t *connection)
{
    uint8_t message[5];

    message[0] = MSG_CHANNEL_EOF;
    (void)put_u32(message + 1, connection->remote_channel);
    (void)send_packet(connection, message, sizeof(message));
}

static void send_held_input(connection_t *connection)
{
    if (connection->held_length > 0u) {
        (void)send_channel_data(connection, connection->held, connection->held_length);
        connection->held_length = 0u;
    }
    if (connection->held_eof) {
        send_channel_eof(connection);
        connection->held_eof = false;
    }
}

static bool client_channel_request(int id, const char *type, const char *data, size_t data_length)
{
    connection_t *connection = open_connection(id);
    const bool shell_or_exec = strcmp(type, "shell") == 0 || strcmp(type, "exec") == 0;
    const bool sftp = strcmp(type, "subsystem") == 0 && data != NULL && data_length == 4u &&
                      memcmp(data, "sftp", 4u) == 0;
    uint8_t message[256];
    size_t at = 0;

    if (connection == NULL || !connection->has_channel || connection->channel_closed) {
        return false;
    }
    if ((shell_or_exec || sftp) && !connection->session_granted) {
        grant_window(connection);
    }
    connection->request = ANSWER_NONE;
    message[at++] = MSG_CHANNEL_REQUEST;
    at += put_u32(message + at, connection->remote_channel);
    at += put_text(message + at, type);
    message[at++] = 1u;
    at += put_request_data(message + at, type, data, data_length);
    if (!send_packet(connection, message, at) ||
        wait_answer(connection, &connection->request) != ANSWER_YES) {
        return false;
    }
    if (shell_or_exec || sftp) {
        connection->session_granted = true;
        send_held_input(connection);
    }
    if (shell_or_exec) {
        /* The session runs on its worker, and the connection ends with it. */
        wait_closed(connection, 10000);
    } else if (sftp) {
        connection->sftp = true;
        pump_once(connection, 1);
        k_msleep(SETTLE_MS / 2); /* The worker opens SFTP once the request is granted. */
    }
    return true;
}

static bool client_global_request(int id, const char *type)
{
    connection_t *connection = open_connection(id);
    uint8_t message[96];
    size_t at = 0;

    if (connection == NULL) {
        return false;
    }
    connection->global = ANSWER_NONE;
    message[at++] = MSG_GLOBAL_REQUEST;
    at += put_text(message + at, type);
    message[at++] = 1u;
    /* An address and a port, which a forward request needs and every other request ignores. */
    at += put_text(message + at, "127.0.0.1");
    at += put_u32(message + at, 2022u);
    if (!send_packet(connection, message, at)) {
        return false;
    }
    return wait_answer(connection, &connection->global) == ANSWER_YES;
}

static bool client_send(int id, const void *data, size_t length)
{
    connection_t *connection = open_connection(id);

    if (connection == NULL || !connection->has_channel) {
        return false;
    }
    if (connection->session_granted) {
        return send_channel_data(connection, data, length);
    }
    if (length > sizeof(connection->held) - connection->held_length) {
        return false;
    }
    memcpy(connection->held + connection->held_length, data, length);
    connection->held_length += length;
    return true;
}

static void client_send_eof(int id)
{
    connection_t *connection = open_connection(id);

    if (connection != NULL && connection->has_channel) {
        if (connection->session_granted) {
            send_channel_eof(connection);
        } else {
            connection->held_eof = true;
        }
    }
}

static void client_set_window(int id, size_t bytes)
{
    connection_t *connection = connection_at(id);

    if (connection != NULL) {
        connection->window_limited = true;
        connection->window = bytes;
    }
}

static void client_drop_on_write(int id)
{
    connection_t *connection = open_connection(id);

    if (connection != NULL) {
        crux_ssh_wolf_test_drop_on_write(connection->local_port);
    }
}

static size_t client_received(int id, void *buffer, size_t size)
{
    connection_t *connection = connection_at(id);
    size_t length;

    if (connection == NULL) {
        return 0u;
    }
    if (!connection->closed) {
        pump_once(connection, SETTLE_MS);
    }
    length = MIN(connection->output_length, size);
    memcpy(buffer, connection->output, length);
    return length;
}

static bool client_received_eof(int id)
{
    connection_t *connection = connection_at(id);

    return connection != NULL && connection->output_eof;
}

static bool client_exit_status(int id, int *status)
{
    connection_t *connection = connection_at(id);

    if (connection == NULL || !connection->has_exit_status) {
        return false;
    }
    *status = connection->exit_status;
    return true;
}

static crux_ssh_file_handle_t *client_file_handle(int id)
{
    connection_t *connection = open_connection(id);

    return connection != NULL ? crux_ssh_wolf_test_file_handle(connection->local_port) : NULL;
}

static void client_end_sftp(int id)
{
    connection_t *connection = open_connection(id);

    if (connection != NULL && connection->has_channel) {
        connection->channel_closed = true;
        send_channel_close(connection);
        wait_closed(connection, WAIT_MS);
    }
}

static void client_disconnect(int id)
{
    connection_t *connection = open_connection(id);

    if (connection != NULL) {
        mark_closed(connection);
        k_msleep(DISCONNECT_SETTLE_MS);
    }
}

static void client_pass_auth_time(int id)
{
    ARG_UNUSED(id);
    k_msleep(CASE_AUTH_LIMIT_MS);
}

/* A request that any server answers, so that this connection is not waiting. */
static void send_sftp_request(connection_t *connection)
{
    uint8_t request[13] = {0};
    size_t at;

    if (!connection->sftp_init_sent) {
        connection->sftp_init_sent = true;
        (void)put_u32(request, 5u);
        request[4] = 1u; /* SSH_FXP_INIT */
        (void)put_u32(request + 5, 3u);
        at = 9u;
    } else {
        /* An unknown type, with a body because wolfSSH ends the session on a request without one.
         */
        (void)put_u32(request, 9u);
        request[4] = 200u;
        (void)put_u32(request + 5, 1u);
        at = 13u;
    }
    (void)client_send((int)(connection - connections), request, at);
}

static void client_pass_read_timeout(int id)
{
    int64_t end = k_uptime_get() + CASE_READ_TIMEOUT_MS;

    while (k_uptime_get() < end) {
        for (int other = 0; other < CONNECTIONS_MAX; ++other) {
            if (other != id && connections[other].used && !connections[other].closed &&
                connections[other].sftp) {
                send_sftp_request(&connections[other]);
                pump_once(&connections[other], 1);
            }
        }
        k_msleep(MIN(BUSY_PERIOD_MS, MAX(end - k_uptime_get(), 1)));
    }
    if (connection_at(id) != NULL && !connections[id].closed) {
        pump_once(&connections[id], 1);
    }
}

/* ---- Running the cases ---- */

static void reset_all(void)
{
    for (int id = 0; id < CONNECTIONS_MAX; ++id) {
        if (connections[id].used) {
            mark_closed(&connections[id]);
        }
        connections[id].used = false;
    }
    (void)crux_ssh_wolf_provider()->stop();
}

static bool prepare_client(void)
{
    if (wc_InitRng(&rng) != 0 || !test_key_generate(CRUX_SSH_KEY_ED25519, &host_key) ||
        !test_key_generate(CRUX_SSH_KEY_ED25519, &ed25519_user) ||
        !test_key_generate(CRUX_SSH_KEY_ECDSA_P256, &ecdsa_user)) {
        return false;
    }
    client = (crux_ssh_test_client_t){
        .provider = crux_ssh_wolf_provider(),
        .connection_capacity = CRUX_SSH_WOLF_CONNECTIONS_MAX,
        .host_key_type = CRUX_SSH_KEY_ED25519,
        .host_key = host_key.host_key,
        .host_key_length = host_key.host_key_length,
        .ed25519_key = ed25519_user.blob,
        .ed25519_key_length = ed25519_user.blob_length,
        .ecdsa_key = ecdsa_user.blob,
        .ecdsa_key_length = ecdsa_user.blob_length,
        .peer_a = {.address = {127, 0, 0, 1}, .address_length = 4},
        .peer_b = {.address = {127, 0, 0, 2}, .address_length = 4},
        .connect = client_connect,
        .password = client_password,
        .none_request = client_none_request,
        .key_probe = client_key_probe,
        .key_login = client_key_login,
        .is_connected = client_is_connected,
        .pass_auth_time = client_pass_auth_time,
        .pass_read_timeout = client_pass_read_timeout,
        .open_channel = client_open_channel,
        .channel_request = client_channel_request,
        .global_request = client_global_request,
        .send = client_send,
        .send_eof = client_send_eof,
        .set_window = client_set_window,
        .drop_on_write = client_drop_on_write,
        .received = client_received,
        .received_eof = client_received_eof,
        .exit_status = client_exit_status,
        .file_handle = client_file_handle,
        .end_sftp = client_end_sftp,
        .disconnect = client_disconnect,
    };
    return true;
}

int test_conformance_run(const char *only)
{
    unsigned failed = 0;
    unsigned ran = 0;

    if (!prepare_client()) {
        printf("client: could not generate keys\n");
        return 2;
    }
    for (size_t i = 0; i < crux_ssh_conformance_case_count; ++i) {
        const crux_ssh_conformance_case_t *test = &crux_ssh_conformance_cases[i];
        unsigned failures;

        if (only != NULL && strcmp(only, test->name) != 0) {
            continue;
        }
        failures = crux_ssh_conformance_run_case(test, &client, reset_all, true);
        printf("%-48s %s\n", test->name, failures == 0u ? "ok" : "FAILED");
        failed += failures != 0u;
        ++ran;
    }
    reset_all();
    test_print_measurements();
    printf("%u cases, %u failed\n", ran, failed);
    return failed == 0u ? 0 : 1;
}
