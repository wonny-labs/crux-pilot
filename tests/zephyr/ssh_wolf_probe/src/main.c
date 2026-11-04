/*
 * Links the wolfSSH server, stream and SFTP paths, and one TLS socket when the image has the socket
 * backend, so that the settings arrangement is built as a real image would use it. It is never run.
 */
#include "crux_ssh_callbacks.h"
#include <wolfssh/internal.h>
#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>
#include <zephyr/net/socket.h>

/* The design's algorithm table, enforced where wolfSSH decides what it can offer. */
#ifdef NO_FILESYSTEM
#error "wolfSSH must not see NO_FILESYSTEM"
#endif
#ifndef WOLFSSH_USER_FILESYSTEM
#error "SFTP must use the user file system port"
#endif
#ifdef WOLFSSH_SCP
#error "SCP stays disabled"
#endif
#if defined(WOLFSSH_NO_ED25519) || defined(WOLFSSH_NO_ECDSA_SHA2_NISTP256)
#error "Ed25519 and ECDSA P-256 keys are required"
#endif
#if defined(WOLFSSH_NO_CURVE25519_SHA256) || defined(WOLFSSH_NO_ECDH_SHA2_NISTP256)
#error "curve25519-sha256 and ecdh-sha2-nistp256 are required"
#endif
#if defined(WOLFSSH_NO_AES_GCM) || defined(WOLFSSH_NO_AES_CTR)
#error "AES-GCM and AES-CTR are required"
#endif
#ifdef WOLFSSH_NO_HMAC_SHA2_256
#error "hmac-sha2-256 is required"
#endif
#ifndef WOLFSSH_NO_RSA
#error "Owner decision: no RSA in SSH"
#endif
#if !defined(WOLFSSH_NO_HMAC_SHA1) || !defined(WOLFSSH_NO_AES_CBC)
#error "SHA-1 and CBC are not offered"
#endif

static int recv_cb(WOLFSSH *ssh, void *buf, word32 sz, void *ctx)
{
    (void)ctx;
    return zsock_recv(wolfSSH_get_fd(ssh), buf, sz, 0);
}

static int send_cb(WOLFSSH *ssh, void *buf, word32 sz, void *ctx)
{
    (void)ctx;
    return zsock_send(wolfSSH_get_fd(ssh), buf, sz, 0);
}

static int auth_cb(byte type, WS_UserAuthData *data, void *ctx)
{
    (void)type;
    (void)data;
    (void)ctx;
    return WOLFSSH_USERAUTH_INVALID_USER;
}

static int auth_result_cb(byte result, WS_UserAuthData *data, void *ctx)
{
    (void)result;
    (void)data;
    (void)ctx;
    return WS_FATAL_ERROR;
}

static int channel_cb(WOLFSSH_CHANNEL *channel, void *ctx)
{
    (void)channel;
    (void)ctx;
    return WS_FATAL_ERROR;
}

static int file_handle;

int main(void)
{
    byte buf[64];

#ifdef CONFIG_NET_SOCKETS_SOCKOPT_TLS
    int tls = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TLS_1_2);

    zsock_close(tls);
#endif

    if (crux_ssh_callback_table()->password == NULL) {
        return 1;
    }
    wolfSSH_Init();
    WOLFSSH_CTX *ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, NULL);

    wolfSSH_SetIORecv(ctx, recv_cb);
    wolfSSH_SetIOSend(ctx, send_cb);
    wolfSSH_SetUserAuth(ctx, auth_cb);
    wolfSSH_SetUserAuthResult(ctx, auth_result_cb);
    wolfSSH_CTX_SetStrictKex(ctx, 1);
    wolfSSH_CTX_SetAlgoListKex(ctx, "curve25519-sha256,ecdh-sha2-nistp256");
    wolfSSH_CTX_SetAlgoListKey(ctx, "ssh-ed25519,ecdsa-sha2-nistp256");
    wolfSSH_CTX_SetAlgoListKeyAccepted(ctx, "ssh-ed25519,ecdsa-sha2-nistp256");
    wolfSSH_CTX_SetAlgoListCipher(ctx, "aes128-gcm@openssh.com,aes256-gcm@openssh.com,aes128-ctr");
    wolfSSH_CTX_SetAlgoListMac(ctx, "hmac-sha2-256");
    wolfSSH_CTX_SetChannelReqShellCb(ctx, channel_cb);
    wolfSSH_CTX_SetChannelReqExecCb(ctx, channel_cb);
    wolfSSH_CTX_SetChannelReqSubsysCb(ctx, channel_cb);

    WOLFSSH *ssh = wolfSSH_new(ctx);

    wolfSSH_set_fd(ssh, -1);
    wolfSSH_SetFilesystemHandle(ssh, &file_handle);
    wolfSSH_accept(ssh);
    wolfSSH_stream_read(ssh, buf, sizeof(buf));
    wolfSSH_stream_send(ssh, buf, sizeof(buf));
    wolfSSH_SFTP_read(ssh);
    wolfSSH_free(ssh);
    wolfSSH_CTX_free(ctx);
    wolfSSH_Cleanup();
    return 0;
}
