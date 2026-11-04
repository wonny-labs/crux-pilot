/* What the fake promises to the CLI and SFTP Host tests, beyond the conformance cases. */
#include "fake_provider.h"
#include "ssh_check.h"

#include <string.h>

static crux_ssh_auth_result_t deny_password(void *context, const crux_ssh_peer_t *peer,
                                            const char *user, const uint8_t *password,
                                            size_t password_length,
                                            crux_ssh_attachment_t **attachment)
{
    (void)context;
    (void)peer;
    (void)user;
    (void)password;
    (void)password_length;
    (void)attachment;
    return CRUX_SSH_AUTH_DENIED;
}

static crux_ssh_auth_result_t deny_check(void *context, const crux_ssh_peer_t *peer,
                                         const char *user, crux_ssh_key_type_t key_type,
                                         const uint8_t *key, size_t key_length, bool signed_request)
{
    (void)context;
    (void)peer;
    (void)user;
    (void)key_type;
    (void)key;
    (void)key_length;
    (void)signed_request;
    return CRUX_SSH_AUTH_DENIED;
}

static crux_ssh_auth_result_t deny_verified(void *context, const crux_ssh_peer_t *peer,
                                            const char *user, crux_ssh_key_type_t key_type,
                                            const uint8_t *key, size_t key_length,
                                            crux_ssh_attachment_t **attachment)
{
    (void)context;
    (void)peer;
    (void)user;
    (void)key_type;
    (void)key;
    (void)key_length;
    (void)attachment;
    return CRUX_SSH_AUTH_DENIED;
}

static void ignore_failed(void *context, const crux_ssh_peer_t *peer, const char *user)
{
    (void)context;
    (void)peer;
    (void)user;
}

static int no_shell(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                    bool terminal)
{
    (void)context;
    (void)attachment;
    (void)channel;
    (void)terminal;
    return 0;
}

static int no_exec(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                   const char *command, size_t command_length)
{
    (void)context;
    (void)attachment;
    (void)channel;
    (void)command;
    (void)command_length;
    return 0;
}

static crux_status_t no_sftp(void *context, crux_ssh_attachment_t *attachment,
                             crux_ssh_file_handle_t **handle)
{
    (void)context;
    (void)attachment;
    (void)handle;
    return CRUX_ERR_NOT_SUPPORTED;
}

static void no_sftp_close(void *context, crux_ssh_attachment_t *attachment,
                          crux_ssh_file_handle_t *handle)
{
    (void)context;
    (void)attachment;
    (void)handle;
}

static bool never_alive(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    (void)attachment;
    return false;
}

static void ignore_closed(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    (void)attachment;
}

int main(void)
{
    static const crux_ssh_callbacks_t callbacks = {
        .password = deny_password,
        .key_check = deny_check,
        .key_verified = deny_verified,
        .key_failed = ignore_failed,
        .shell = no_shell,
        .exec = no_exec,
        .sftp_open = no_sftp,
        .sftp_close = no_sftp_close,
        .session_alive = never_alive,
        .connection_closed = ignore_closed,
    };
    static char line[CRUX_SSH_FAKE_BUFFER];
    const crux_ssh_test_client_t *client = crux_ssh_fake_client();
    crux_ssh_config_t config = {
        .max_connections = 1,
        .max_auth_attempts = 3,
        .auth_timeout_ms = 1,
        .io_timeout_ms = 1,
        .host_key_type = client->host_key_type,
        .host_key = client->host_key,
        .host_key_length = client->host_key_length,
    };
    int connection = -1;

    crux_ssh_fake_reset();
    SSH_CHECK(client->provider->start(&config, &callbacks) == CRUX_OK);
    SSH_CHECK(client->connect(&client->peer_a, &connection));

    /* A line of the CLI's default limit of 320 bytes fits, and input past the buffer is reported.
     */
    SSH_CHECK(CRUX_SSH_FAKE_BUFFER >= 1024u);
    SSH_CHECK(client->send(connection, line, sizeof(line)));
    SSH_CHECK(!client->send(connection, "x", 1));

    /* The material is the size of real key blobs. */
    SSH_CHECK(client->ed25519_key_length == 51u);
    SSH_CHECK(client->ecdsa_key_length == 104u);
    SSH_CHECK(memcmp(client->ed25519_key + 4, "ssh-ed25519", 11) == 0);

    SSH_CHECK(client->provider->stop() == CRUX_OK);
    printf("%u failures\n", ssh_check_failures);
    return ssh_check_failures == 0 ? 0 : 1;
}
