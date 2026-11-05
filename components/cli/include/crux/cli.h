/**
 * @file
 * @brief Command tables and handlers for the Crux command line.
 *
 * Thread context only. A project declares commands with CRUX_CLI_COMMAND() and passes its tables to
 * crux_cli_init(), which must return before any session starts. A handler runs on the thread of its
 * session and may block that session only. A handler is trusted firmware: the registry can reject
 * a command and cannot grant anything, so services still check their own permissions.
 *
 * There is no public access class. Every command needs a session, and login and first-account
 * setup sit outside the registry. See docs/cli-contract.md.
 */
#ifndef CRUX_CLI_H
#define CRUX_CLI_H

#include <crux/access.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How a command is checked. Zero is invalid, so a zero-initialised entry fails crux_cli_init(). */
typedef enum {
    CRUX_CLI_ACCESS_INVALID = 0,
    /** Any valid session. The handler or the service it calls must check further. */
    CRUX_CLI_ACCESS_SESSION = 1,
    /** A session holding the command's permission. Hidden from a session without it. */
    CRUX_CLI_ACCESS_PERMISSION = 2
} crux_cli_access_class_t;

/** Command line results; zero is success and every other value is a refusal or a failure. */
typedef enum {
    CRUX_CLI_OK = 0,
    CRUX_CLI_ERR_INVALID_ARGUMENT = -1,
    CRUX_CLI_ERR_NOT_READY = -2,
    CRUX_CLI_ERR_USAGE = -3,  /**< Argument count or form. */
    CRUX_CLI_ERR_FAILED = -4, /**< The command ran and failed. */
    CRUX_CLI_ERR_DENIED = -5, /**< A service inside the handler refused. */
    CRUX_CLI_ERR_OUTPUT =
        -6, /**< The transport failed or the session ended, so the session ends. */
    CRUX_CLI_ERR_TOO_LONG = -7,
    CRUX_CLI_ERR_CANCELLED = -8 /**< The user stopped a prompt with Ctrl-C. Prints nothing. */
} crux_cli_result_t;

/** One running command. Valid only during its handler call. */
typedef struct crux_cli_call crux_cli_call_t;

/**
 * Runs one command on the thread of its session.
 *
 * @param call Valid until the handler returns.
 * @param argc Number of arguments, without the command's own words.
 * @param argv Arguments, valid until the handler returns. Double-quoted values are unescaped.
 * @param context The command's context, unchanged.
 * @return CRUX_CLI_OK, or a result that the registry reports with one fixed line. A value outside
 * crux_cli_result_t reads as CRUX_CLI_ERR_FAILED.
 */
typedef crux_cli_result_t (*crux_cli_handler_t)(crux_cli_call_t *call, size_t argc,
                                                const char *const *argv, void *context);

enum {
    CRUX_CLI_CHANNEL_SERIAL = 1u << 0,
    CRUX_CLI_CHANNEL_SSH = 1u << 1,
    CRUX_CLI_FLAG_INTERACTIVE = 1u << 2,     /**< Reads a prompt, so exec refuses it. */
    CRUX_CLI_FLAG_FORCED_CHANGE_OK = 1u << 3 /**< Also runs during a forced password change. */
};

/** One command. Declare it with CRUX_CLI_COMMAND(), so that it cannot lack an access class. */
typedef struct {
    const char *name;    /**< One or two words of [a-z0-9-], for example "config get". */
    const char *summary; /**< One line of printable ASCII for help. */
    crux_cli_handler_t handler;
    void *context;          /**< Passed unchanged to the handler. */
    const char *permission; /**< Required for CRUX_CLI_ACCESS_PERMISSION, NULL otherwise. */
    crux_cli_access_class_t access_class;
    uint8_t min_args;
    uint8_t max_args;
    uint8_t flags; /**< At least one channel bit is required. */
} crux_cli_command_t;

/** Access argument of CRUX_CLI_COMMAND(): the session must hold this permission. */
#define CRUX_CLI_PERMISSION(permission) (permission), CRUX_CLI_ACCESS_PERMISSION
/** Access argument of CRUX_CLI_COMMAND(): any session, and the handler or service checks further.
 */
#define CRUX_CLI_SESSION_ONLY NULL, CRUX_CLI_ACCESS_SESSION
/**
 * Initialiser for a crux_cli_command_t. The access argument expands to the permission and class, so
 * a command with no access class does not compile. Positional, because designated initialisers are
 * not C++17. The field order keeps a table entry free of padding.
 */
#define CRUX_CLI_COMMAND(name, summary, access, min_args, max_args, flags, handler, context)       \
    {(name), (summary), (handler), (context), access, (min_args), (max_args), (flags)}

/**
 * Validate every table once, and make the registry ready.
 *
 * The built-in commands help, logout, passwd and account list are always registered, ahead of the
 * tables, so a table cannot reuse their names. Refuses a bad name or summary, a duplicate across
 * tables, a one-word name that is also the first word of a two-word name, a bad class and
 * permission pair, argument bounds that the token limit cannot reach, flags with no channel or an
 * unknown bit, and a missing handler. On failure, or before a success, no command runs and sessions
 * report not ready. A failure can be retried.
 *
 * @param tables Tables, borrowed for the program's life. May be NULL when table_count is zero.
 * @param counts Number of commands in each table, borrowed for the program's life.
 * @param table_count Number of tables.
 * @retval CRUX_CLI_OK Ready.
 * @retval CRUX_CLI_ERR_INVALID_ARGUMENT A table failed validation, or the registry is already
 * ready, in which case nothing changed.
 */
crux_cli_result_t crux_cli_init(const crux_cli_command_t *const *tables, const size_t *counts,
                                size_t table_count);

/**
 * Set the text printed after "password not accepted", which states the policy's password rule.
 *
 * Call it before any session starts. Without it, only the fixed words are printed.
 *
 * @param text One line of printable ASCII, borrowed for the program's life, or NULL for none.
 * @retval CRUX_CLI_OK Set.
 * @retval CRUX_CLI_ERR_INVALID_ARGUMENT The text holds anything else. The previous text stays.
 */
crux_cli_result_t crux_cli_set_password_rule(const char *text);

/** @return The session running this command. Zero if call is NULL. */
crux_access_session_t crux_cli_session(const crux_cli_call_t *call);

/**
 * Write output for the user. Converts nothing, so end a line with a single LF.
 *
 * Retries a short write until the transport accepts everything or fails. Each attempt has a bound,
 * and between attempts the session is checked, so the idle and absolute limits end a stuck write. A
 * slow client blocks only its own session.
 *
 * @retval CRUX_CLI_OK Accepted by the transport, which does not mean delivered.
 * @retval CRUX_CLI_ERR_INVALID_ARGUMENT The call is NULL, or data is NULL with a nonzero length.
 * @retval CRUX_CLI_ERR_OUTPUT The transport failed or the session ended. Return this result.
 */
crux_cli_result_t crux_cli_write(crux_cli_call_t *call, const void *data, size_t length);

/**
 * Prompt for a secret, and read it without echo.
 *
 * The bytes go only to the caller's buffer, never to the line buffer. Backspace and Ctrl-U edit it,
 * and other controls are discarded. The caller owns and clears the buffer. A line longer than the
 * buffer is read to its end and refused.
 *
 * @param prompt Text to write first, or NULL for none.
 * @param[out] buffer Receives the bytes and a terminator. Zeroed on every failure.
 * @param capacity Size of buffer, including the terminator.
 * @param[out] length Bytes before the terminator. Zero on every failure.
 * @retval CRUX_CLI_OK Read.
 * @retval CRUX_CLI_ERR_INVALID_ARGUMENT The call, buffer or length is NULL, or capacity is zero.
 * @retval CRUX_CLI_ERR_USAGE The command runs under exec, which has no prompts.
 * @retval CRUX_CLI_ERR_TOO_LONG The line did not fit.
 * @retval CRUX_CLI_ERR_CANCELLED The user pressed Ctrl-C.
 * @retval CRUX_CLI_ERR_OUTPUT The transport failed or the session ended. Return this result.
 */
crux_cli_result_t crux_cli_read_secret(crux_cli_call_t *call, const char *prompt, char *buffer,
                                       size_t capacity, size_t *length);

#ifdef __cplusplus
}
#endif

#endif
