/* The commands every registry holds. Each uses only crux_access. The rest of the reference table,
 * such as status and config, arrives with the services they call. */
#include "cli_internal.h"

#include <string.h>

#define BOTH_CHANNELS (CRUX_CLI_CHANNEL_SERIAL | CRUX_CLI_CHANNEL_SSH)

static crux_cli_result_t write_text(crux_cli_call_t *call, const char *text)
{
    return crux_cli_write(call, text, strlen(text));
}

static crux_cli_result_t write_summary(crux_cli_call_t *call, const crux_cli_command_t *command)
{
    crux_cli_result_t result = write_text(call, command->name);
    if (result == CRUX_CLI_OK) {
        result = write_text(call, " - ");
    }
    if (result == CRUX_CLI_OK) {
        result = write_text(call, command->summary);
    }
    return result == CRUX_CLI_OK ? write_text(call, "\n") : result;
}

/* Uses the quiet probe, so filtering the list leaves no denial events, and rule L3 for a name. */
static crux_cli_result_t run_help(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                  void *context)
{
    (void)context;
    if (argc > 0) {
        size_t words = 0;
        const crux_cli_command_t *command = crux_cli_registry_find(argv, argc, &words);
        if (command == NULL || words != argc || !crux_cli_command_visible(call, command)) {
            return crux_cli_call_report(call, CRUX_CLI_TEXT_UNKNOWN, CRUX_CLI_EXIT_UNKNOWN);
        }
        return write_summary(call, command);
    }
    const crux_cli_command_t *command;
    for (size_t index = 0; (command = crux_cli_registry_at(index)) != NULL; ++index) {
        if (crux_cli_command_visible(call, command)) {
            crux_cli_result_t result = write_summary(call, command);
            if (result != CRUX_CLI_OK) {
                return result;
            }
        }
    }
    return CRUX_CLI_OK;
}

/* Ends the run. The caller ends the session, as it does for any other way out. */
static crux_cli_result_t run_logout(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                    void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    call->logout_requested = true;
    return CRUX_CLI_OK;
}

static crux_cli_result_t report_change(crux_cli_call_t *call, crux_access_result_t answer)
{
    switch (answer) {
    case CRUX_ACCESS_OK:
        return crux_cli_call_report(call, "password changed\n", CRUX_CLI_EXIT_OK);
    case CRUX_ACCESS_ERR_BUSY:
        return crux_cli_call_report(call, CRUX_CLI_TEXT_BUSY, CRUX_CLI_EXIT_FAILED);
    case CRUX_ACCESS_ERR_PASSWORD_REJECTED:
        if (crux_cli_send_password_rejected(call->io, call->session) != CRUX_CLI_OK) {
            call->output_failed = true;
        }
        return crux_cli_call_report(call, NULL, CRUX_CLI_EXIT_FAILED);
    case CRUX_ACCESS_ERR_STORE:
        return crux_cli_call_report(call, CRUX_CLI_TEXT_STORE, CRUX_CLI_EXIT_FAILED);
    case CRUX_ACCESS_ERR_NOT_READY:
        return crux_cli_call_report(call, CRUX_CLI_TEXT_UNAVAILABLE, CRUX_CLI_EXIT_FAILED);
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        call->session_lost = true;
        return crux_cli_call_report(call, NULL, CRUX_CLI_EXIT_FAILED);
    default:
        return crux_cli_call_report(call, "password not changed\n", CRUX_CLI_EXIT_FAILED);
    }
}

/* The account service checks ownership, and it works during a forced change. */
static crux_cli_result_t run_passwd(crux_cli_call_t *call, size_t argc, const char *const *argv,
                                    void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    char *current = call->context->reserved.secret[0];
    char *fresh = call->context->reserved.secret[1];
    char *repeat = call->context->reserved.secret[2];
    const size_t size = sizeof(call->context->reserved.secret[0]);
    size_t current_length = 0;
    size_t fresh_length = 0;
    size_t repeat_length = 0;

    crux_cli_result_t result =
        crux_cli_read_secret(call, "current password: ", current, size, &current_length);
    if (result == CRUX_CLI_OK) {
        result = crux_cli_read_secret(call, "new password: ", fresh, size, &fresh_length);
    }
    if (result == CRUX_CLI_OK) {
        result = crux_cli_read_secret(call, "repeat: ", repeat, size, &repeat_length);
    }
    if (result == CRUX_CLI_OK) {
        if (fresh_length != repeat_length || memcmp(fresh, repeat, fresh_length) != 0) {
            result = crux_cli_call_report(call, CRUX_CLI_TEXT_DIFFER, CRUX_CLI_EXIT_FAILED);
        } else {
            uint32_t retry_after_ms = 0;
            crux_access_result_t answer = crux_access_account_change_password(
                call->session, current, current_length, fresh, fresh_length, &retry_after_ms);
            crux_cli_secure_zero(call->context->reserved.secret,
                                 sizeof(call->context->reserved.secret));
            if (answer == CRUX_ACCESS_ERR_RETRY_LATER &&
                crux_cli_send_retry(call->io, call->session, retry_after_ms) != CRUX_CLI_OK) {
                call->output_failed = true;
            }
            result = answer == CRUX_ACCESS_ERR_RETRY_LATER
                         ? crux_cli_call_report(call, NULL, CRUX_CLI_EXIT_FAILED)
                         : report_change(call, answer);
        }
    }
    crux_cli_secure_zero(call->context->reserved.secret, sizeof(call->context->reserved.secret));
    return result;
}

static crux_cli_result_t write_account(crux_cli_call_t *call,
                                       const crux_access_account_info_t *account)
{
    char number[11];
    uint32_t keys = account->key_count > UINT32_MAX ? UINT32_MAX : (uint32_t)account->key_count;
    (void)crux_cli_format_decimal(number, keys);
    const char *const parts[] = {account->user,
                                 " ",
                                 account->role,
                                 account->enabled ? " enabled" : " disabled",
                                 account->must_change_password ? " must-change keys=" : " keys=",
                                 number,
                                 "\n"};
    for (size_t index = 0; index < sizeof(parts) / sizeof(parts[0]); ++index) {
        crux_cli_result_t result = write_text(call, parts[index]);
        if (result != CRUX_CLI_OK) {
            return result;
        }
    }
    return CRUX_CLI_OK;
}

static crux_cli_result_t run_account_list(crux_cli_call_t *call, size_t argc,
                                          const char *const *argv, void *context)
{
    (void)argc;
    (void)argv;
    (void)context;
    crux_access_account_info_t accounts[CRUX_CLI_ACCOUNT_LIST_MAX];
    size_t count = 0;
    crux_access_result_t answer =
        crux_access_account_list(call->session, accounts, CRUX_CLI_ACCOUNT_LIST_MAX, &count);
    switch (answer) {
    case CRUX_ACCESS_OK:
        break;
    case CRUX_ACCESS_ERR_DENIED:
        return CRUX_CLI_ERR_DENIED;
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        call->session_lost = true;
        return CRUX_CLI_ERR_FAILED;
    default:
        return CRUX_CLI_ERR_NOT_READY;
    }
    size_t shown = count < CRUX_CLI_ACCOUNT_LIST_MAX ? count : CRUX_CLI_ACCOUNT_LIST_MAX;
    for (size_t index = 0; index < shown; ++index) {
        crux_cli_result_t result = write_account(call, &accounts[index]);
        if (result != CRUX_CLI_OK) {
            return result;
        }
    }
    if (count > shown) {
        char number[11];
        (void)crux_cli_format_decimal(number, (uint32_t)(count - shown));
        crux_cli_result_t result = write_text(call, "and ");
        if (result == CRUX_CLI_OK) {
            result = write_text(call, number);
        }
        return result == CRUX_CLI_OK ? write_text(call, " more\n") : result;
    }
    return CRUX_CLI_OK;
}

const crux_cli_command_t crux_cli_builtin_commands[] = {
    CRUX_CLI_COMMAND("help", "list commands, or describe one", CRUX_CLI_SESSION_ONLY, 0, 2,
                     BOTH_CHANNELS | CRUX_CLI_FLAG_FORCED_CHANGE_OK, run_help, NULL),
    CRUX_CLI_COMMAND("logout", "end this session", CRUX_CLI_SESSION_ONLY, 0, 0,
                     BOTH_CHANNELS | CRUX_CLI_FLAG_FORCED_CHANGE_OK, run_logout, NULL),
    CRUX_CLI_COMMAND("passwd", "change your password", CRUX_CLI_SESSION_ONLY, 0, 0,
                     BOTH_CHANNELS | CRUX_CLI_FLAG_INTERACTIVE | CRUX_CLI_FLAG_FORCED_CHANGE_OK,
                     run_passwd, NULL),
    CRUX_CLI_COMMAND("account list", "list accounts",
                     CRUX_CLI_PERMISSION(CRUX_ACCESS_PERMISSION_MANAGE), 0, 0, BOTH_CHANNELS,
                     run_account_list, NULL),
};

const size_t crux_cli_builtin_command_count =
    sizeof(crux_cli_builtin_commands) / sizeof(crux_cli_builtin_commands[0]);

bool crux_cli_command_visible(const crux_cli_call_t *call, const crux_cli_command_t *command)
{
    unsigned channel = call->channel == CRUX_ACCESS_CHANNEL_SERIAL ? CRUX_CLI_CHANNEL_SERIAL
                                                                   : CRUX_CLI_CHANNEL_SSH;
    if ((command->flags & channel) == 0) {
        return false;
    }
    bool required = true;
    if (crux_access_session_must_change_password(call->session, &required) != CRUX_ACCESS_OK ||
        (required && (command->flags & CRUX_CLI_FLAG_FORCED_CHANGE_OK) == 0)) {
        return false;
    }
    if (command->access_class != CRUX_CLI_ACCESS_PERMISSION) {
        return true;
    }
    bool permitted = false;
    return crux_access_permitted(call->session, command->permission, &permitted) ==
               CRUX_ACCESS_OK &&
           permitted;
}
