#include "fake_provider.h"

#include <string.h>

#define FAKE_CAPACITY 4u
#define FAKE_CONNECTION_RECORDS 32u
#define FAKE_NAME_MAX 64u

/* The size of real key blobs: ssh-ed25519 is 51 bytes and ecdsa-sha2-nistp256 is 104. */
#define FAKE_ED25519_KEY_LENGTH 51u
#define FAKE_ECDSA_KEY_LENGTH 104u

struct crux_ssh_channel {
    int connection;
    bool closed;
};

typedef struct {
    bool connected;
    bool authenticated;
    bool channel_open;
    bool session_started;
    bool terminal;
    unsigned attempts;
    unsigned requests;
    bool name_bound;
    char bound_name[FAKE_NAME_MAX];
    size_t bound_name_length;
    crux_ssh_peer_t peer;
    crux_ssh_attachment_t *attachment;
    crux_ssh_file_handle_t *file_handle;
    struct crux_ssh_channel channel;
    char input[CRUX_SSH_FAKE_BUFFER];
    size_t input_length;
    size_t input_position;
    bool input_eof;
    char output[CRUX_SSH_FAKE_BUFFER];
    size_t output_length;
    size_t window;
    bool output_eof;
    bool drop_on_write;
    bool dropped;
    bool has_exit_status;
    int exit_status;
} connection_t;

static struct {
    bool started;
    bool stopped_once;
    bool terminal_seen;
    unsigned callback_depth;
    crux_ssh_fake_fault_t fault;
    crux_ssh_config_t config;
    crux_ssh_callbacks_t callbacks;
    const crux_ssh_callbacks_t *borrowed_callbacks;
    connection_t connections[FAKE_CONNECTION_RECORDS];
    size_t connection_records;
} fake;

static const uint8_t fake_host_key[FAKE_ED25519_KEY_LENGTH] = {0x48, 0x4f, 0x53, 0x54};
static const uint8_t fake_ed25519_key[FAKE_ED25519_KEY_LENGTH] = {
    0, 0, 0, 11, 's', 's', 'h', '-', 'e', 'd', '2', '5', '5', '1', '9'};
static const uint8_t fake_ecdsa_key[FAKE_ECDSA_KEY_LENGTH] = {0, 0, 0, 19, 'e', 'c', 'd', 's', 'a'};
/* Stands for a handle that is not the one Crux returned. It is only ever compared. */
static char wrong_handle;

static bool has_fault(crux_ssh_fake_fault_t fault)
{
    return fake.fault == fault;
}

/* The one place where the fake reads its callbacks. */
static const crux_ssh_callbacks_t *callbacks(void)
{
    if (has_fault(CRUX_SSH_FAKE_FAULT_START_BORROWS_CALLBACKS) && fake.borrowed_callbacks != NULL) {
        return fake.borrowed_callbacks;
    }
    return &fake.callbacks;
}

static connection_t *find_connection(int id)
{
    if (id < 0 || (size_t)id >= fake.connection_records) {
        return NULL;
    }
    return &fake.connections[id];
}

static connection_t *connected_connection(int id)
{
    connection_t *connection = find_connection(id);

    return connection != NULL && connection->connected ? connection : NULL;
}

static size_t active_connections(void)
{
    size_t count = 0;

    for (size_t i = 0; i < fake.connection_records; ++i) {
        count += fake.connections[i].connected;
    }
    return count;
}

/* The address a callback receives, which a fault can take from another connection. */
static const crux_ssh_peer_t *peer_for(const connection_t *connection, crux_ssh_fake_fault_t fault)
{
    if (has_fault(fault)) {
        for (size_t i = 0; i < fake.connection_records; ++i) {
            if (&fake.connections[i] != connection && fake.connections[i].connected) {
                return &fake.connections[i].peer;
            }
        }
    }
    return &connection->peer;
}

static void end_sftp_session(connection_t *connection)
{
    crux_ssh_file_handle_t *handle = connection->file_handle;

    if (handle != NULL) {
        connection->file_handle = NULL;
        if (!has_fault(CRUX_SSH_FAKE_FAULT_NO_SFTP_CLOSE)) {
            if (has_fault(CRUX_SSH_FAKE_FAULT_SFTP_CLOSE_WRONG_HANDLE)) {
                handle = (crux_ssh_file_handle_t *)&wrong_handle;
            }
            ++fake.callback_depth;
            callbacks()->sftp_close(callbacks()->context, connection->attachment, handle);
            --fake.callback_depth;
        }
    }
}

static void notify_closed(connection_t *connection)
{
    if (connection->attachment != NULL && !has_fault(CRUX_SSH_FAKE_FAULT_NO_CONNECTION_CLOSED)) {
        ++fake.callback_depth;
        callbacks()->connection_closed(callbacks()->context, connection->attachment);
        if (has_fault(CRUX_SSH_FAKE_FAULT_CONNECTION_CLOSED_TWICE)) {
            callbacks()->connection_closed(callbacks()->context, connection->attachment);
        }
        --fake.callback_depth;
    }
}

static void end_connection(connection_t *connection)
{
    if (!connection->connected) {
        return;
    }
    connection->connected = false;
    if (has_fault(CRUX_SSH_FAKE_FAULT_CLOSED_BEFORE_SFTP_CLOSE)) {
        notify_closed(connection);
        end_sftp_session(connection);
    } else {
        end_sftp_session(connection);
        notify_closed(connection);
    }
    connection->attachment = NULL;
    connection->authenticated = false;
    connection->channel_open = false;
}

static void count_failure(connection_t *connection)
{
    ++connection->attempts;
    if (connection->attempts >= fake.config.max_auth_attempts &&
        !has_fault(CRUX_SSH_FAKE_FAULT_NO_ATTEMPT_LIMIT)) {
        end_connection(connection);
    }
}

/* ---- Provider ---- */

static crux_status_t fake_start(const crux_ssh_config_t *config, const crux_ssh_callbacks_t *given)
{
    if (config == NULL || given == NULL || given->password == NULL || given->key_check == NULL ||
        given->key_verified == NULL || given->key_failed == NULL || given->shell == NULL ||
        given->exec == NULL || given->sftp_open == NULL || given->sftp_close == NULL ||
        given->session_alive == NULL || given->connection_closed == NULL ||
        config->max_connections == 0 || config->max_auth_attempts == 0 ||
        config->auth_timeout_ms == 0 || config->io_timeout_ms == 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!has_fault(CRUX_SSH_FAKE_FAULT_START_ACCEPTS_NO_HOST_KEY) &&
        (config->host_key == NULL || config->host_key_length == 0 ||
         (config->host_key_type != CRUX_SSH_KEY_ED25519 &&
          config->host_key_type != CRUX_SSH_KEY_ECDSA_P256))) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (config->max_connections > FAKE_CAPACITY) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    if (fake.started || (fake.stopped_once && has_fault(CRUX_SSH_FAKE_FAULT_RESTART_REFUSED))) {
        return CRUX_ERR_INVALID_STATE;
    }
    fake.config = *config;
    fake.callbacks = *given;
    fake.borrowed_callbacks = given;
    fake.connection_records = 0;
    fake.started = true;
    return CRUX_OK;
}

static crux_status_t fake_stop(void)
{
    if (!fake.started) {
        return CRUX_ERR_INVALID_STATE;
    }
    if (fake.callback_depth != 0) {
        return has_fault(CRUX_SSH_FAKE_FAULT_STOP_FROM_CALLBACK) ? CRUX_OK : CRUX_ERR_INVALID_STATE;
    }
    if (!has_fault(CRUX_SSH_FAKE_FAULT_STOP_KEEPS_CONNECTIONS)) {
        for (size_t i = 0; i < fake.connection_records; ++i) {
            if (has_fault(CRUX_SSH_FAKE_FAULT_STOP_SKIPS_SFTP_CLOSE)) {
                fake.connections[i].file_handle = NULL;
            }
            end_connection(&fake.connections[i]);
        }
    }
    fake.started = false;
    fake.stopped_once = true;
    return CRUX_OK;
}

static crux_status_t fake_read(crux_ssh_channel_t *channel, void *buffer, size_t length,
                               size_t *transferred)
{
    connection_t *connection;
    size_t available;

    if (transferred != NULL) {
        *transferred = 0;
    }
    if (channel == NULL || buffer == NULL || length == 0 || transferred == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    connection = &fake.connections[channel->connection];
    if (channel->closed && !has_fault(CRUX_SSH_FAKE_FAULT_READ_AFTER_CLOSE)) {
        return CRUX_ERR_INVALID_STATE;
    }
    if (connection->dropped && !has_fault(CRUX_SSH_FAKE_FAULT_READ_AFTER_DROP_OK)) {
        return CRUX_ERR_IO;
    }
    available = connection->input_length - connection->input_position;
    if (available == 0) {
        return connection->input_eof && !has_fault(CRUX_SSH_FAKE_FAULT_READ_EOF_AS_TIMEOUT)
                   ? CRUX_ERR_NO_DATA
                   : CRUX_ERR_TIMEOUT;
    }
    if (available > length) {
        available = length;
    }
    memcpy(buffer, connection->input + connection->input_position, available);
    connection->input_position += available;
    *transferred = available;
    return CRUX_OK;
}

static crux_status_t fake_write(crux_ssh_channel_t *channel, const void *buffer, size_t length,
                                size_t *transferred)
{
    connection_t *connection;
    size_t room;

    if (transferred != NULL) {
        *transferred = 0;
    }
    if (channel == NULL || buffer == NULL || length == 0 || transferred == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    connection = &fake.connections[channel->connection];
    if (channel->closed) {
        return CRUX_ERR_INVALID_STATE;
    }
    if (connection->drop_on_write) {
        connection->dropped = true;
        if (has_fault(CRUX_SSH_FAKE_FAULT_WRITE_DROP_HIDDEN)) {
            connection->dropped = false;
            connection->drop_on_write = false;
            *transferred = length;
            return CRUX_OK;
        }
        return CRUX_ERR_IO;
    }
    if (connection->dropped) {
        return CRUX_ERR_IO;
    }
    room = has_fault(CRUX_SSH_FAKE_FAULT_WRITE_IGNORES_WINDOW) ? length : connection->window;
    if (room == 0) {
        return CRUX_ERR_TIMEOUT;
    }
    if (room > length) {
        room = length;
    }
    if (room > CRUX_SSH_FAKE_BUFFER - connection->output_length) {
        room = CRUX_SSH_FAKE_BUFFER - connection->output_length;
    }
    memcpy(connection->output + connection->output_length, buffer, room);
    connection->output_length += room;
    if (!has_fault(CRUX_SSH_FAKE_FAULT_WRITE_IGNORES_WINDOW)) {
        connection->window -= room;
    }
    *transferred = room;
    return CRUX_OK;
}

static crux_status_t fake_close(crux_ssh_channel_t *channel)
{
    if (channel == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!channel->closed) {
        channel->closed = true;
        if (!has_fault(CRUX_SSH_FAKE_FAULT_NO_EOF_ON_CLOSE)) {
            fake.connections[channel->connection].output_eof = true;
        }
    }
    return CRUX_OK;
}

static const crux_ssh_provider_t provider = {
    .start = fake_start,
    .stop = fake_stop,
    .read = fake_read,
    .write = fake_write,
    .close = fake_close,
};

/* ---- Client ---- */

static bool client_connect(const crux_ssh_peer_t *peer, int *id)
{
    connection_t *connection;

    if (!fake.started || fake.connection_records == FAKE_CONNECTION_RECORDS ||
        (active_connections() >= fake.config.max_connections &&
         !has_fault(CRUX_SSH_FAKE_FAULT_NO_CONNECTION_LIMIT))) {
        return false;
    }
    *id = (int)fake.connection_records++;
    connection = &fake.connections[*id];
    memset(connection, 0, sizeof(*connection));
    connection->connected = true;
    connection->peer = *peer;
    connection->window = CRUX_SSH_FAKE_BUFFER;
    connection->channel.connection = *id;
    return true;
}

/*
 * The name rules that apply to every authentication request. False means the request has been dealt
 * with: the connection has ended, or the attempt has been counted.
 */
static bool admit_name(connection_t *connection, const char *user, size_t user_length)
{
    size_t kept = user_length < FAKE_NAME_MAX ? user_length : FAKE_NAME_MAX;

    if (user_length == 0 && !has_fault(CRUX_SSH_FAKE_FAULT_NO_USER_CHECK)) {
        if (has_fault(CRUX_SSH_FAKE_FAULT_EMPTY_NAME_KEEPS_CONNECTION)) {
            count_failure(connection);
        } else {
            end_connection(connection);
        }
        return false;
    }
    if (!connection->name_bound) {
        connection->name_bound = true;
        connection->bound_name_length = user_length;
        memcpy(connection->bound_name, user, kept);
    } else if ((connection->bound_name_length != user_length ||
                memcmp(connection->bound_name, user, kept) != 0) &&
               !has_fault(CRUX_SSH_FAKE_FAULT_NAME_CHANGE_ALLOWED)) {
        end_connection(connection);
        return false;
    }
    ++connection->requests;
    if (!has_fault(CRUX_SSH_FAKE_FAULT_NO_USER_CHECK) &&
        (user_length > CRUX_SSH_USER_NAME_MAX || memchr(user, '\0', user_length) != NULL)) {
        count_failure(connection);
        return false;
    }
    return true;
}

/* Copies a length-delimited user name, which admit_name has checked unless a fault disabled that.
 */
static void terminated_user(char *out, const char *user, size_t user_length)
{
    size_t length = user_length <= CRUX_SSH_USER_NAME_MAX ? user_length : CRUX_SSH_USER_NAME_MAX;

    memcpy(out, user, length);
    out[length] = '\0';
}

/* Whether a credential callback's answer authenticates. */
static bool grants(crux_ssh_auth_result_t result, const crux_ssh_attachment_t *attachment)
{
    if (has_fault(CRUX_SSH_FAKE_FAULT_UNKNOWN_RESULT_ACCEPTS)) {
        return result != CRUX_SSH_AUTH_DENIED;
    }
    return result == CRUX_SSH_AUTH_ACCEPTED && attachment != NULL;
}

/* A request after a successful login ends the connection, before any callback. */
static bool may_attempt(connection_t *connection)
{
    if (connection == NULL) {
        return false;
    }
    if (connection->authenticated && !has_fault(CRUX_SSH_FAKE_FAULT_SECOND_LOGIN_OVERWRITES)) {
        if (!has_fault(CRUX_SSH_FAKE_FAULT_SECOND_LOGIN_KEEPS_CONNECTION)) {
            end_connection(connection);
        }
        return false;
    }
    return true;
}

static bool client_password(int id, const char *user, size_t user_length, const char *password)
{
    connection_t *connection = connected_connection(id);
    crux_ssh_attachment_t *attachment = NULL;
    char name[CRUX_SSH_USER_NAME_MAX + 1];
    uint8_t wrong[CRUX_SSH_FAKE_BUFFER];
    const uint8_t *bytes = (const uint8_t *)password;
    size_t length = strlen(password);
    crux_ssh_auth_result_t result;

    if (!may_attempt(connection) || !admit_name(connection, user, user_length)) {
        return false;
    }
    /* Only START_BORROWS_CALLBACKS can lose a callback: the caller has cleared its structure. */
    if (callbacks()->password == NULL) {
        count_failure(connection);
        return false;
    }
    if (has_fault(CRUX_SSH_FAKE_FAULT_PASSWORD_WRONG_LENGTH) && length > 0) {
        --length;
    }
    if (has_fault(CRUX_SSH_FAKE_FAULT_PASSWORD_WRONG_BYTES) && length <= sizeof(wrong)) {
        memset(wrong, 'x', length);
        bytes = wrong;
    }
    terminated_user(name, user, user_length);
    ++fake.callback_depth;
    result = callbacks()->password(callbacks()->context,
                                   peer_for(connection, CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_PASSWORD),
                                   name, bytes, length, &attachment);
    --fake.callback_depth;
    if (!grants(result, attachment)) {
        count_failure(connection);
        return false;
    }
    connection->attachment = attachment;
    connection->authenticated = true;
    return true;
}

static bool client_none_request(int id, const char *user)
{
    connection_t *connection = connected_connection(id);
    unsigned before;

    if (!may_attempt(connection)) {
        return false;
    }
    before = connection->requests;
    if (admit_name(connection, user, strlen(user)) &&
        (before != 0 || has_fault(CRUX_SSH_FAKE_FAULT_OPENING_NONE_COUNTED))) {
        count_failure(connection);
    }
    return false;
}

static bool key_type_from_name(const char *algorithm, crux_ssh_key_type_t *type)
{
    if (strcmp(algorithm, "ssh-ed25519") == 0) {
        *type = CRUX_SSH_KEY_ED25519;
        return true;
    }
    if (strcmp(algorithm, "ecdsa-sha2-nistp256") == 0) {
        *type = CRUX_SSH_KEY_ECDSA_P256;
        return true;
    }
    if (has_fault(CRUX_SSH_FAKE_FAULT_ACCEPTS_RSA) && strncmp(algorithm, "rsa-", 4) == 0) {
        *type = CRUX_SSH_KEY_ED25519;
        return true;
    }
    return false;
}

static bool complete_key_login(connection_t *connection, const char *name, crux_ssh_key_type_t type,
                               const uint8_t *key, size_t key_length)
{
    crux_ssh_attachment_t *attachment = NULL;
    crux_ssh_auth_result_t result;

    ++fake.callback_depth;
    result = callbacks()->key_verified(
        callbacks()->context, peer_for(connection, CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_VERIFIED),
        name, type, key, key_length, &attachment);
    --fake.callback_depth;
    if (grants(result, attachment) || has_fault(CRUX_SSH_FAKE_FAULT_VERIFIED_RESULT_IGNORED)) {
        connection->attachment = attachment;
        connection->authenticated = true;
        return true;
    }
    return false;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool client_key_probe(int id, const char *user, const char *algorithm, const uint8_t *key,
                             size_t key_length)
{
    connection_t *connection = connected_connection(id);
    char name[CRUX_SSH_USER_NAME_MAX + 1];
    crux_ssh_key_type_t type;
    crux_ssh_auth_result_t result;

    if (!may_attempt(connection) || !admit_name(connection, user, strlen(user))) {
        return false;
    }
    if (!key_type_from_name(algorithm, &type)) {
        if (!has_fault(CRUX_SSH_FAKE_FAULT_NOT_COUNTED_REFUSED_TYPE)) {
            count_failure(connection);
        }
        return false;
    }
    terminated_user(name, user, strlen(user));
    ++fake.callback_depth;
    result = callbacks()->key_check(callbacks()->context,
                                    peer_for(connection, CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_PROBE),
                                    name, type, key, key_length, false);
    --fake.callback_depth;
    if (result != CRUX_SSH_AUTH_ACCEPTED) {
        count_failure(connection);
        return false;
    }
    if (has_fault(CRUX_SSH_FAKE_FAULT_SESSION_FROM_PROBE)) {
        (void)complete_key_login(connection, name, type, key, key_length);
    }
    return true;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool client_key_login(int id, const char *user, const char *algorithm, const uint8_t *key,
                             size_t key_length, bool good_signature)
{
    connection_t *connection = connected_connection(id);
    char name[CRUX_SSH_USER_NAME_MAX + 1];
    crux_ssh_key_type_t type;
    crux_ssh_auth_result_t result;

    if (!may_attempt(connection) || !admit_name(connection, user, strlen(user))) {
        return false;
    }
    if (!key_type_from_name(algorithm, &type)) {
        if (!has_fault(CRUX_SSH_FAKE_FAULT_NOT_COUNTED_REFUSED_TYPE)) {
            count_failure(connection);
        }
        return false;
    }
    terminated_user(name, user, strlen(user));
    ++fake.callback_depth;
    result = callbacks()->key_check(
        callbacks()->context, peer_for(connection, CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_SIGNED), name,
        type, key, has_fault(CRUX_SSH_FAKE_FAULT_SIGNED_CHECK_EMPTY_KEY) ? 0 : key_length, true);
    --fake.callback_depth;
    if (result != CRUX_SSH_AUTH_ACCEPTED) {
        if (!has_fault(CRUX_SSH_FAKE_FAULT_NOT_COUNTED_REFUSED_SIGNED)) {
            count_failure(connection);
        }
        return false;
    }
    if (!good_signature && !has_fault(CRUX_SSH_FAKE_FAULT_SIGNATURE_NOT_CHECKED)) {
        if (!has_fault(CRUX_SSH_FAKE_FAULT_KEY_FAILURE_SILENT)) {
            ++fake.callback_depth;
            callbacks()->key_failed(callbacks()->context,
                                    peer_for(connection, CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_FAILED),
                                    name);
            --fake.callback_depth;
        }
        count_failure(connection);
        return false;
    }
    if (!complete_key_login(connection, name, type, key, key_length)) {
        if (!has_fault(CRUX_SSH_FAKE_FAULT_NOT_COUNTED_VERIFIED_REFUSAL)) {
            count_failure(connection);
        }
        return false;
    }
    return true;
}

static bool client_is_connected(int id)
{
    return connected_connection(id) != NULL;
}

static void client_pass_auth_time(int id)
{
    connection_t *connection = connected_connection(id);

    if (connection != NULL && !connection->authenticated &&
        !has_fault(CRUX_SSH_FAKE_FAULT_NO_AUTH_TIME_LIMIT)) {
        end_connection(connection);
    }
}

static void client_pass_read_timeout(int id)
{
    connection_t *connection = connected_connection(id);
    crux_ssh_attachment_t *attachment;
    bool alive;

    if (connection == NULL || connection->file_handle == NULL) {
        return;
    }
    if (has_fault(CRUX_SSH_FAKE_FAULT_SESSION_ALIVE_IGNORED)) {
        return;
    }
    attachment = connection->attachment;
    if (has_fault(CRUX_SSH_FAKE_FAULT_SESSION_ALIVE_WRONG_ATTACHMENT)) {
        attachment = NULL;
    }
    if (has_fault(CRUX_SSH_FAKE_FAULT_SESSION_ALIVE_OTHER_CONNECTION)) {
        for (size_t i = 0; i < fake.connection_records; ++i) {
            if (&fake.connections[i] != connection && fake.connections[i].connected) {
                attachment = fake.connections[i].attachment;
            }
        }
    }
    ++fake.callback_depth;
    alive = callbacks()->session_alive(callbacks()->context, attachment);
    --fake.callback_depth;
    if (!alive) {
        end_connection(connection);
    }
}

static bool client_open_channel(int id, const char *type)
{
    connection_t *connection = connected_connection(id);

    if (connection == NULL) {
        return false;
    }
    if (has_fault(CRUX_SSH_FAKE_FAULT_ACCEPTS_FORWARDING) && strcmp(type, "direct-tcpip") == 0) {
        return true;
    }
    if (!connection->authenticated && !has_fault(CRUX_SSH_FAKE_FAULT_REQUEST_BEFORE_AUTH)) {
        return false;
    }
    if (strcmp(type, "session") != 0) {
        return false;
    }
    if (connection->channel_open && !has_fault(CRUX_SSH_FAKE_FAULT_ACCEPTS_SECOND_CHANNEL)) {
        return false;
    }
    connection->channel_open = true;
    return true;
}

/* The attachment a session callback receives. */
static crux_ssh_attachment_t *attachment_for(const connection_t *connection)
{
    if (has_fault(CRUX_SSH_FAKE_FAULT_WRONG_ATTACHMENT)) {
        for (size_t i = 0; i < fake.connection_records; ++i) {
            if (fake.connections[i].connected && fake.connections[i].attachment != NULL) {
                return fake.connections[i].attachment;
            }
        }
    }
    return connection->attachment;
}

static void finish_session(connection_t *connection, int status)
{
    if (!connection->dropped) {
        if (!has_fault(CRUX_SSH_FAKE_FAULT_NO_EXIT_STATUS)) {
            bool in_range = status >= 0 && status <= 255;

            connection->has_exit_status = true;
            connection->exit_status =
                in_range || has_fault(CRUX_SSH_FAKE_FAULT_EXIT_STATUS_NOT_CLAMPED) ? status : 255;
        }
        if (!has_fault(CRUX_SSH_FAKE_FAULT_NO_EOF_ON_CLOSE)) {
            connection->output_eof = true;
        }
    }
    end_connection(connection);
}

static bool run_shell(connection_t *connection)
{
    bool terminal = connection->terminal;
    int status;

    if (has_fault(CRUX_SSH_FAKE_FAULT_TERMINAL_ALWAYS_FALSE)) {
        terminal = false;
    } else if (has_fault(CRUX_SSH_FAKE_FAULT_TERMINAL_ALWAYS_TRUE)) {
        terminal = true;
    } else if (has_fault(CRUX_SSH_FAKE_FAULT_TERMINAL_ACROSS_CONNECTIONS)) {
        terminal = fake.terminal_seen;
    }
    connection->session_started = true;
    ++fake.callback_depth;
    status = callbacks()->shell(callbacks()->context, attachment_for(connection),
                                &connection->channel, terminal);
    --fake.callback_depth;
    finish_session(connection, status);
    return true;
}

static bool run_exec(connection_t *connection, const char *command, size_t command_length)
{
    char terminated[CRUX_SSH_FAKE_BUFFER];
    int status;

    if (command_length >= sizeof(terminated) ||
        (memchr(command, '\0', command_length) != NULL &&
         !has_fault(CRUX_SSH_FAKE_FAULT_COMMAND_NUL_PASSED))) {
        return false;
    }
    memcpy(terminated, command, command_length);
    terminated[command_length] = '\0';
    connection->session_started = true;
    ++fake.callback_depth;
    status = callbacks()->exec(callbacks()->context, attachment_for(connection),
                               &connection->channel, terminated, command_length);
    --fake.callback_depth;
    finish_session(connection, status);
    return true;
}

static bool run_subsystem(connection_t *connection, const char *name, size_t name_length)
{
    static char fault_handle;
    crux_ssh_file_handle_t *handle = NULL;
    crux_status_t status;

    if (name_length != 4 || memcmp(name, "sftp", 4) != 0) {
        return has_fault(CRUX_SSH_FAKE_FAULT_ACCEPTS_OTHER_SUBSYSTEM) && name_length == 3 &&
               memcmp(name, "scp", 3) == 0;
    }
    connection->session_started = true;
    ++fake.callback_depth;
    status = callbacks()->sftp_open(callbacks()->context, attachment_for(connection), &handle);
    --fake.callback_depth;
    if (status == CRUX_OK && handle == NULL &&
        has_fault(CRUX_SSH_FAKE_FAULT_SFTP_NULL_HANDLE_ACCEPTED)) {
        handle = (crux_ssh_file_handle_t *)&fault_handle;
    }
    if (status != CRUX_OK || handle == NULL) {
        end_connection(connection);
        return false;
    }
    connection->file_handle = handle;
    return true;
}

/* The signature of each NOLINT function here is fixed by ssh_test_client.h. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static bool client_channel_request(int id, const char *type, const char *data, size_t data_length)
{
    connection_t *connection = connected_connection(id);

    if (connection == NULL) {
        return false;
    }
    if (has_fault(CRUX_SSH_FAKE_FAULT_REQUEST_BEFORE_AUTH) && !connection->authenticated &&
        strcmp(type, "shell") == 0) {
        return run_shell(connection);
    }
    if (!connection->authenticated || !connection->channel_open || connection->session_started) {
        return false;
    }
    if (strcmp(type, "pty-req") == 0) {
        if (has_fault(CRUX_SSH_FAKE_FAULT_TERMINAL_REQUEST_REFUSED)) {
            return false;
        }
        connection->terminal = true;
        fake.terminal_seen = true;
        return true;
    }
    if (strcmp(type, "window-change") == 0) {
        return true;
    }
    if (strcmp(type, "shell") == 0) {
        return run_shell(connection);
    }
    if (strcmp(type, "exec") == 0) {
        return run_exec(connection, data, data_length);
    }
    if (strcmp(type, "subsystem") == 0) {
        return run_subsystem(connection, data, data_length);
    }
    return false;
}

static bool client_global_request(int id, const char *type)
{
    (void)id;
    return has_fault(CRUX_SSH_FAKE_FAULT_ACCEPTS_FORWARDING) && strcmp(type, "tcpip-forward") == 0;
}

static bool client_send(int id, const void *data, size_t length)
{
    connection_t *connection = find_connection(id);

    if (connection == NULL || length > CRUX_SSH_FAKE_BUFFER - connection->input_length) {
        return false;
    }
    memcpy(connection->input + connection->input_length, data, length);
    connection->input_length += length;
    return true;
}

static void client_send_eof(int id)
{
    connection_t *connection = find_connection(id);

    if (connection != NULL) {
        connection->input_eof = true;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void client_set_window(int id, size_t bytes)
{
    connection_t *connection = find_connection(id);

    if (connection != NULL) {
        connection->window = bytes;
    }
}

static void client_drop_on_write(int id)
{
    connection_t *connection = find_connection(id);

    if (connection != NULL) {
        connection->drop_on_write = true;
    }
}

static size_t client_received(int id, void *buffer, size_t size)
{
    connection_t *connection = find_connection(id);
    size_t length = 0;

    if (connection != NULL) {
        length = connection->output_length < size ? connection->output_length : size;
        memcpy(buffer, connection->output, length);
    }
    return length;
}

static bool client_received_eof(int id)
{
    connection_t *connection = find_connection(id);

    return connection != NULL && connection->output_eof;
}

static bool client_exit_status(int id, int *status)
{
    connection_t *connection = find_connection(id);

    if (connection == NULL || !connection->has_exit_status) {
        return false;
    }
    *status = connection->exit_status;
    return true;
}

static crux_ssh_file_handle_t *client_file_handle(int id)
{
    connection_t *connection = connected_connection(id);

    if (connection == NULL || has_fault(CRUX_SSH_FAKE_FAULT_FILE_HANDLE_NOT_SET)) {
        return NULL;
    }
    return connection->file_handle;
}

static void client_end_sftp(int id)
{
    connection_t *connection = connected_connection(id);

    if (connection != NULL) {
        end_connection(connection);
    }
}

static void client_disconnect(int id)
{
    connection_t *connection = connected_connection(id);

    if (connection != NULL) {
        end_connection(connection);
    }
}

static const crux_ssh_test_client_t client = {
    .provider = &provider,
    .connection_capacity = FAKE_CAPACITY,
    .host_key_type = CRUX_SSH_KEY_ED25519,
    .host_key = fake_host_key,
    .host_key_length = sizeof(fake_host_key),
    .ed25519_key = fake_ed25519_key,
    .ed25519_key_length = sizeof(fake_ed25519_key),
    .ecdsa_key = fake_ecdsa_key,
    .ecdsa_key_length = sizeof(fake_ecdsa_key),
    .peer_a = {.address = {192, 0, 2, 1}, .address_length = 4},
    .peer_b = {.address = {192, 0, 2, 2}, .address_length = 4},
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

/* ---- Control ---- */

void crux_ssh_fake_reset(void)
{
    memset(&fake, 0, sizeof(fake));
}

void crux_ssh_fake_set_fault(crux_ssh_fake_fault_t fault)
{
    fake.fault = fault;
}

const crux_ssh_provider_t *crux_ssh_fake_provider(void)
{
    return &provider;
}

const crux_ssh_test_client_t *crux_ssh_fake_client(void)
{
    return &client;
}
