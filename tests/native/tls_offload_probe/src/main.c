/*
 * A TLS server socket over the native socket offload, for tls_probe_client.py to drive from the
 * host. It proves the socket only: the HTTPS listener and its conformance stay with Issue #185.
 *
 * One thread serves every connection, as Zephyr's HTTP server does, and one poll set holds an
 * eventfd, the listeners and the clients, which is what that server would ask of this socket.
 * Everything a client must not depend on is printed as a PROBE line, for the driver to record.
 */
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zvfs/eventfd.h>

#include <errno.h>
#include <psa/crypto.h>
#include <string.h>

#include "test_tls_credentials.h"

#define PROBE_SEC_TAG 1
#define PROBE_LISTENERS 2
#define PROBE_CLIENTS 8
#define PROBE_BUFFER 2048

/* The poll set: the eventfd, then the listeners, then the clients. */
enum { SLOT_EVENT = 0, SLOT_LISTENER = 1, SLOT_CLIENT = 1 + PROBE_LISTENERS };

static struct zsock_pollfd slots[SLOT_CLIENT + PROBE_CLIENTS];
static const char *const listener_names[PROBE_LISTENERS] = {"tls1.2", "tls1.3"};
static uint8_t buffer[PROBE_BUFFER];
static uint32_t accepted, accept_failed, closed_clean, closed_error;
static uint32_t bytes_echoed, empty_polls;
/* See the main loop. The driver turns it off to show what a server without it sees. */
static bool sweep_on_empty_poll = true;
#define PROBE_NO_SWEEP "@probe-no-sweep"

static void event_writer(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    zvfs_eventfd_write(slots[SLOT_EVENT].fd, 1);
}
K_TIMER_DEFINE(event_timer, event_writer, NULL);

static int open_listener(int protocol, uint16_t port)
{
    static const sec_tag_t tags[] = {PROBE_SEC_TAG};
    static const int reuse = 1;
    struct net_sockaddr_in address = {
        .sin_family = NET_AF_INET,
        .sin_port = net_htons(port),
    };
    int fd = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, protocol);

    if (fd < 0) {
        return -errno;
    }
    zsock_inet_pton(NET_AF_INET, "127.0.0.1", &address.sin_addr);
    if (zsock_setsockopt(fd, ZSOCK_SOL_SOCKET, ZSOCK_SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        zsock_setsockopt(fd, ZSOCK_SOL_TLS, ZSOCK_TLS_SEC_TAG_LIST, tags, sizeof(tags)) < 0 ||
        zsock_bind(fd, (struct net_sockaddr *)&address, sizeof(address)) < 0 ||
        zsock_listen(fd, PROBE_CLIENTS) < 0) {
        int error = -errno;

        zsock_close(fd);
        return error;
    }
    return fd;
}

static void close_client(struct zsock_pollfd *slot, bool clean)
{
    zsock_close(slot->fd);
    slot->fd = -1;
    if (clean) {
        ++closed_clean;
    } else {
        ++closed_error;
    }
    printk("PROBE close clean=%d closed_clean=%u closed_error=%u echoed=%u\n", clean, closed_clean,
           closed_error, bytes_echoed);
}

/* A handshake happens inside accept, so a client that says nothing holds this one thread. */
static void accept_client(int slot)
{
    short revents = slots[slot].revents;
    int64_t started = k_uptime_get();
    int fd = zsock_accept(slots[slot].fd, NULL, NULL);
    int64_t elapsed = k_uptime_get() - started;

    if (fd < 0) {
        ++accept_failed;
        printk("PROBE accept listener=%s errno=%d revents=0x%x sim_ms=%d\n",
               listener_names[slot - SLOT_LISTENER], errno, (unsigned int)revents, (int)elapsed);
        return;
    }
    for (int i = SLOT_CLIENT; i < (int)ARRAY_SIZE(slots); ++i) {
        if (slots[i].fd < 0) {
            slots[i].fd = fd;
            slots[i].events = ZSOCK_POLLIN;
            ++accepted;
            printk("PROBE accept listener=%s ok revents=0x%x sim_ms=%d accepted=%u\n",
                   listener_names[slot - SLOT_LISTENER], (unsigned int)revents, (int)elapsed,
                   accepted);
            return;
        }
    }
    ++accept_failed;
    printk("PROBE accept listener=%s no_slot\n", listener_names[slot - SLOT_LISTENER]);
    zsock_close(fd);
}

static bool send_all(int fd, const uint8_t *data, size_t length)
{
    while (length > 0) {
        ssize_t sent = zsock_send(fd, data, length, 0);

        if (sent < 0) {
            if (errno == EAGAIN) {
                k_msleep(1);
                continue;
            }
            printk("PROBE send errno=%d\n", errno);
            return false;
        }
        data += sent;
        length -= sent;
    }
    return true;
}

static void serve_client(struct zsock_pollfd *slot)
{
    short revents = slot->revents;
    ssize_t received = zsock_recv(slot->fd, buffer, sizeof(buffer), ZSOCK_MSG_DONTWAIT);

    if (received == 0) {
        close_client(slot, true);
    } else if (received < 0) {
        /* Part of a record, or a ticket. Only a hangup with nothing to read ends the client. */
        if (errno != EAGAIN || (revents & (ZSOCK_POLLHUP | ZSOCK_POLLERR)) != 0) {
            printk("PROBE recv errno=%d revents=0x%x\n", errno, (unsigned int)revents);
            close_client(slot, false);
        }
    } else if (send_all(slot->fd, buffer, received)) {
        bytes_echoed += received;
        if (received == sizeof(PROBE_NO_SWEEP) - 1 &&
            memcmp(buffer, PROBE_NO_SWEEP, sizeof(PROBE_NO_SWEEP) - 1) == 0) {
            sweep_on_empty_poll = false;
            printk("PROBE sweep off\n");
        }
    } else {
        close_client(slot, false);
    }
}

/* The PSA provider beside the TLS backend in one image. The test vector is SHA-256 of "abc". */
static void check_psa(void)
{
    static const uint8_t expected[4] = {0xba, 0x78, 0x16, 0xbf};
    uint8_t digest[32];
    uint8_t random[16] = {0};
    size_t length = 0;
    psa_status_t init = psa_crypto_init();
    psa_status_t random_status = psa_generate_random(random, sizeof(random));
    psa_status_t hash = psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)"abc", 3, digest,
                                         sizeof(digest), &length);
    bool known = hash == PSA_SUCCESS && length == sizeof(digest) &&
                 memcmp(digest, expected, sizeof(expected)) == 0;

    printk("PROBE psa provider=%s init=%d random=%d sha256=%d known_answer=%d\n",
           IS_ENABLED(CONFIG_WOLFPSA) ? "wolfpsa" : "tf-psa-crypto", (int)init, (int)random_status,
           (int)hash, known);
}

int main(void)
{
    int result;
    bool event_seen = false;

    printk("PROBE start tls_backend=%s\n", IS_ENABLED(CONFIG_WOLFSSL) ? "wolfssl" : "mbedtls");
    check_psa();
    result = tls_credential_add(PROBE_SEC_TAG, TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
                                crux_test_tls_certificate, sizeof(crux_test_tls_certificate));
    printk("PROBE credential certificate result=%d\n", result);
    result = tls_credential_add(PROBE_SEC_TAG, TLS_CREDENTIAL_PRIVATE_KEY,
                                crux_test_tls_private_key, sizeof(crux_test_tls_private_key));
    printk("PROBE credential key result=%d\n", result);

    for (int i = 0; i < (int)ARRAY_SIZE(slots); ++i) {
        slots[i].fd = -1;
        slots[i].events = ZSOCK_POLLIN;
    }
    slots[SLOT_EVENT].fd = zvfs_eventfd(0, ZVFS_EFD_NONBLOCK);
    printk("PROBE eventfd fd=%d\n", slots[SLOT_EVENT].fd);

    /* A listener that cannot open is recorded: a profile may lack a TLS version. */
    static const int protocols[PROBE_LISTENERS] = {NET_IPPROTO_TLS_1_2, NET_IPPROTO_TLS_1_3};
    static const uint16_t ports[PROBE_LISTENERS] = {CONFIG_CRUX_TLS_PROBE_PORT_TLS12,
                                                    CONFIG_CRUX_TLS_PROBE_PORT_TLS13};
    for (int i = 0; i < PROBE_LISTENERS; ++i) {
        int fd = open_listener(protocols[i], ports[i]);

        printk("PROBE listener name=%s port=%u result=%d\n", listener_names[i], ports[i], fd);
        slots[SLOT_LISTENER + i].fd = fd < 0 ? -1 : fd;
    }
    k_timer_start(&event_timer, K_MSEC(200), K_MSEC(200));
    printk("PROBE ready\n");

    while (true) {
        int ready = zsock_poll(slots, ARRAY_SIZE(slots), -1);

        if (ready < 0) {
            printk("PROBE poll errno=%d\n", errno);
            k_msleep(10);
            continue;
        }
        /*
         * A poll with no timeout returned nothing. The offload reports a peer's FIN as POLLIN
         * alone, as the host does, and the TLS layer takes POLLIN with no record as "try again", so
         * it asks the poll to retry and then returns 0, every time, for as long as this client
         * stays. Only a read finds the end of the stream. The Mbed profile's server would spin
         * without it.
         */
        if (ready == 0) {
            if (++empty_polls == 1) {
                printk("PROBE poll returned 0 with no timeout\n");
            }
            if (sweep_on_empty_poll) {
                for (int i = SLOT_CLIENT; i < (int)ARRAY_SIZE(slots); ++i) {
                    if (slots[i].fd >= 0) {
                        serve_client(&slots[i]);
                    }
                }
            }
            continue;
        }
        if (slots[SLOT_EVENT].revents & ZSOCK_POLLIN) {
            zvfs_eventfd_t value;

            (void)zvfs_eventfd_read(slots[SLOT_EVENT].fd, &value);
            if (!event_seen) {
                event_seen = true;
                printk("PROBE eventfd wakes the poll set\n");
            }
        }
        for (int i = 0; i < PROBE_LISTENERS; ++i) {
            if (slots[SLOT_LISTENER + i].fd >= 0 && slots[SLOT_LISTENER + i].revents != 0) {
                accept_client(SLOT_LISTENER + i);
            }
        }
        for (int i = SLOT_CLIENT; i < (int)ARRAY_SIZE(slots); ++i) {
            if (slots[i].fd >= 0 && slots[i].revents != 0) {
                serve_client(&slots[i]);
            }
        }
    }
    return 0;
}
