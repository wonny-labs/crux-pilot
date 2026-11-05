#include "cli_internal.h"

#include <string.h>

static const char prompt_text[] = "> ";

static unsigned channel_flag(crux_access_channel_t channel)
{
    switch (channel) {
    case CRUX_ACCESS_CHANNEL_SERIAL:
        return CRUX_CLI_CHANNEL_SERIAL;
    case CRUX_ACCESS_CHANNEL_SSH:
        return CRUX_CLI_CHANNEL_SSH;
    case CRUX_ACCESS_CHANNEL_WEB:
        break;
    }
    return 0;
}

static void clear_line(crux_cli_context_t *context)
{
    crux_cli_secure_zero(context->line, sizeof(context->line));
}

void crux_cli_call_say(crux_cli_call_t *call, const char *text)
{
    if (!call->output_failed && crux_cli_send_text(call->io, call->session, text) != CRUX_CLI_OK) {
        call->output_failed = true;
    }
}

crux_cli_result_t crux_cli_call_report(crux_cli_call_t *call, const char *text, int exit_status)
{
    if (text != NULL) {
        crux_cli_call_say(call, text);
    }
    call->reported = true;
    call->exit_status = exit_status;
    return call->output_failed ? CRUX_CLI_ERR_OUTPUT : CRUX_CLI_OK;
}

static void emit_rejected(const crux_cli_call_t *call, crux_cli_event_outcome_t outcome)
{
    crux_cli_event_t event;
    memset(&event, 0, sizeof(event));
    event.type = CRUX_CLI_EVENT_LINE_REJECTED;
    event.outcome = outcome;
    event.channel = (uint8_t)call->channel;
    if (crux_access_session_user(call->session, event.user, sizeof(event.user)) != CRUX_ACCESS_OK) {
        event.user[0] = '\0';
    }
    crux_cli_port_event(&event);
}

/* Rule L3: everything a session cannot run gives these words and this status. */
static void reject_as_unknown(crux_cli_call_t *call)
{
    crux_cli_call_say(call, CRUX_CLI_TEXT_UNKNOWN);
    call->exit_status = CRUX_CLI_EXIT_UNKNOWN;
}

static void say_usage(crux_cli_call_t *call, const crux_cli_command_t *command)
{
    crux_cli_call_say(call, "usage: ");
    crux_cli_call_say(call, command->name);
    crux_cli_call_say(call, ": ");
    crux_cli_call_say(call, command->summary);
    crux_cli_call_say(call, "\n");
    call->exit_status = CRUX_CLI_EXIT_USAGE;
}

/* One fixed line per result, with no internal path or native error. */
static void finish_command(crux_cli_call_t *call, const crux_cli_command_t *command,
                           crux_cli_result_t result)
{
    if (result == CRUX_CLI_ERR_OUTPUT) {
        /* The session being over is the better account, if that is why it stopped. */
        call->output_failed = call->output_failed || !call->session_lost;
        call->exit_status = CRUX_CLI_EXIT_FAILED;
        return;
    }
    if (call->reported) {
        return;
    }
    call->exit_status = CRUX_CLI_EXIT_FAILED;
    switch (result) {
    case CRUX_CLI_OK:
        call->exit_status = CRUX_CLI_EXIT_OK;
        break;
    case CRUX_CLI_ERR_CANCELLED:
        break;
    case CRUX_CLI_ERR_USAGE:
        say_usage(call, command);
        break;
    case CRUX_CLI_ERR_TOO_LONG:
        crux_cli_call_say(call, CRUX_CLI_TEXT_INPUT_TOO_LONG);
        call->exit_status = CRUX_CLI_EXIT_USAGE;
        break;
    case CRUX_CLI_ERR_DENIED:
        crux_cli_call_say(call, CRUX_CLI_TEXT_DENIED);
        break;
    case CRUX_CLI_ERR_NOT_READY:
        crux_cli_call_say(call, CRUX_CLI_TEXT_UNAVAILABLE);
        break;
    default:
        crux_cli_call_say(call, CRUX_CLI_TEXT_FAILED);
        break;
    }
}

/* The checks of a known command, which all end the same way for the user. */
static bool command_may_run(crux_cli_call_t *call, const crux_cli_command_t *command)
{
    if ((command->flags & channel_flag(call->channel)) == 0) {
        reject_as_unknown(call);
        emit_rejected(call, CRUX_CLI_EVENT_DENIED);
        return false;
    }
    /* The registry applies this itself for every class, because a SESSION command passes no
     * permission check. Each dispatch asks, so it clears as soon as the change succeeds. */
    bool required = true;
    crux_access_result_t answer =
        crux_access_session_must_change_password(call->session, &required);
    if (answer != CRUX_ACCESS_OK) {
        reject_as_unknown(call);
        call->session_lost = true;
        return false;
    }
    if (required && (command->flags & CRUX_CLI_FLAG_FORCED_CHANGE_OK) == 0) {
        reject_as_unknown(call);
        emit_rejected(call, CRUX_CLI_EVENT_DENIED);
        return false;
    }
    if (command->access_class != CRUX_CLI_ACCESS_PERMISSION) {
        return true;
    }
    /* This check leaves Access's denial event. */
    answer = crux_access_check(call->session, command->permission);
    if (answer == CRUX_ACCESS_OK) {
        return true;
    }
    reject_as_unknown(call);
    if (answer == CRUX_ACCESS_ERR_SESSION_INVALID || answer == CRUX_ACCESS_ERR_NOT_READY) {
        call->session_lost = true;
    }
    return false;
}

static void dispatch(crux_cli_call_t *call, size_t length, bool too_long)
{
    crux_cli_context_t *context = call->context;
    call->exit_status = CRUX_CLI_EXIT_OK;
    if (crux_access_touch(call->session) != CRUX_ACCESS_OK) {
        call->session_lost = true;
        call->exit_status = CRUX_CLI_EXIT_FAILED;
        return;
    }
    if (too_long) {
        crux_cli_call_say(call, CRUX_CLI_TEXT_TOO_LONG);
        emit_rejected(call, CRUX_CLI_EVENT_LIMIT);
        call->exit_status = CRUX_CLI_EXIT_USAGE;
        return;
    }
    /* Every tokenising limit applies here, independent of the command. */
    size_t count = 0;
    bool too_many = false;
    if (crux_cli_tokenize(context, length, &count, &too_many) != CRUX_CLI_OK) {
        crux_cli_call_say(call, CRUX_CLI_TEXT_INVALID_LINE);
        emit_rejected(call, too_many ? CRUX_CLI_EVENT_LIMIT : CRUX_CLI_EVENT_INVALID);
        call->exit_status = CRUX_CLI_EXIT_USAGE;
        return;
    }
    if (count == 0) {
        return;
    }
    size_t words = 0;
    const crux_cli_command_t *command =
        crux_cli_registry_find(context->reserved.argv, count, &words);
    if (command == NULL) {
        reject_as_unknown(call);
        emit_rejected(call, CRUX_CLI_EVENT_INVALID);
        return;
    }
    if (!command_may_run(call, command)) {
        return;
    }
    /* Only a command the session can run gets this far, so these answers reveal nothing. */
    if (call->exec && (command->flags & CRUX_CLI_FLAG_INTERACTIVE) != 0) {
        crux_cli_call_say(call, CRUX_CLI_TEXT_NOT_FOR_EXEC);
        call->exit_status = CRUX_CLI_EXIT_USAGE;
        return;
    }
    size_t args = count - words;
    if (args < command->min_args || args > command->max_args) {
        say_usage(call, command);
        return;
    }
    finish_command(call, command,
                   command->handler(call, args, context->reserved.argv + words, command->context));
}

crux_cli_result_t crux_cli_session_loop(crux_cli_context_t *context, const crux_cli_io_t *io,
                                        crux_access_channel_t channel,
                                        crux_access_session_t session)
{
    if (!crux_cli_registry_ready()) {
        return CRUX_CLI_ERR_NOT_READY;
    }
    bool required = false;
    if (crux_access_session_must_change_password(session, &required) == CRUX_ACCESS_OK &&
        required && crux_cli_send_text(io, session, CRUX_CLI_TEXT_MUST_CHANGE) != CRUX_CLI_OK) {
        return CRUX_CLI_ERR_OUTPUT;
    }
    for (;;) {
        if (crux_cli_send(io, session, prompt_text, sizeof(prompt_text) - 1u) != CRUX_CLI_OK) {
            clear_line(context);
            return CRUX_CLI_ERR_OUTPUT;
        }
        size_t length = 0;
        crux_cli_read_t input = crux_cli_read_line(context, io, session, &length);
        crux_cli_call_t call;
        memset(&call, 0, sizeof(call));
        call.context = context;
        call.io = io;
        call.channel = channel;
        call.session = session;
        switch (input) {
        case CRUX_CLI_READ_OK:
        case CRUX_CLI_READ_TOO_LONG:
            dispatch(&call, length, input == CRUX_CLI_READ_TOO_LONG);
            break;
        case CRUX_CLI_READ_INTERRUPTED:
        case CRUX_CLI_READ_IDLE:
            break;
        case CRUX_CLI_READ_TRANSPORT:
            call.output_failed = true;
            break;
        case CRUX_CLI_READ_SESSION:
            call.session_lost = true;
            break;
        }
        clear_line(context);
        if (call.output_failed) {
            return CRUX_CLI_ERR_OUTPUT;
        }
        if (call.session_lost) {
            (void)crux_cli_send_text(io, session, CRUX_CLI_TEXT_SESSION_ENDED);
            return CRUX_CLI_ERR_DENIED;
        }
        if (call.logout_requested) {
            return CRUX_CLI_OK;
        }
    }
}

crux_cli_result_t crux_cli_run(crux_cli_context_t *context, const crux_cli_io_t *io,
                               crux_access_channel_t channel, crux_access_session_t session)
{
    if (context == NULL || io == NULL || io->read == NULL || io->write == NULL ||
        channel_flag(channel) == 0) {
        return CRUX_CLI_ERR_INVALID_ARGUMENT;
    }
    crux_cli_input_reset(context);
    return crux_cli_session_loop(context, io, channel, session);
}

crux_cli_result_t crux_cli_exec(crux_cli_context_t *context, const crux_cli_io_t *io,
                                crux_access_channel_t channel, crux_access_session_t session,
                                const char *line, size_t length, int *exit_status)
{
    if (exit_status != NULL) {
        *exit_status = CRUX_CLI_EXIT_FAILED;
    }
    if (context == NULL || io == NULL || io->write == NULL || exit_status == NULL ||
        channel_flag(channel) == 0 || (line == NULL && length > 0)) {
        return CRUX_CLI_ERR_INVALID_ARGUMENT;
    }
    if (!crux_cli_registry_ready()) {
        return CRUX_CLI_ERR_NOT_READY;
    }
    crux_cli_input_reset(context);
    bool too_long = length > CRUX_CLI_LINE_MAX;
    if (!too_long && length > 0) {
        memcpy(context->line, line, length);
    }
    crux_cli_call_t call;
    memset(&call, 0, sizeof(call));
    call.context = context;
    call.io = io;
    call.channel = channel;
    call.session = session;
    call.exec = true;
    dispatch(&call, length, too_long);
    clear_line(context);
    if (call.output_failed) {
        return CRUX_CLI_ERR_OUTPUT;
    }
    *exit_status = call.exit_status;
    if (call.session_lost) {
        (void)crux_cli_send_text(io, session, CRUX_CLI_TEXT_SESSION_ENDED);
        return CRUX_CLI_ERR_DENIED;
    }
    return CRUX_CLI_OK;
}

crux_access_session_t crux_cli_session(const crux_cli_call_t *call)
{
    if (call == NULL) {
        crux_access_session_t none = {0};
        return none;
    }
    return call->session;
}

crux_cli_result_t crux_cli_write(crux_cli_call_t *call, const void *data, size_t length)
{
    if (call == NULL || (data == NULL && length > 0)) {
        return CRUX_CLI_ERR_INVALID_ARGUMENT;
    }
    if (crux_cli_send(call->io, call->session, data, length) != CRUX_CLI_OK) {
        call->output_failed = true;
        return CRUX_CLI_ERR_OUTPUT;
    }
    return CRUX_CLI_OK;
}

crux_cli_result_t crux_cli_read_secret(crux_cli_call_t *call, const char *prompt, char *buffer,
                                       size_t capacity, size_t *length)
{
    if (length != NULL) {
        *length = 0;
    }
    if (buffer != NULL && capacity > 0) {
        crux_cli_secure_zero(buffer, capacity);
    }
    if (call == NULL || buffer == NULL || capacity == 0 || length == NULL) {
        return CRUX_CLI_ERR_INVALID_ARGUMENT;
    }
    if (call->exec) {
        return CRUX_CLI_ERR_USAGE;
    }
    if (prompt != NULL && crux_cli_write(call, prompt, strlen(prompt)) != CRUX_CLI_OK) {
        return CRUX_CLI_ERR_OUTPUT;
    }
    crux_cli_read_t input =
        crux_cli_read_hidden(call->context, call->io, call->session, buffer, capacity, length);
    switch (input) {
    case CRUX_CLI_READ_OK:
        /* Finishing a prompt is user activity, as a complete line is. */
        if (crux_access_touch(call->session) != CRUX_ACCESS_OK) {
            crux_cli_secure_zero(buffer, capacity);
            *length = 0;
            call->session_lost = true;
            return CRUX_CLI_ERR_OUTPUT;
        }
        return CRUX_CLI_OK;
    case CRUX_CLI_READ_TOO_LONG:
        return CRUX_CLI_ERR_TOO_LONG;
    case CRUX_CLI_READ_INTERRUPTED:
        return CRUX_CLI_ERR_CANCELLED;
    case CRUX_CLI_READ_SESSION:
        call->session_lost = true;
        return CRUX_CLI_ERR_OUTPUT;
    case CRUX_CLI_READ_IDLE:
    case CRUX_CLI_READ_TRANSPORT:
        break;
    }
    call->output_failed = true;
    return CRUX_CLI_ERR_OUTPUT;
}
