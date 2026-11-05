#include "ssh_cli_harness.h"

#include <string.h>

ssh_cli_shim_t ssh_cli_shim;

static crux_access_session_t next_session;
static unsigned connections_closed;
static unsigned connections_opened;

static crux_ssh_auth_result_t on_password(void *context, const crux_ssh_peer_t *peer,
                                          const char *user, const uint8_t *password,
                                          size_t password_length,
                                          crux_ssh_attachment_t **attachment)
{
    (void)context;
    (void)peer;
    (void)user;
    (void)password;
    (void)password_length;
    *attachment = crux_ssh_fake_attachment(next_session);
    return *attachment != NULL ? CRUX_SSH_AUTH_ACCEPTED : CRUX_SSH_AUTH_DENIED;
}

static crux_ssh_auth_result_t on_key_check(void *context, const crux_ssh_peer_t *peer,
                                           const char *user, crux_ssh_key_type_t key_type,
                                           const uint8_t *key, size_t key_length,
                                           bool signed_request)
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

static crux_ssh_auth_result_t on_key_verified(void *context, const crux_ssh_peer_t *peer,
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

static void on_key_failed(void *context, const crux_ssh_peer_t *peer, const char *user)
{
    (void)context;
    (void)peer;
    (void)user;
}

static crux_status_t on_sftp_open(void *context, crux_ssh_attachment_t *attachment,
                                  crux_ssh_file_handle_t **handle)
{
    (void)context;
    (void)attachment;
    (void)handle;
    return CRUX_ERR_INVALID_STATE;
}

static void on_sftp_close(void *context, crux_ssh_attachment_t *attachment,
                          crux_ssh_file_handle_t *handle)
{
    (void)context;
    (void)attachment;
    (void)handle;
}

static bool on_session_alive(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    (void)attachment;
    return true;
}

static void on_connection_closed(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    (void)attachment;
    ++connections_closed;
}

static const crux_ssh_callbacks_t callbacks = {
    .context = NULL,
    .password = on_password,
    .key_check = on_key_check,
    .key_verified = on_key_verified,
    .key_failed = on_key_failed,
    .shell = crux_ssh_cli_shell,
    .exec = crux_ssh_cli_exec,
    .sftp_open = on_sftp_open,
    .sftp_close = on_sftp_close,
    .session_alive = on_session_alive,
    .connection_closed = on_connection_closed,
};

/* ---- The shim ---- */

static crux_status_t shim_start(const crux_ssh_config_t *config, const crux_ssh_callbacks_t *given)
{
    return crux_ssh_fake_provider()->start(config, given);
}

static crux_status_t shim_stop(void)
{
    return crux_ssh_fake_provider()->stop();
}

static void read_timed_out(void)
{
    ++ssh_cli_shim.read_timeouts_seen;
    if (ssh_cli_shim.on_read_timeout != NULL) {
        ssh_cli_shim.on_read_timeout(ssh_cli_shim.hook_context);
    }
}

static void write_timed_out(void)
{
    ++ssh_cli_shim.write_timeouts_seen;
    if (ssh_cli_shim.on_write_timeout != NULL) {
        ssh_cli_shim.on_write_timeout(ssh_cli_shim.hook_context);
    }
}

static crux_status_t shim_read(crux_ssh_channel_t *channel, void *buffer, size_t length,
                               size_t *transferred)
{
    crux_status_t status;

    ++ssh_cli_shim.read_calls;
    ssh_cli_shim.used_after_close = ssh_cli_shim.used_after_close || ssh_cli_shim.close_calls > 0;
    *transferred = 0;
    if (ssh_cli_shim.read_result != CRUX_OK) {
        return ssh_cli_shim.read_result;
    }
    if (ssh_cli_shim.read_says_nothing) {
        return CRUX_OK;
    }
    if (ssh_cli_shim.read_overclaims) {
        *transferred = length + 1u;
        return CRUX_OK;
    }
    if (ssh_cli_shim.read_timeouts > 0) {
        --ssh_cli_shim.read_timeouts;
        read_timed_out();
        return CRUX_ERR_TIMEOUT;
    }
    status = crux_ssh_fake_provider()->read(channel, buffer, length, transferred);
    if (status == CRUX_ERR_TIMEOUT) {
        read_timed_out();
    }
    return status;
}

static crux_status_t shim_write(crux_ssh_channel_t *channel, const void *buffer, size_t length,
                                size_t *transferred)
{
    crux_status_t status;

    ++ssh_cli_shim.write_calls;
    ssh_cli_shim.used_after_close = ssh_cli_shim.used_after_close || ssh_cli_shim.close_calls > 0;
    *transferred = 0;
    if (ssh_cli_shim.write_result != CRUX_OK) {
        return ssh_cli_shim.write_result;
    }
    if (ssh_cli_shim.write_says_nothing) {
        return CRUX_OK;
    }
    if (ssh_cli_shim.write_overclaims ||
        (ssh_cli_shim.line_end_overclaims && *(const char *)buffer == '\r')) {
        *transferred = length + 1u;
        return CRUX_OK;
    }
    if (ssh_cli_shim.write_timeouts > 0) {
        --ssh_cli_shim.write_timeouts;
        write_timed_out();
        return CRUX_ERR_TIMEOUT;
    }
    if (ssh_cli_shim.write_limit != 0 && length > ssh_cli_shim.write_limit) {
        length = ssh_cli_shim.write_limit;
    }
    status = crux_ssh_fake_provider()->write(channel, buffer, length, transferred);
    if (status == CRUX_ERR_TIMEOUT) {
        write_timed_out();
    }
    return status;
}

static crux_status_t shim_close(crux_ssh_channel_t *channel)
{
    ++ssh_cli_shim.close_calls;
    return crux_ssh_fake_provider()->close(channel);
}

static const crux_ssh_provider_t shim = {
    .start = shim_start,
    .stop = shim_stop,
    .read = shim_read,
    .write = shim_write,
    .close = shim_close,
};

/* ---- The client side ---- */

bool ssh_cli_begin(void)
{
    const crux_ssh_test_client_t *client = crux_ssh_fake_client();
    const crux_ssh_config_t config = {
        .port = 22,
        .max_connections = 4,
        .max_auth_attempts = 3,
        .auth_timeout_ms = 1000,
        .io_timeout_ms = 1000,
        .host_key_type = client->host_key_type,
        .host_key = client->host_key,
        .host_key_length = client->host_key_length,
    };

    crux_ssh_fake_reset();
    crux_ssh_fake_attachment_reset();
    memset(&ssh_cli_shim, 0, sizeof(ssh_cli_shim));
    connections_closed = 0;
    connections_opened = 0;
    return shim.start(&config, &callbacks) == CRUX_OK &&
           crux_ssh_cli_set_provider(&shim) == CRUX_OK;
}

bool ssh_cli_connect(crux_access_session_t session, int *connection)
{
    const crux_ssh_test_client_t *client = crux_ssh_fake_client();

    /* The pool of attachments is small, and an ended connection no longer uses its own. */
    if (connections_opened == connections_closed) {
        crux_ssh_fake_attachment_reset();
    }
    next_session = session;
    if (!client->connect(&client->peer_a, connection) ||
        !client->password(*connection, "alice", 5, "password") ||
        !client->open_channel(*connection, "session")) {
        return false;
    }
    ++connections_opened;
    return true;
}

bool ssh_cli_input(int connection, const void *data, size_t length, bool end)
{
    const crux_ssh_test_client_t *client = crux_ssh_fake_client();
    bool queued = length == 0 || client->send(connection, data, length);

    if (end) {
        client->send_eof(connection);
    }
    return queued;
}

bool ssh_cli_shell(int connection, bool terminal)
{
    const crux_ssh_test_client_t *client = crux_ssh_fake_client();

    if (terminal && !client->channel_request(connection, "pty-req", NULL, 0)) {
        return false;
    }
    return client->channel_request(connection, "shell", NULL, 0);
}

bool ssh_cli_exec(int connection, const char *command, size_t length)
{
    return crux_ssh_fake_client()->channel_request(connection, "exec", command, length);
}

ssh_cli_result_t ssh_cli_result(int connection)
{
    const crux_ssh_test_client_t *client = crux_ssh_fake_client();
    ssh_cli_result_t result;

    memset(&result, 0, sizeof(result));
    result.length = client->received(connection, result.text, CRUX_SSH_FAKE_BUFFER);
    result.has_status = client->exit_status(connection, &result.status);
    result.eof = client->received_eof(connection);
    return result;
}

unsigned ssh_cli_connections_closed(void)
{
    return connections_closed;
}
