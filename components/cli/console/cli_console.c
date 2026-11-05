/* The login loop. It is outside the registry, and it runs over any transport, so its logic can be
 * tested on Host without a port. Nothing about the device appears before login. */
#include "cli_console.h"

#include "cli_internal.h"

#include <string.h>

#define SETUP_WORD "!setup"

static const crux_access_origin_t serial_origin = {CRUX_ACCESS_CHANNEL_SERIAL, {0}, 0};
static const crux_access_session_t no_session = {0};

static void clear_input(crux_cli_context_t *context)
{
    crux_cli_secure_zero(context->line, sizeof(context->line));
    crux_cli_secure_zero(context->reserved.secret, sizeof(context->reserved.secret));
}

static crux_cli_result_t say(const crux_cli_io_t *io, const char *text)
{
    return crux_cli_send_text(io, no_session, text);
}

/* What to do with the result of a read at a prompt that has no later step. */
static crux_cli_result_t prompt_failed(crux_cli_read_t input)
{
    return input == CRUX_CLI_READ_TRANSPORT ? CRUX_CLI_ERR_OUTPUT : CRUX_CLI_OK;
}

/* Reads one hidden entry. An entry that cannot fit reads as CRUX_CLI_READ_TOO_LONG. */
static crux_cli_read_t read_password(crux_cli_context_t *context, const crux_cli_io_t *io,
                                     const char *prompt, size_t slot, size_t *length)
{
    if (say(io, prompt) != CRUX_CLI_OK) {
        return CRUX_CLI_READ_TRANSPORT;
    }
    return crux_cli_read_hidden(context, io, no_session, context->reserved.secret[slot],
                                sizeof(context->reserved.secret[slot]), length);
}

/* One pass of first-account setup. *again asks for the setup prompt rather than the login prompt.
 * Access alone decides whether setup is open, and the prompts never announce it. */
static crux_cli_result_t setup_once(crux_cli_context_t *context, const crux_cli_io_t *io,
                                    const char *setup_role, bool *again)
{
    size_t name_length = 0;
    size_t first_length = 0;
    size_t second_length = 0;
    *again = false;

    if (say(io, "new user: ") != CRUX_CLI_OK) {
        return CRUX_CLI_ERR_OUTPUT;
    }
    crux_cli_read_t input = crux_cli_read_line(context, io, no_session, &name_length);
    if (input == CRUX_CLI_READ_TOO_LONG || (input == CRUX_CLI_READ_OK && name_length == 0)) {
        *again = true;
        return say(io, "invalid name\n");
    }
    if (input != CRUX_CLI_READ_OK) {
        return prompt_failed(input);
    }
    input = read_password(context, io, "password: ", 0, &first_length);
    bool first_fits = input != CRUX_CLI_READ_TOO_LONG;
    if (first_fits && input != CRUX_CLI_READ_OK) {
        return prompt_failed(input);
    }
    input = read_password(context, io, "repeat: ", 1, &second_length);
    bool second_fits = input != CRUX_CLI_READ_TOO_LONG;
    if (second_fits && input != CRUX_CLI_READ_OK) {
        return prompt_failed(input);
    }
    /* An entry no account can hold is the rule's refusal, as it would be from Access. */
    if (!first_fits || !second_fits) {
        *again = true;
        return crux_cli_send_password_rejected(io, no_session);
    }
    if (first_length != second_length ||
        memcmp(context->reserved.secret[0], context->reserved.secret[1], first_length) != 0) {
        *again = true;
        return say(io, CRUX_CLI_TEXT_DIFFER);
    }
    crux_access_result_t answer = crux_access_provision_first_account(
        context->line, context->reserved.secret[0], first_length, setup_role, &serial_origin);
    clear_input(context);
    switch (answer) {
    case CRUX_ACCESS_OK:
        return say(io, "account created\n");
    case CRUX_ACCESS_ERR_PASSWORD_REJECTED:
        *again = true;
        return crux_cli_send_password_rejected(io, no_session);
    case CRUX_ACCESS_ERR_INVALID_ARGUMENT:
        *again = true;
        return say(io, "invalid name\n");
    case CRUX_ACCESS_ERR_STORE:
        return say(io, CRUX_CLI_TEXT_STORE);
    case CRUX_ACCESS_ERR_NOT_READY:
        return say(io, CRUX_CLI_TEXT_LOGIN_UNAVAILABLE);
    default:
        return say(io, "setup not available\n");
    }
}

static crux_cli_result_t run_setup(crux_cli_context_t *context, const crux_cli_io_t *io,
                                   const char *setup_role)
{
    bool again = true;
    crux_cli_result_t result = CRUX_CLI_OK;
    while (again && result == CRUX_CLI_OK) {
        clear_input(context);
        result = setup_once(context, io, setup_role, &again);
    }
    clear_input(context);
    return result;
}

static crux_cli_result_t report_login(const crux_cli_io_t *io, crux_access_result_t answer)
{
    switch (answer) {
    case CRUX_ACCESS_ERR_BUSY:
        return say(io, CRUX_CLI_TEXT_BUSY);
    case CRUX_ACCESS_ERR_NO_RESOURCES:
        return say(io, "no session available\n");
    case CRUX_ACCESS_ERR_NOT_READY:
        return say(io, CRUX_CLI_TEXT_LOGIN_UNAVAILABLE);
    default:
        /* A wrong password, an unknown name and a disabled account give the same output. */
        return say(io, "login failed\n");
    }
}

static crux_cli_result_t login_once(crux_cli_context_t *context, const crux_cli_io_t *io,
                                    const char *setup_role)
{
    size_t name_length = 0;
    size_t password_length = 0;
    if (say(io, "login: ") != CRUX_CLI_OK) {
        return CRUX_CLI_ERR_OUTPUT;
    }
    crux_cli_read_t input = crux_cli_read_line(context, io, no_session, &name_length);
    if (input != CRUX_CLI_READ_OK && input != CRUX_CLI_READ_TOO_LONG) {
        return prompt_failed(input);
    }
    if (input == CRUX_CLI_READ_OK && name_length == 0) {
        return CRUX_CLI_OK;
    }
    if (input == CRUX_CLI_READ_OK && strcmp(context->line, SETUP_WORD) == 0) {
        return run_setup(context, io, setup_role);
    }
    /* A name no account can hold still reaches the password prompt, and access control refuses it
     * by form. An overlong name was only partly kept, so it goes as an empty one. */
    if (input == CRUX_CLI_READ_TOO_LONG) {
        context->line[0] = '\0';
    }
    input = read_password(context, io, "password: ", 0, &password_length);
    if (input == CRUX_CLI_READ_TOO_LONG) {
        /* Denied by its length alone. The bytes are already gone. */
        password_length = sizeof(context->reserved.secret[0]);
    } else if (input != CRUX_CLI_READ_OK) {
        return prompt_failed(input);
    }
    if (!crux_cli_registry_ready()) {
        return say(io, CRUX_CLI_TEXT_LOGIN_UNAVAILABLE);
    }
    crux_access_session_t session = {0};
    uint32_t retry_after_ms = 0;
    crux_access_result_t answer =
        crux_access_login_password(context->line, context->reserved.secret[0], password_length,
                                   &serial_origin, &session, &retry_after_ms);
    clear_input(context);
    if (answer == CRUX_ACCESS_ERR_RETRY_LATER) {
        /* The console does not sleep on the delay, because the contract never sleeps. */
        return crux_cli_send_retry(io, no_session, retry_after_ms);
    }
    if (answer != CRUX_ACCESS_OK) {
        return report_login(io, answer);
    }
    crux_cli_result_t ended =
        crux_cli_session_loop(context, io, CRUX_ACCESS_CHANNEL_SERIAL, session);
    crux_access_logout(&session);
    return ended == CRUX_CLI_ERR_OUTPUT ? CRUX_CLI_ERR_OUTPUT : CRUX_CLI_OK;
}

crux_cli_result_t crux_cli_console_serve(crux_cli_context_t *context, const crux_cli_io_t *io,
                                         const char *setup_role)
{
    if (context == NULL || io == NULL || io->read == NULL || io->write == NULL ||
        setup_role == NULL) {
        return CRUX_CLI_ERR_INVALID_ARGUMENT;
    }
    crux_cli_input_reset(context);
    crux_cli_result_t result = CRUX_CLI_OK;
    while (result == CRUX_CLI_OK) {
        result = login_once(context, io, setup_role);
        clear_input(context);
    }
    return result;
}
