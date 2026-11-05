/*
 * The wolfSSH SFTP server on Linux, over the real port and the real file service, for OpenSSH to
 * talk to. It stands in for the provider of #192, with no claim about what that provider does:
 * every key is accepted, and the user name picks an account of a fake Access core. See
 * CMakeLists.txt.
 *
 * Usage: proof_server <port> <host key DER> <directory> <idle limit in seconds>
 * The roots data, logs, inbox and web must exist in the directory.
 */
#include "crux_ssh_attachment.h"
#include "crux_ssh_callbacks.h"
#include "fake_ports.h"
#include "file_linux.h"
#include "file_service_port.h"

#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>

#include <arpa/inet.h>
#include <crux_access_adapter.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define ACCOUNT_PASSWORD "correct horse battery"
#define POLL_MS 100
#define SESSIONS_MAX 8

enum { ROOT_DATA, ROOT_LOGS, ROOT_INBOX, ROOT_WEB };

static const crux_file_area_t areas[] = {
    {"data", ROOT_DATA, "data.read", "data.write", 1024u * 1024u, 3u * 1024u * 1024u, 16u,
     ".filepart", CRUX_FILE_SERVICE_DIRECT},
    {"logs", ROOT_LOGS, "logs.read", NULL, 1024u * 1024u, 3u * 1024u * 1024u, 16u, NULL, 0},
    {"inbox", ROOT_INBOX, "inbox.read", "inbox.write", 1024u * 1024u, 3u * 1024u * 1024u, 16u, NULL,
     CRUX_FILE_SERVICE_DIRECT},
    {"web", ROOT_WEB, "data.read", "data.write", 1024u * 1024u, 3u * 1024u * 1024u, 16u, NULL,
     CRUX_FILE_SERVICE_MANAGED},
};
static const crux_file_service_config_t service_config = {areas, 4, NULL, 0, true};

static const char *const root_permissions[] = {CRUX_ACCESS_PERMISSION_MANAGE, "data.read",
                                               "data.write", "logs.read"};
static const char *const viewer_permissions[] = {"data.read", "logs.read"};
static const char *const dropper_permissions[] = {"inbox.write"};
static const char *const stranger_permissions[] = {"status.read"};
static const char *const auditor_permissions[] = {"data.read", "logs.read", "inbox.read"};
static const crux_access_role_t roles[] = {
    {"administrator", root_permissions, 4},
    {"viewer", viewer_permissions, 2},
    {"dropper", dropper_permissions, 1},
    {"stranger", stranger_permissions, 1},
    {"auditor", auditor_permissions, 3},
};
static crux_access_policy_t policy = {roles, 5, 0, 3600u, NULL};

static const crux_access_origin_t origin_serial = {CRUX_ACCESS_CHANNEL_SERIAL, {0}, 0};
static const crux_access_origin_t origin_ssh = {CRUX_ACCESS_CHANNEL_SSH, {192, 0, 2, 1}, 4};

/* The file service reports here. The driver reads these lines, to see that a rejected name was
 * recorded and that its text was not. */
void crux_file_service_port_event(crux_file_service_event_t event, crux_access_session_t session,
                                  const char *area, const char *name, const char *new_name,
                                  bool storage_limit)
{
    (void)session;
    printf("event %d area=%s name=%s new_name=%s storage_limit=%d\n", (int)event, area,
           name != NULL ? name : "-", new_name != NULL ? new_name : "-", storage_limit ? 1 : 0);
    fflush(stdout);
}

struct crux_ssh_attachment {
    crux_access_session_t session;
};

crux_access_session_t crux_ssh_attachment_session(const crux_ssh_attachment_t *attachment)
{
    crux_access_session_t none = {0};
    return attachment != NULL ? attachment->session : none;
}

static int on_user_auth(byte type, WS_UserAuthData *data, void *context)
{
    (void)data;
    (void)context;
    return type == WOLFSSH_USERAUTH_PUBLICKEY ? WOLFSSH_USERAUTH_SUCCESS : WOLFSSH_USERAUTH_FAILURE;
}

static int on_recv(WOLFSSH *ssh, void *buffer, word32 size, void *context)
{
    (void)context;
    ssize_t got = recv(wolfSSH_get_fd(ssh), buffer, size, MSG_DONTWAIT);
    if (got > 0) {
        return (int)got;
    }
    if (got == 0) {
        return WS_CBIO_ERR_CONN_CLOSE;
    }
    return errno == EAGAIN || errno == EWOULDBLOCK ? WS_CBIO_ERR_WANT_READ : WS_CBIO_ERR_GENERAL;
}

static int on_send(WOLFSSH *ssh, void *buffer, word32 size, void *context)
{
    (void)context;
    ssize_t sent = send(wolfSSH_get_fd(ssh), buffer, size, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent >= 0) {
        return (int)sent;
    }
    return errno == EAGAIN || errno == EWOULDBLOCK ? WS_CBIO_ERR_WANT_WRITE : WS_CBIO_ERR_GENERAL;
}

static int wait_socket(int fd, short events, int timeout_ms)
{
    struct pollfd entry = {.fd = fd, .events = events};
    int ready = poll(&entry, 1, timeout_ms);
    return ready < 0 && errno == EINTR ? 0 : ready;
}

static bool session_alive(crux_access_session_t session)
{
    char user[CRUX_ACCESS_NAME_SIZE];
    return crux_access_session_user(session, user, sizeof(user)) == CRUX_ACCESS_OK;
}

static void serve_sftp(WOLFSSH *ssh, crux_access_session_t session)
{
    int fd = wolfSSH_get_fd(ssh);
    int error = 0;
    int result = WS_SUCCESS;

    for (;;) {
        byte peek[1];

        if (wolfSSH_SFTP_PendingSend(ssh) || wolfSSH_stream_peek(ssh, peek, sizeof(peek)) > 0) {
            result = wolfSSH_SFTP_read(ssh);
            error = wolfSSH_get_error(ssh);
            if (result >= 0 || error == WS_WANT_READ || error == WS_WANT_WRITE ||
                error == WS_CHAN_RXD || error == WS_WINDOW_FULL || error == WS_REKEYING) {
                if (error == WS_WANT_WRITE || error == WS_WINDOW_FULL) {
                    (void)wait_socket(fd, error == WS_WANT_WRITE ? POLLOUT : POLLIN, POLL_MS);
                    if (error == WS_WINDOW_FULL) {
                        (void)wolfSSH_worker(ssh, NULL);
                    }
                }
                if (result >= 0 || error != WS_WANT_READ) {
                    continue;
                }
            } else {
                printf("sftp read ended: %d %s\n", result, wolfSSH_ErrorToName(error));
                fflush(stdout);
                return;
            }
        }

        int ready = wait_socket(fd, POLLIN, POLL_MS);
        if (ready < 0) {
            return;
        }
        if (ready == 0) {
            /* As the contract has the provider do at each read timeout. */
            if (!session_alive(session)) {
                printf("session_alive false\n");
                fflush(stdout);
                return;
            }
            continue;
        }
        result = wolfSSH_worker(ssh, NULL);
        error = wolfSSH_get_error(ssh);
        if (error == WS_EOF) {
            if (wolfSSH_stream_peek(ssh, peek, sizeof(peek)) <= 0) {
                return;
            }
        } else if (result != WS_SUCCESS && result != WS_CHAN_RXD && error != WS_WANT_READ &&
                   error != WS_WANT_WRITE && error != WS_REKEYING && error != WS_WINDOW_FULL) {
            printf("worker ended: %d %s\n", result, wolfSSH_ErrorToName(error));
            fflush(stdout);
            return;
        }
    }
}

static WOLFSSH_CTX *context;

/* wolfSSH_accept answers the SFTP INIT itself when the client asked for the subsystem, and it
 * returns WS_SFTP_COMPLETE, so the session is ready before the port has its handle. */
static bool accept_connection(WOLFSSH *ssh)
{
    for (;;) {
        int result = wolfSSH_accept(ssh);
        int error = wolfSSH_get_error(ssh);
        if (result == WS_SFTP_COMPLETE) {
            return true;
        }
        if (result == WS_SUCCESS) {
            printf("not an SFTP session\n");
            fflush(stdout);
            return false;
        }
        if (error != WS_WANT_READ && error != WS_WANT_WRITE && error != WS_AUTH_PENDING) {
            printf("handshake ended: %d %s\n", result, wolfSSH_ErrorToName(error));
            fflush(stdout);
            return false;
        }
        if (wait_socket(wolfSSH_get_fd(ssh), error == WS_WANT_WRITE ? POLLOUT : POLLIN, 1000) < 0) {
            return false;
        }
    }
}

static void *serve_connection(void *argument)
{
    int fd = (int)(intptr_t)argument;
    WOLFSSH *ssh = wolfSSH_new(context);
    struct crux_ssh_attachment attachment = {{0}};
    crux_ssh_file_handle_t *handle = NULL;

    if (ssh == NULL) {
        printf("no wolfSSH session\n");
        fflush(stdout);
        close(fd);
        return NULL;
    }
    wolfSSH_set_fd(ssh, fd);
    if (!accept_connection(ssh)) {
        goto done;
    }
    const char *user = wolfSSH_GetUsername(ssh);
    uint32_t retry_after_ms = 0;
    if (user == NULL ||
        crux_access_login_password(user, ACCOUNT_PASSWORD, strlen(ACCOUNT_PASSWORD), &origin_ssh,
                                   &attachment.session, &retry_after_ms) != CRUX_ACCESS_OK) {
        printf("no account for this user\n");
        fflush(stdout);
        goto done;
    }
    if (crux_ssh_sftp_open(NULL, &attachment, &handle) != CRUX_OK) {
        printf("no SFTP session\n");
        fflush(stdout);
        goto logout;
    }
    wolfSSH_SetFilesystemHandle(ssh, handle);
    serve_sftp(ssh, attachment.session);
    /* wolfSSH closes whatever the client left open as it frees the session, through the port. The
     * contract says that no file operation follows sftp_close, so this order is the one to use. */
    wolfSSH_free(ssh);
    ssh = NULL;
    crux_ssh_sftp_close(NULL, &attachment, handle);
logout:
    crux_access_logout(&attachment.session);
done:
    if (ssh != NULL) {
        wolfSSH_free(ssh);
    }
    close(fd);
    printf("connection closed\n");
    fflush(stdout);
    return NULL;
}

static void *clock_thread(void *argument)
{
    (void)argument;
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        access_fake_clock_set((uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u);
        usleep(50000);
    }
    return NULL;
}

static void make_accounts(void)
{
    const char *password = ACCOUNT_PASSWORD;
    size_t length = strlen(password);
    crux_access_session_t root = {0};
    uint32_t retry_after_ms = 0;
    static const char *const people[][2] = {
        {"vera", "viewer"}, {"dora", "dropper"}, {"stan", "stranger"},
                                               {"audit", "auditor"}};

    if (crux_access_provision_first_account("root", password, length, "administrator",
                                            &origin_serial) != CRUX_ACCESS_OK ||
        crux_access_login_password("root", password, length, &origin_ssh, &root, &retry_after_ms) !=
            CRUX_ACCESS_OK) {
        fprintf(stderr, "cannot make the first account\n");
        exit(2);
    }
    for (size_t i = 0; i < sizeof(people) / sizeof(people[0]); i++) {
        if (crux_access_account_create(root, people[i][0], people[i][1], password, length) !=
            CRUX_ACCESS_OK) {
            fprintf(stderr, "cannot make the account %s\n", people[i][0]);
            exit(2);
        }
    }
    crux_access_logout(&root);
}

static void bind_roots(const char *directory)
{
    static const char *const names[] = {"data", "logs", "inbox", "web"};
    char path[512];
    for (size_t i = 0; i < 4; i++) {
        snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
        if (crux_file_linux_bind_root((crux_file_root_t)i, path) != CRUX_FILE_OK) {
            fprintf(stderr, "cannot bind %s\n", path);
            exit(2);
        }
    }
}

static void load_host_key(const char *path)
{
    static byte key[4096];
    FILE *file = fopen(path, "rb");
    size_t length = file != NULL ? fread(key, 1, sizeof(key), file) : 0;
    if (file != NULL) {
        fclose(file);
    }
    if (length == 0 || wolfSSH_CTX_UsePrivateKey_buffer(context, key, (word32)length,
                                                        WOLFSSH_FORMAT_ASN1) != WS_SUCCESS) {
        fprintf(stderr, "cannot use the host key\n");
        exit(2);
    }
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <port> <host key DER> <directory> <idle limit in seconds>\n",
                argv[0]);
        return 2;
    }
    pthread_t ticker;
    access_fake_reset();
    policy.session_idle_limit_s = (uint32_t)atoi(argv[4]);
    if (pthread_create(&ticker, NULL, clock_thread, NULL) != 0 ||
        crux_access_init(&policy) != CRUX_ACCESS_OK) {
        fprintf(stderr, "cannot start access\n");
        return 2;
    }
    usleep(100000);
    make_accounts();
    bind_roots(argv[3]);
    if (crux_file_service_start(&service_config) != CRUX_FILE_SERVICE_OK) {
        fprintf(stderr, "cannot start the file service\n");
        return 2;
    }

    wolfSSH_Init();
    context = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL);
    if (context == NULL) {
        fprintf(stderr, "no wolfSSH context\n");
        return 2;
    }
    wolfSSH_SetIORecv(context, on_recv);
    wolfSSH_SetIOSend(context, on_send);
    wolfSSH_SetUserAuth(context, on_user_auth);
    wolfSSH_CTX_SetStrictKex(context, 1);
    wolfSSH_CTX_SetAlgoListKex(context, "curve25519-sha256,ecdh-sha2-nistp256");
    wolfSSH_CTX_SetAlgoListKey(context, "ecdsa-sha2-nistp256");
    wolfSSH_CTX_SetAlgoListKeyAccepted(context, "ssh-ed25519,ecdsa-sha2-nistp256");
    wolfSSH_CTX_SetAlgoListCipher(context,
                                  "aes128-gcm@openssh.com,aes256-gcm@openssh.com,aes128-ctr");
    wolfSSH_CTX_SetAlgoListMac(context, "hmac-sha2-256");
    load_host_key(argv[2]);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    struct sockaddr_in address = {.sin_family = AF_INET,
                                  .sin_port = htons((uint16_t)atoi(argv[1])),
                                  .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, SESSIONS_MAX) != 0) {
        perror("listen");
        return 2;
    }
    printf("listening\n");
    fflush(stdout);
    for (;;) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            continue;
        }
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        pthread_t worker;
        if (pthread_create(&worker, NULL, serve_connection, (void *)(intptr_t)fd) == 0) {
            pthread_detach(worker);
        } else {
            close(fd);
        }
    }
}
