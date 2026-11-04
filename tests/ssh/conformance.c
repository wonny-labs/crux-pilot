#include "conformance.h"

#include <stdio.h>
#include <string.h>

/* What the provider handed to Crux, so that a case can check order and content. */
struct crux_ssh_attachment {
    int id;
};
struct crux_ssh_file_handle {
    int id;
};

#define EVENT_LOG_MAX 64
#define ATTACHMENT_MAX 8
#define KEY_MAX 16

typedef enum {
    EVENT_PASSWORD,
    EVENT_KEY_CHECK,
    EVENT_KEY_VERIFIED,
    EVENT_KEY_FAILED,
    EVENT_SHELL,
    EVENT_EXEC,
    EVENT_SFTP_OPEN,
    EVENT_SFTP_CLOSE,
    EVENT_CONNECTION_CLOSED
} event_kind_t;

typedef struct {
    event_kind_t kind;
    int attachment_id;
    bool signed_request;
    crux_ssh_key_type_t key_type;
    char user[CRUX_SSH_USER_NAME_MAX + 2];
    size_t user_length;
    uint8_t key[KEY_MAX];
    size_t key_length;
    crux_ssh_peer_t peer;
} event_t;

typedef struct {
    event_t events[EVENT_LOG_MAX];
    size_t event_count;
    struct crux_ssh_attachment attachments[ATTACHMENT_MAX];
    struct crux_ssh_file_handle handles[ATTACHMENT_MAX];
    int next_attachment;
    crux_ssh_auth_result_t password_result;
    crux_ssh_auth_result_t key_check_result;
    crux_ssh_auth_result_t key_verified_result;
    crux_status_t sftp_open_status;
    /* What a session callback does. */
    const crux_ssh_test_client_t *client;
    int shell_status;
    int (*session)(crux_ssh_channel_t *channel);
    char last_command[32];
    size_t last_command_length;
    crux_status_t stop_from_callback;
    bool try_stop_in_session;
    bool withhold_attachment;
} crux_state_t;

static crux_state_t state;
static unsigned failures;
static bool verbose_output;
static const char *current_case;

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            ++failures;                                                                            \
            if (verbose_output) {                                                                  \
                printf("FAIL %s, line %d: %s\n", current_case, __LINE__, #expression);             \
            }                                                                                      \
        }                                                                                          \
    } while (0)

static const uint8_t host_key[] = {1, 2, 3, 4};
static const uint8_t user_key[] = {0x11, 0x22, 0x33, 0x44};
static const char ed25519[] = "ssh-ed25519";
static const char ecdsa[] = "ecdsa-sha2-nistp256";

static event_t *record(event_kind_t kind, const crux_ssh_attachment_t *attachment)
{
    event_t *event = NULL;

    if (state.event_count < EVENT_LOG_MAX) {
        event = &state.events[state.event_count++];
        memset(event, 0, sizeof(*event));
        event->kind = kind;
        event->attachment_id = attachment != NULL ? attachment->id : -1;
    }
    return event;
}

static size_t count_events(event_kind_t kind)
{
    size_t count = 0;

    for (size_t i = 0; i < state.event_count; ++i) {
        if (state.events[i].kind == kind) {
            ++count;
        }
    }
    return count;
}

static const event_t *find_event(event_kind_t kind)
{
    for (size_t i = 0; i < state.event_count; ++i) {
        if (state.events[i].kind == kind) {
            return &state.events[i];
        }
    }
    return NULL;
}

static crux_ssh_attachment_t *new_attachment(void)
{
    if (state.withhold_attachment || state.next_attachment >= ATTACHMENT_MAX) {
        return NULL;
    }
    state.attachments[state.next_attachment].id = state.next_attachment + 1;
    return &state.attachments[state.next_attachment++];
}

static void record_user(event_t *event, const crux_ssh_peer_t *peer, const char *user)
{
    if (event != NULL) {
        event->peer = *peer;
        event->user_length = strlen(user);
        if (event->user_length < sizeof(event->user)) {
            memcpy(event->user, user, event->user_length + 1);
        }
    }
}

static crux_ssh_auth_result_t on_password(void *context, const crux_ssh_peer_t *peer,
                                          const char *user, const uint8_t *password,
                                          size_t password_length,
                                          crux_ssh_attachment_t **attachment)
{
    (void)context;
    (void)password;
    (void)password_length;
    record_user(record(EVENT_PASSWORD, NULL), peer, user);
    if (state.password_result == CRUX_SSH_AUTH_ACCEPTED) {
        *attachment = new_attachment();
    }
    return state.password_result;
}

static crux_ssh_auth_result_t on_key_check(void *context, const crux_ssh_peer_t *peer,
                                           const char *user, crux_ssh_key_type_t key_type,
                                           const uint8_t *key, size_t key_length,
                                           bool signed_request)
{
    event_t *event = record(EVENT_KEY_CHECK, NULL);

    (void)context;
    record_user(event, peer, user);
    if (event != NULL) {
        event->signed_request = signed_request;
        event->key_type = key_type;
        event->key_length = key_length;
        if (key_length <= KEY_MAX) {
            memcpy(event->key, key, key_length);
        }
    }
    return state.key_check_result;
}

static crux_ssh_auth_result_t on_key_verified(void *context, const crux_ssh_peer_t *peer,
                                              const char *user, crux_ssh_key_type_t key_type,
                                              const uint8_t *key, size_t key_length,
                                              crux_ssh_attachment_t **attachment)
{
    event_t *event = record(EVENT_KEY_VERIFIED, NULL);

    (void)context;
    record_user(event, peer, user);
    if (event != NULL) {
        event->key_type = key_type;
        event->key_length = key_length;
        if (key_length <= KEY_MAX) {
            memcpy(event->key, key, key_length);
        }
    }
    if (state.key_verified_result == CRUX_SSH_AUTH_ACCEPTED) {
        *attachment = new_attachment();
    }
    return state.key_verified_result;
}

static void on_key_failed(void *context, const crux_ssh_peer_t *peer, const char *user)
{
    (void)context;
    record_user(record(EVENT_KEY_FAILED, NULL), peer, user);
}

static int run_session(crux_ssh_channel_t *channel)
{
    if (state.try_stop_in_session) {
        state.stop_from_callback = state.client->provider->stop();
    }
    return state.session != NULL ? state.session(channel) : state.shell_status;
}

static int on_shell(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel)
{
    (void)context;
    record(EVENT_SHELL, attachment);
    return run_session(channel);
}

static int on_exec(void *context, crux_ssh_attachment_t *attachment, crux_ssh_channel_t *channel,
                   const char *command, size_t command_length)
{
    (void)context;
    record(EVENT_EXEC, attachment);
    state.last_command_length = command_length;
    if (command_length < sizeof(state.last_command)) {
        memcpy(state.last_command, command, command_length + 1);
    }
    return run_session(channel);
}

static crux_status_t on_sftp_open(void *context, crux_ssh_attachment_t *attachment,
                                  crux_ssh_file_handle_t **handle)
{
    (void)context;
    record(EVENT_SFTP_OPEN, attachment);
    if (state.sftp_open_status != CRUX_OK) {
        return state.sftp_open_status;
    }
    if (attachment != NULL && attachment->id >= 1 && attachment->id <= ATTACHMENT_MAX) {
        state.handles[attachment->id - 1].id = attachment->id;
        *handle = &state.handles[attachment->id - 1];
    }
    return CRUX_OK;
}

static void on_sftp_close(void *context, crux_ssh_attachment_t *attachment,
                          crux_ssh_file_handle_t *handle)
{
    (void)context;
    (void)handle;
    record(EVENT_SFTP_CLOSE, attachment);
}

static void on_connection_closed(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    record(EVENT_CONNECTION_CLOSED, attachment);
}

static crux_ssh_callbacks_t all_callbacks(void)
{
    crux_ssh_callbacks_t callbacks = {
        .context = &state,
        .password = on_password,
        .key_check = on_key_check,
        .key_verified = on_key_verified,
        .key_failed = on_key_failed,
        .shell = on_shell,
        .exec = on_exec,
        .sftp_open = on_sftp_open,
        .sftp_close = on_sftp_close,
        .connection_closed = on_connection_closed,
    };

    return callbacks;
}

static crux_ssh_config_t valid_config(void)
{
    crux_ssh_config_t config = {
        .port = 2222,
        .max_connections = 2,
        .max_auth_attempts = 3,
        .auth_timeout_ms = 1000,
        .io_timeout_ms = 100,
        .host_key_type = CRUX_SSH_KEY_ED25519,
        .host_key = host_key,
        .host_key_length = sizeof(host_key),
    };

    return config;
}

static const crux_ssh_peer_t peer_a = {.address = {192, 0, 2, 1}, .address_length = 4};
static const crux_ssh_peer_t peer_b = {.address = {192, 0, 2, 2}, .address_length = 4};

static void start(const crux_ssh_test_client_t *client)
{
    crux_ssh_config_t config = valid_config();
    crux_ssh_callbacks_t callbacks = all_callbacks();

    state.client = client;
    CHECK(client->provider->start(&config, &callbacks) == CRUX_OK);
}

static int connect_ok(const crux_ssh_test_client_t *client, const crux_ssh_peer_t *peer)
{
    int connection = -1;

    CHECK(client->connect(peer, &connection));
    return connection;
}

/* A connection that has authenticated and opened its one session channel. */
static int session_connection(const crux_ssh_test_client_t *client, const crux_ssh_peer_t *peer)
{
    int connection = connect_ok(client, peer);

    state.password_result = CRUX_SSH_AUTH_ACCEPTED;
    CHECK(client->password(connection, "operator", 8, "secret"));
    CHECK(client->open_channel(connection, "session"));
    return connection;
}

/* ---- Start and stop ---- */

static void case_start_rejects_bad_arguments(const crux_ssh_test_client_t *client)
{
    const crux_ssh_provider_t *provider = client->provider;
    crux_ssh_config_t config = valid_config();
    crux_ssh_callbacks_t callbacks = all_callbacks();
    static const size_t members[] = {
        offsetof(crux_ssh_callbacks_t, password),
        offsetof(crux_ssh_callbacks_t, key_check),
        offsetof(crux_ssh_callbacks_t, key_verified),
        offsetof(crux_ssh_callbacks_t, key_failed),
        offsetof(crux_ssh_callbacks_t, shell),
        offsetof(crux_ssh_callbacks_t, exec),
        offsetof(crux_ssh_callbacks_t, sftp_open),
        offsetof(crux_ssh_callbacks_t, sftp_close),
        offsetof(crux_ssh_callbacks_t, connection_closed),
    };

    CHECK(provider->start(NULL, &callbacks) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(provider->start(&config, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    for (size_t i = 0; i < sizeof(members) / sizeof(members[0]); ++i) {
        crux_ssh_callbacks_t missing = callbacks;

        memset((char *)&missing + members[i], 0, sizeof(missing.password));
        CHECK(provider->start(&config, &missing) == CRUX_ERR_INVALID_ARGUMENT);
    }

    config = valid_config();
    config.max_connections = 0;
    CHECK(provider->start(&config, &callbacks) == CRUX_ERR_INVALID_ARGUMENT);
    config = valid_config();
    config.max_auth_attempts = 0;
    CHECK(provider->start(&config, &callbacks) == CRUX_ERR_INVALID_ARGUMENT);
    config = valid_config();
    config.auth_timeout_ms = 0;
    CHECK(provider->start(&config, &callbacks) == CRUX_ERR_INVALID_ARGUMENT);
    config = valid_config();
    config.io_timeout_ms = 0;
    CHECK(provider->start(&config, &callbacks) == CRUX_ERR_INVALID_ARGUMENT);
    config = valid_config();
    config.host_key = NULL;
    CHECK(provider->start(&config, &callbacks) == CRUX_ERR_INVALID_ARGUMENT);
    config = valid_config();
    config.host_key_length = 0;
    CHECK(provider->start(&config, &callbacks) == CRUX_ERR_INVALID_ARGUMENT);
    config = valid_config();
    config.host_key_type = (crux_ssh_key_type_t)99;
    CHECK(provider->start(&config, &callbacks) == CRUX_ERR_INVALID_ARGUMENT);
    config = valid_config();
    config.max_connections = (uint8_t)(client->connection_capacity + 1);
    CHECK(provider->start(&config, &callbacks) == CRUX_ERR_OUT_OF_RANGE);

    /* Nothing was started by any of these. */
    CHECK(provider->stop() == CRUX_ERR_INVALID_STATE);
}

static void case_start_and_stop_lifecycle(const crux_ssh_test_client_t *client)
{
    const crux_ssh_provider_t *provider = client->provider;
    crux_ssh_config_t config = valid_config();
    crux_ssh_callbacks_t callbacks = all_callbacks();

    CHECK(provider->stop() == CRUX_ERR_INVALID_STATE);
    CHECK(provider->start(&config, &callbacks) == CRUX_OK);
    CHECK(provider->start(&config, &callbacks) == CRUX_ERR_INVALID_STATE);
    CHECK(provider->stop() == CRUX_OK);
    CHECK(provider->stop() == CRUX_ERR_INVALID_STATE);
    CHECK(provider->start(&config, &callbacks) == CRUX_OK);
    CHECK(provider->stop() == CRUX_OK);
}

static void case_start_copies_its_arguments(const crux_ssh_test_client_t *client)
{
    crux_ssh_config_t config = valid_config();
    crux_ssh_callbacks_t callbacks = all_callbacks();
    int connection;

    state.client = client;
    state.password_result = CRUX_SSH_AUTH_DENIED;
    CHECK(client->provider->start(&config, &callbacks) == CRUX_OK);
    /* The caller's structures can go out of scope. */
    memset(&callbacks, 0, sizeof(callbacks));
    memset(&config, 0, sizeof(config));

    connection = connect_ok(client, &peer_a);
    CHECK(!client->password(connection, "operator", 8, "wrong"));
    CHECK(count_events(EVENT_PASSWORD) == 1);
    CHECK(client->is_connected(connection));
}

/* ---- Passwords ---- */

static void case_password_accept_attaches(const crux_ssh_test_client_t *client)
{
    const event_t *event;
    int connection;

    start(client);
    connection = connect_ok(client, &peer_a);
    state.password_result = CRUX_SSH_AUTH_ACCEPTED;
    CHECK(client->password(connection, "operator", 8, "secret"));

    event = find_event(EVENT_PASSWORD);
    CHECK(event != NULL);
    if (event != NULL) {
        CHECK(strcmp(event->user, "operator") == 0);
        CHECK(event->peer.address_length == 4);
        CHECK(memcmp(event->peer.address, peer_a.address, 4) == 0);
    }
    CHECK(client->open_channel(connection, "session"));
}

static void case_password_denied_keeps_trying(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = connect_ok(client, &peer_a);
    state.password_result = CRUX_SSH_AUTH_DENIED;
    CHECK(!client->password(connection, "operator", 8, "wrong"));
    CHECK(client->is_connected(connection));
    /* A denial leaves no attachment, so no session opens and no closing call is owed. */
    CHECK(!client->open_channel(connection, "session"));
    client->disconnect(connection);
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 0);
}

static void case_unknown_result_denies(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = connect_ok(client, &peer_a);
    /* Deliberately out of range: a provider must treat any such value as a denial. */
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    state.password_result = (crux_ssh_auth_result_t)77;
    CHECK(!client->password(connection, "operator", 8, "secret"));
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 0);
    CHECK(!client->open_channel(connection, "session"));
}

static void case_accepted_without_attachment_denies(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = connect_ok(client, &peer_a);
    state.password_result = CRUX_SSH_AUTH_ACCEPTED;
    state.withhold_attachment = true;
    CHECK(!client->password(connection, "operator", 8, "secret"));
    CHECK(!client->open_channel(connection, "session"));
    state.key_check_result = CRUX_SSH_AUTH_ACCEPTED;
    state.key_verified_result = CRUX_SSH_AUTH_ACCEPTED;
    CHECK(!client->key_login(connection, "operator", ed25519, user_key, sizeof(user_key), true));
    CHECK(!client->open_channel(connection, "session"));
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 0);
}

static void case_bad_user_never_reaches_a_callback(const crux_ssh_test_client_t *client)
{
    static const char too_long[] = "a123456789012345678901234567890b"; /* 32 bytes */
    static const char longest[] = "a12345678901234567890123456789b";   /* 31 bytes */
    int connection;

    start(client);
    connection = connect_ok(client, &peer_a);
    state.password_result = CRUX_SSH_AUTH_DENIED;
    CHECK(!client->password(connection, "", 0, "secret"));
    CHECK(!client->password(connection, too_long, sizeof(too_long) - 1, "secret"));
    CHECK(count_events(EVENT_PASSWORD) == 0);
    client->disconnect(connection);

    connection = connect_ok(client, &peer_a);
    CHECK(!client->password(connection, "oper\0ator", 9, "secret"));
    CHECK(count_events(EVENT_PASSWORD) == 0);
    CHECK(!client->password(connection, longest, sizeof(longest) - 1, "secret"));
    CHECK(count_events(EVENT_PASSWORD) == 1);
}

static void case_attempts_are_bounded(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = connect_ok(client, &peer_a);
    state.password_result = CRUX_SSH_AUTH_DENIED;
    state.key_check_result = CRUX_SSH_AUTH_ACCEPTED;

    /* A bad user name, a failed signature and a wrong password each count. */
    CHECK(!client->password(connection, "", 0, "secret"));
    CHECK(client->is_connected(connection));
    CHECK(!client->key_login(connection, "operator", ed25519, user_key, sizeof(user_key), false));
    CHECK(client->is_connected(connection));
    /* A probe the server accepts is not a failure. */
    CHECK(client->key_probe(connection, "operator", ed25519, user_key, sizeof(user_key)));
    CHECK(client->is_connected(connection));
    CHECK(!client->password(connection, "operator", 8, "wrong"));
    CHECK(!client->is_connected(connection));

    /* A probe the server refuses counts, as a signed request does. */
    connection = connect_ok(client, &peer_a);
    state.key_check_result = CRUX_SSH_AUTH_DENIED;
    for (int i = 0; i < 2; ++i) {
        CHECK(!client->key_probe(connection, "operator", ed25519, user_key, sizeof(user_key)));
        CHECK(client->is_connected(connection));
    }
    CHECK(!client->key_probe(connection, "operator", ed25519, user_key, sizeof(user_key)));
    CHECK(!client->is_connected(connection));

    /* The slot is free again, and the count is per connection. */
    connection = connect_ok(client, &peer_a);
    CHECK(client->is_connected(connection));
}

static void case_auth_time_is_bounded(const crux_ssh_test_client_t *client)
{
    int connection;
    int authenticated;

    start(client);
    connection = connect_ok(client, &peer_a);
    client->pass_auth_time(connection);
    CHECK(!client->is_connected(connection));
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 0);

    /* The limit is for logging in, so it does not end an authenticated connection. */
    authenticated = session_connection(client, &peer_a);
    client->pass_auth_time(authenticated);
    CHECK(client->is_connected(authenticated));
}

static void case_connections_are_bounded(const crux_ssh_test_client_t *client)
{
    int first;
    int second;
    int refused = -1;
    int again;

    start(client);
    first = connect_ok(client, &peer_a);
    second = connect_ok(client, &peer_b);
    /* Unauthenticated connections hold slots as well, and a refusal reaches no callback. */
    CHECK(!client->connect(&peer_a, &refused));
    CHECK(state.event_count == 0);
    CHECK(client->is_connected(first));
    CHECK(client->is_connected(second));

    client->disconnect(first);
    again = connect_ok(client, &peer_a);
    CHECK(client->is_connected(again));
    CHECK(client->is_connected(second));
}

/* ---- Public keys ---- */

static void case_key_types(const crux_ssh_test_client_t *client)
{
    const event_t *event;
    int connection;

    start(client);
    state.key_check_result = CRUX_SSH_AUTH_ACCEPTED;
    connection = connect_ok(client, &peer_a);
    CHECK(client->key_probe(connection, "operator", ed25519, user_key, sizeof(user_key)));
    event = find_event(EVENT_KEY_CHECK);
    CHECK(event != NULL && event->key_type == CRUX_SSH_KEY_ED25519);

    state.event_count = 0;
    CHECK(client->key_probe(connection, "operator", ecdsa, user_key, sizeof(user_key)));
    event = find_event(EVENT_KEY_CHECK);
    CHECK(event != NULL && event->key_type == CRUX_SSH_KEY_ECDSA_P256);
}

static void case_rsa_keys_are_refused(const crux_ssh_test_client_t *client)
{
    static const char *const refused[] = {"ssh-rsa", "rsa-sha2-256",        "rsa-sha2-512",
                                          "ssh-dss", "ecdsa-sha2-nistp384", "unknown"};
    crux_ssh_config_t config = valid_config();
    crux_ssh_callbacks_t callbacks = all_callbacks();
    int connection;

    /* Each refusal counts as an attempt, so this case allows for all of them. */
    config.max_auth_attempts = 10;
    CHECK(client->provider->start(&config, &callbacks) == CRUX_OK);
    state.key_check_result = CRUX_SSH_AUTH_ACCEPTED;
    connection = connect_ok(client, &peer_a);
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        CHECK(!client->key_probe(connection, "operator", refused[i], user_key, sizeof(user_key)));
    }
    CHECK(state.event_count == 0);
    CHECK(!client->key_login(connection, "operator", "rsa-sha2-256", user_key, sizeof(user_key),
                             true));
    CHECK(state.event_count == 0);
}

static void case_probe_never_creates_a_session(const crux_ssh_test_client_t *client)
{
    const event_t *event;
    int connection;

    start(client);
    state.key_check_result = CRUX_SSH_AUTH_ACCEPTED;
    state.key_verified_result = CRUX_SSH_AUTH_ACCEPTED;
    connection = connect_ok(client, &peer_a);
    CHECK(client->key_probe(connection, "operator", ed25519, user_key, sizeof(user_key)));

    event = find_event(EVENT_KEY_CHECK);
    CHECK(event != NULL);
    if (event != NULL) {
        CHECK(!event->signed_request);
        CHECK(event->key_length == sizeof(user_key));
        CHECK(memcmp(event->key, user_key, sizeof(user_key)) == 0);
        CHECK(strcmp(event->user, "operator") == 0);
    }
    CHECK(count_events(EVENT_KEY_VERIFIED) == 0);
    CHECK(!client->open_channel(connection, "session"));

    /* A key the account does not hold is not accepted by the probe either. */
    state.key_check_result = CRUX_SSH_AUTH_DENIED;
    CHECK(!client->key_probe(connection, "operator", ed25519, user_key, sizeof(user_key)));
}

static void case_session_only_after_verified_signature(const crux_ssh_test_client_t *client)
{
    const event_t *check;
    const event_t *verified;
    int connection;

    start(client);
    state.key_check_result = CRUX_SSH_AUTH_ACCEPTED;
    state.key_verified_result = CRUX_SSH_AUTH_ACCEPTED;
    connection = connect_ok(client, &peer_a);
    CHECK(client->key_login(connection, "operator", ecdsa, user_key, sizeof(user_key), true));

    check = find_event(EVENT_KEY_CHECK);
    verified = find_event(EVENT_KEY_VERIFIED);
    CHECK(check != NULL && verified != NULL);
    if (check != NULL && verified != NULL) {
        CHECK(check->signed_request);
        CHECK(check < verified); /* The check comes first, and only then the verified result. */
        CHECK(verified->key_type == CRUX_SSH_KEY_ECDSA_P256);
        CHECK(verified->key_length == sizeof(user_key));
        CHECK(memcmp(verified->key, user_key, sizeof(user_key)) == 0);
        CHECK(strcmp(verified->user, "operator") == 0);
    }
    CHECK(count_events(EVENT_KEY_FAILED) == 0);
    CHECK(client->open_channel(connection, "session"));
}

static void case_failed_signature_reports_and_denies(const crux_ssh_test_client_t *client)
{
    const event_t *failed;
    int connection;

    start(client);
    state.key_check_result = CRUX_SSH_AUTH_ACCEPTED;
    state.key_verified_result = CRUX_SSH_AUTH_ACCEPTED;
    connection = connect_ok(client, &peer_a);
    CHECK(!client->key_login(connection, "operator", ed25519, user_key, sizeof(user_key), false));

    failed = find_event(EVENT_KEY_FAILED);
    CHECK(failed != NULL);
    if (failed != NULL) {
        CHECK(strcmp(failed->user, "operator") == 0);
        CHECK(memcmp(failed->peer.address, peer_a.address, 4) == 0);
    }
    CHECK(count_events(EVENT_KEY_VERIFIED) == 0);
    CHECK(!client->open_channel(connection, "session"));
}

static void case_unlisted_key_stops_at_the_check(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    state.key_check_result = CRUX_SSH_AUTH_DENIED;
    state.key_verified_result = CRUX_SSH_AUTH_ACCEPTED;
    connection = connect_ok(client, &peer_a);
    CHECK(!client->key_login(connection, "operator", ed25519, user_key, sizeof(user_key), true));
    CHECK(count_events(EVENT_KEY_CHECK) == 1);
    /* The access adapter counts this failure from the signed check, so neither of these runs. */
    CHECK(count_events(EVENT_KEY_VERIFIED) == 0);
    CHECK(count_events(EVENT_KEY_FAILED) == 0);
    CHECK(!client->open_channel(connection, "session"));
}

static void case_verified_refusal_denies(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    state.key_check_result = CRUX_SSH_AUTH_ACCEPTED;
    state.key_verified_result = CRUX_SSH_AUTH_DENIED;
    connection = connect_ok(client, &peer_a);
    CHECK(!client->key_login(connection, "operator", ed25519, user_key, sizeof(user_key), true));
    CHECK(count_events(EVENT_KEY_VERIFIED) == 1);
    CHECK(!client->open_channel(connection, "session"));
    client->disconnect(connection);
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 0);
}

/* ---- Requests ---- */

static void case_no_request_before_authentication(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = connect_ok(client, &peer_a);
    CHECK(!client->open_channel(connection, "session"));
    CHECK(!client->channel_request(connection, "shell", NULL, 0));
    CHECK(!client->channel_request(connection, "exec", "status", 6));
    CHECK(!client->channel_request(connection, "subsystem", "sftp", 4));
    CHECK(state.event_count == 0);
}

static void case_shell_runs_and_returns_status(const crux_ssh_test_client_t *client)
{
    int connection;
    int status = -1;
    const event_t *event;

    start(client);
    connection = session_connection(client, &peer_a);
    state.shell_status = 7;
    /* A terminal request is accepted with no callback, and starts no session. */
    CHECK(client->channel_request(connection, "pty-req", NULL, 0));
    CHECK(client->channel_request(connection, "window-change", NULL, 0));
    CHECK(count_events(EVENT_SHELL) == 0);
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    event = find_event(EVENT_SHELL);
    CHECK(event != NULL && event->attachment_id == 1);
    CHECK(client->exit_status(connection, &status));
    CHECK(status == 7);
    CHECK(client->received_eof(connection));
    /* One session per connection: the connection ends with it. */
    CHECK(!client->is_connected(connection));
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 1);
}

static void case_exit_status_is_clamped(const crux_ssh_test_client_t *client)
{
    int connection;
    int status = -1;

    start(client);
    connection = session_connection(client, &peer_a);
    state.shell_status = 300;
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    CHECK(client->exit_status(connection, &status));
    CHECK(status == 255);

    client->disconnect(connection);
    connection = session_connection(client, &peer_a);
    state.shell_status = -1;
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    CHECK(client->exit_status(connection, &status));
    CHECK(status == 255);
}

static void case_exec_receives_the_command(const crux_ssh_test_client_t *client)
{
    int connection;
    int status = -1;
    const event_t *event;

    start(client);
    connection = session_connection(client, &peer_a);
    state.shell_status = 3;
    CHECK(client->channel_request(connection, "exec", "config get name", 15));
    event = find_event(EVENT_EXEC);
    CHECK(event != NULL && event->attachment_id == 1);
    CHECK(state.last_command_length == 15);
    CHECK(strcmp(state.last_command, "config get name") == 0);
    CHECK(client->exit_status(connection, &status));
    CHECK(status == 3);
}

static void case_exec_with_embedded_nul_is_refused(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = session_connection(client, &peer_a);
    CHECK(!client->channel_request(connection, "exec", "status\0reboot", 13));
    CHECK(count_events(EVENT_EXEC) == 0);
}

static void case_other_requests_are_refused(const crux_ssh_test_client_t *client)
{
    static const char *const channel_requests[] = {"x11-req", "auth-agent-req@openssh.com",
                                                   "signal", "subsystem-unknown", "env"};
    static const char *const global_requests[] = {"tcpip-forward", "cancel-tcpip-forward",
                                                  "streamlocal-forward@openssh.com"};
    static const char *const channels[] = {"direct-tcpip", "forwarded-tcpip", "x11",
                                           "direct-streamlocal@openssh.com", "unknown"};
    int connection;
    size_t before;

    start(client);
    connection = session_connection(client, &peer_a);
    before = state.event_count;
    for (size_t i = 0; i < sizeof(channel_requests) / sizeof(channel_requests[0]); ++i) {
        CHECK(!client->channel_request(connection, channel_requests[i], NULL, 0));
    }
    for (size_t i = 0; i < sizeof(global_requests) / sizeof(global_requests[0]); ++i) {
        CHECK(!client->global_request(connection, global_requests[i]));
    }
    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i) {
        CHECK(!client->open_channel(connection, channels[i]));
    }
    CHECK(!client->channel_request(connection, "subsystem", "scp", 3));
    CHECK(!client->channel_request(connection, "subsystem", "shell", 5));
    CHECK(state.event_count == before);
    /* None of these disturbed the session. */
    CHECK(client->is_connected(connection));
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    CHECK(count_events(EVENT_SHELL) == 1);
}

static void case_one_session_channel_per_connection(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = session_connection(client, &peer_a);
    CHECK(!client->open_channel(connection, "session"));
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    /* The session is over once its callback returns, and the connection with it. */
    CHECK(!client->channel_request(connection, "exec", "status", 6));
    CHECK(!client->open_channel(connection, "session"));
    CHECK(count_events(EVENT_EXEC) == 0);
}

static void case_connections_keep_their_own_attachment(const crux_ssh_test_client_t *client)
{
    int first;
    int second;
    const event_t *event;

    start(client);
    first = session_connection(client, &peer_a);
    second = session_connection(client, &peer_b);
    CHECK(client->channel_request(second, "shell", NULL, 0));
    event = find_event(EVENT_SHELL);
    CHECK(event != NULL && event->attachment_id == 2);
    state.event_count = 0;
    CHECK(client->channel_request(first, "shell", NULL, 0));
    event = find_event(EVENT_SHELL);
    CHECK(event != NULL && event->attachment_id == 1);
}

/* ---- Channel read and write ---- */

static char session_input[32];
static size_t session_input_length;
static crux_status_t session_status[4];
static size_t session_transferred[4];

static int session_reads_in_pieces(crux_ssh_channel_t *channel)
{
    const crux_ssh_provider_t *provider = state.client->provider;
    char piece[4];
    size_t count = 0;

    session_input_length = 0;
    for (size_t i = 0; i < 4; ++i) {
        session_status[i] = provider->read(channel, piece, sizeof(piece), &count);
        session_transferred[i] = count;
        if (session_status[i] == CRUX_OK && session_input_length + count <= sizeof(session_input)) {
            memcpy(session_input + session_input_length, piece, count);
            session_input_length += count;
        }
    }
    return 0;
}

static void case_read_delivers_then_reports_eof(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = session_connection(client, &peer_a);
    client->send(connection, "hello", 5);
    client->send_eof(connection);
    state.session = session_reads_in_pieces;
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    CHECK(session_status[0] == CRUX_OK);
    CHECK(session_transferred[0] >= 1 && session_transferred[0] <= 4);
    CHECK(session_status[1] == CRUX_OK);
    CHECK(session_input_length == 5);
    CHECK(memcmp(session_input, "hello", 5) == 0);
    CHECK(session_status[2] == CRUX_ERR_NO_DATA);
    CHECK(session_status[3] == CRUX_ERR_NO_DATA);
}

static int session_reads_nothing(crux_ssh_channel_t *channel)
{
    const crux_ssh_provider_t *provider = state.client->provider;
    char buffer[4];
    size_t count = 99;

    session_status[0] = provider->read(channel, buffer, sizeof(buffer), &count);
    session_transferred[1] = count;
    session_status[1] = provider->read(channel, NULL, sizeof(buffer), &count);
    session_status[2] = provider->read(channel, buffer, 0, &count);
    session_status[3] = provider->read(channel, buffer, sizeof(buffer), NULL);
    session_transferred[0] =
        provider->read(NULL, buffer, sizeof(buffer), &count) == CRUX_ERR_INVALID_ARGUMENT ? 1 : 0;
    return 0;
}

static void case_read_times_out_and_checks_arguments(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = session_connection(client, &peer_a);
    state.session = session_reads_nothing;
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    CHECK(session_status[0] == CRUX_ERR_TIMEOUT);
    CHECK(session_transferred[1] == 0);
    CHECK(session_status[1] == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(session_status[2] == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(session_status[3] == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(session_transferred[0] == 1);
}

static int session_writes_into_a_window(crux_ssh_channel_t *channel)
{
    const crux_ssh_provider_t *provider = state.client->provider;
    size_t count = 0;

    session_status[0] = provider->write(channel, "abcdef", 6, &count);
    session_transferred[0] = count;
    count = 99;
    session_status[1] = provider->write(channel, "ghi", 3, &count);
    session_transferred[1] = count;
    session_status[2] = provider->write(channel, NULL, 3, &count);
    session_status[3] = provider->write(channel, "ghi", 0, &count);
    session_transferred[2] = provider->write(channel, "ghi", 3, NULL) == CRUX_ERR_INVALID_ARGUMENT;
    session_transferred[3] = provider->write(NULL, "ghi", 3, &count) == CRUX_ERR_INVALID_ARGUMENT;
    return 0;
}

static void case_write_respects_the_window(const crux_ssh_test_client_t *client)
{
    char output[16] = {0};
    int connection;

    start(client);
    connection = session_connection(client, &peer_a);
    client->set_window(connection, 4);
    state.session = session_writes_into_a_window;
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    /* A short write is success, and a full window is a timeout that leaves the channel usable. */
    CHECK(session_status[0] == CRUX_OK);
    CHECK(session_transferred[0] == 4);
    CHECK(session_status[1] == CRUX_ERR_TIMEOUT);
    CHECK(session_transferred[1] == 0);
    CHECK(session_status[2] == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(session_status[3] == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(session_transferred[2] == 1);
    CHECK(session_transferred[3] == 1);
    CHECK(client->received(connection, output, sizeof(output)) == 4);
    CHECK(memcmp(output, "abcd", 4) == 0);
}

static void case_slow_client_leaves_others_alone(const crux_ssh_test_client_t *client)
{
    char output[16] = {0};
    int slow;
    int other;

    start(client);
    slow = session_connection(client, &peer_a);
    other = session_connection(client, &peer_b);
    client->set_window(slow, 0);
    state.session = session_writes_into_a_window;
    CHECK(client->channel_request(slow, "shell", NULL, 0));
    CHECK(session_status[0] == CRUX_ERR_TIMEOUT);
    CHECK(client->received(slow, output, sizeof(output)) == 0);

    /* A window belongs to one connection. */
    CHECK(client->channel_request(other, "shell", NULL, 0));
    CHECK(session_status[0] == CRUX_OK);
    CHECK(client->received(other, output, sizeof(output)) >= 6);
    CHECK(memcmp(output, "abcdef", 6) == 0);
}

static int session_writes_after_drop(crux_ssh_channel_t *channel)
{
    size_t count = 99;

    session_status[0] = state.client->provider->write(channel, "abc", 3, &count);
    session_transferred[0] = count;
    return 0;
}

static void case_write_reports_a_dropped_connection(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = session_connection(client, &peer_a);
    client->drop_on_write(connection);
    state.session = session_writes_after_drop;
    client->channel_request(connection, "shell", NULL, 0);
    CHECK(session_status[0] == CRUX_ERR_IO);
    CHECK(!client->is_connected(connection));
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 1);
}

static int session_closes_first(crux_ssh_channel_t *channel)
{
    const crux_ssh_provider_t *provider = state.client->provider;
    char buffer[4];
    size_t count = 0;

    session_status[0] = provider->close(channel);
    session_status[1] = provider->close(channel);
    session_status[2] = provider->read(channel, buffer, sizeof(buffer), &count);
    session_status[3] = provider->write(channel, "x", 1, &count);
    session_transferred[0] = provider->close(NULL) == CRUX_ERR_INVALID_ARGUMENT;
    return 5;
}

static void case_close_ends_this_side(const crux_ssh_test_client_t *client)
{
    int connection;
    int status = -1;

    start(client);
    connection = session_connection(client, &peer_a);
    client->send(connection, "hello", 5);
    state.session = session_closes_first;
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    CHECK(session_status[0] == CRUX_OK);
    CHECK(session_status[1] == CRUX_OK);
    CHECK(session_status[2] == CRUX_ERR_INVALID_STATE);
    CHECK(session_status[3] == CRUX_ERR_INVALID_STATE);
    CHECK(session_transferred[0] == 1);
    CHECK(client->received_eof(connection));
    /* The exit status still follows the close. */
    CHECK(client->exit_status(connection, &status));
    CHECK(status == 5);
}

/* ---- SFTP and its file handle ---- */

static void case_sftp_attaches_a_file_handle(const crux_ssh_test_client_t *client)
{
    const event_t *opened;
    const event_t *closed;
    int connection;
    crux_ssh_file_handle_t *handle;

    start(client);
    connection = session_connection(client, &peer_a);
    CHECK(client->file_handle(connection) == NULL);
    CHECK(client->channel_request(connection, "subsystem", "sftp", 4));
    opened = find_event(EVENT_SFTP_OPEN);
    CHECK(opened != NULL && opened->attachment_id == 1);
    handle = client->file_handle(connection);
    /* The handle the library would pass to each file operation is the one Crux returned. */
    CHECK(handle == &state.handles[0]);
    CHECK(count_events(EVENT_SFTP_CLOSE) == 0);
    /* The connection has its session, so a further one is refused. */
    CHECK(!client->channel_request(connection, "shell", NULL, 0));
    CHECK(count_events(EVENT_SHELL) == 0);

    client->end_sftp(connection);
    closed = find_event(EVENT_SFTP_CLOSE);
    CHECK(closed != NULL && closed->attachment_id == 1);
    CHECK(client->file_handle(connection) == NULL);
    CHECK(count_events(EVENT_SFTP_CLOSE) == 1);
    CHECK(!client->is_connected(connection));
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 1);
}

static void case_sftp_handles_are_per_session(const crux_ssh_test_client_t *client)
{
    int first;
    int second;

    start(client);
    first = session_connection(client, &peer_a);
    second = session_connection(client, &peer_b);
    CHECK(client->channel_request(first, "subsystem", "sftp", 4));
    CHECK(client->channel_request(second, "subsystem", "sftp", 4));
    CHECK(client->file_handle(first) == &state.handles[0]);
    CHECK(client->file_handle(second) == &state.handles[1]);
}

static void case_sftp_open_failure_closes_the_channel(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = session_connection(client, &peer_a);
    state.sftp_open_status = CRUX_ERR_NOT_READY;
    CHECK(!client->channel_request(connection, "subsystem", "sftp", 4));
    CHECK(client->file_handle(connection) == NULL);
    /* Nothing opened, so nothing is owed. The attachment is still released. */
    CHECK(count_events(EVENT_SFTP_CLOSE) == 0);
    CHECK(!client->is_connected(connection));
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 1);
}

static void case_disconnect_during_sftp_closes_in_order(const crux_ssh_test_client_t *client)
{
    int connection;
    size_t closed_at = 0;
    size_t sftp_closed_at = 0;

    start(client);
    connection = session_connection(client, &peer_a);
    CHECK(client->channel_request(connection, "subsystem", "sftp", 4));
    client->disconnect(connection);
    for (size_t i = 0; i < state.event_count; ++i) {
        if (state.events[i].kind == EVENT_SFTP_CLOSE) {
            sftp_closed_at = i + 1;
        }
        if (state.events[i].kind == EVENT_CONNECTION_CLOSED) {
            closed_at = i + 1;
        }
    }
    CHECK(sftp_closed_at != 0);
    CHECK(closed_at != 0);
    CHECK(sftp_closed_at < closed_at);
    CHECK(count_events(EVENT_SFTP_CLOSE) == 1);
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 1);
}

/* ---- End of a connection ---- */

static void case_connection_closed_runs_once(const crux_ssh_test_client_t *client)
{
    const event_t *event;
    int connection;

    start(client);
    connection = session_connection(client, &peer_a);
    /* The client leaves while the connection has no session running. */
    client->disconnect(connection);
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 1);
    event = find_event(EVENT_CONNECTION_CLOSED);
    CHECK(event != NULL && event->attachment_id == 1);
    /* Stopping afterwards owes nothing further. */
    CHECK(client->provider->stop() == CRUX_OK);
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 1);
}

static void case_stop_ends_connections(const crux_ssh_test_client_t *client)
{
    int authenticated;
    int unauthenticated;
    int refused = -1;

    start(client);
    authenticated = session_connection(client, &peer_a);
    unauthenticated = connect_ok(client, &peer_b);
    CHECK(client->provider->stop() == CRUX_OK);
    CHECK(!client->is_connected(authenticated));
    CHECK(!client->is_connected(unauthenticated));
    CHECK(count_events(EVENT_CONNECTION_CLOSED) == 1);
    CHECK(!client->connect(&peer_a, &refused));
}

static void case_stop_from_a_callback_is_refused(const crux_ssh_test_client_t *client)
{
    int connection;

    start(client);
    connection = session_connection(client, &peer_a);
    state.try_stop_in_session = true;
    state.stop_from_callback = CRUX_OK;
    CHECK(client->channel_request(connection, "shell", NULL, 0));
    CHECK(state.stop_from_callback == CRUX_ERR_INVALID_STATE);
    /* The refused call changed nothing, so a stop from outside still works. */
    CHECK(client->provider->stop() == CRUX_OK);
}

#define CASE(name) {#name, case_##name}

const crux_ssh_conformance_case_t crux_ssh_conformance_cases[] = {
    CASE(start_rejects_bad_arguments),
    CASE(start_and_stop_lifecycle),
    CASE(start_copies_its_arguments),
    CASE(password_accept_attaches),
    CASE(password_denied_keeps_trying),
    CASE(unknown_result_denies),
    CASE(accepted_without_attachment_denies),
    CASE(bad_user_never_reaches_a_callback),
    CASE(attempts_are_bounded),
    CASE(auth_time_is_bounded),
    CASE(connections_are_bounded),
    CASE(key_types),
    CASE(rsa_keys_are_refused),
    CASE(probe_never_creates_a_session),
    CASE(session_only_after_verified_signature),
    CASE(failed_signature_reports_and_denies),
    CASE(unlisted_key_stops_at_the_check),
    CASE(verified_refusal_denies),
    CASE(no_request_before_authentication),
    CASE(shell_runs_and_returns_status),
    CASE(exit_status_is_clamped),
    CASE(exec_receives_the_command),
    CASE(exec_with_embedded_nul_is_refused),
    CASE(other_requests_are_refused),
    CASE(one_session_channel_per_connection),
    CASE(connections_keep_their_own_attachment),
    CASE(read_delivers_then_reports_eof),
    CASE(read_times_out_and_checks_arguments),
    CASE(write_respects_the_window),
    CASE(slow_client_leaves_others_alone),
    CASE(write_reports_a_dropped_connection),
    CASE(close_ends_this_side),
    CASE(sftp_attaches_a_file_handle),
    CASE(sftp_handles_are_per_session),
    CASE(sftp_open_failure_closes_the_channel),
    CASE(disconnect_during_sftp_closes_in_order),
    CASE(connection_closed_runs_once),
    CASE(stop_ends_connections),
    CASE(stop_from_a_callback_is_refused),
};
const size_t crux_ssh_conformance_case_count =
    sizeof(crux_ssh_conformance_cases) / sizeof(crux_ssh_conformance_cases[0]);

unsigned crux_ssh_conformance_run_case(const crux_ssh_conformance_case_t *test,
                                       const crux_ssh_test_client_t *client,
                                       crux_ssh_conformance_reset_t reset, bool verbose)
{
    reset();
    memset(&state, 0, sizeof(state));
    memset(session_status, 0, sizeof(session_status));
    memset(session_transferred, 0, sizeof(session_transferred));
    memset(session_input, 0, sizeof(session_input));
    session_input_length = 0;
    failures = 0;
    verbose_output = verbose;
    current_case = test->name;
    state.client = client;
    test->run(client);
    return failures;
}
