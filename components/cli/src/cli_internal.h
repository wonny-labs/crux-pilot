#ifndef CRUX_CLI_INTERNAL_H
#define CRUX_CLI_INTERNAL_H

#include "cli_port.h"

#include <crux_cli_adapter.h>

/* The bound of one read or write attempt, and so how often a waiting session is checked. */
#ifndef CRUX_CLI_READ_TIMEOUT_MS
#define CRUX_CLI_READ_TIMEOUT_MS 1000u
#endif
#ifndef CRUX_CLI_WRITE_TIMEOUT_MS
#define CRUX_CLI_WRITE_TIMEOUT_MS 1000u
#endif
/* A stream with no line end or timeout still has its session checked this often. */
#ifndef CRUX_CLI_SESSION_CHECK_BYTES
#define CRUX_CLI_SESSION_CHECK_BYTES 256u
#endif
/* Without a session, a write gets this many timed-out attempts before the transport counts as
 * failed. */
#ifndef CRUX_CLI_WRITE_ATTEMPTS_NO_SESSION
#define CRUX_CLI_WRITE_ATTEMPTS_NO_SESSION 3u
#endif
/* Read timeouts before a secret prompt with no session gives up and clears what was typed. */
#ifndef CRUX_CLI_SECRET_IDLE_POLLS
#define CRUX_CLI_SECRET_IDLE_POLLS 60u
#endif
/* Most accounts `account list` shows. */
#ifndef CRUX_CLI_ACCOUNT_LIST_MAX
#define CRUX_CLI_ACCOUNT_LIST_MAX 8u
#endif
/* The longest value of a configuration string, from the status and configuration proposal, and its
 * longest name. Issue #180 ties these to its own limits when it lands. */
#ifndef CRUX_CLI_CONFIG_STRING_MAX
#define CRUX_CLI_CONFIG_STRING_MAX 128u
#endif
#define CRUX_CLI_CONFIG_NAME_MAX 31u

/* `config set <name> "<value>"` with every value byte escaped has to fit. */
_Static_assert(CRUX_CLI_LINE_MAX >=
                   11u + CRUX_CLI_CONFIG_NAME_MAX + 1u + 2u + 2u * CRUX_CLI_CONFIG_STRING_MAX,
               "CRUX_CLI_LINE_MAX cannot hold the longest config set line");
_Static_assert(CRUX_CLI_TOKENS_MAX >= 4, "CRUX_CLI_TOKENS_MAX cannot hold the longest config set");

/* Fixed output. A console and a session use the same words. */
#define CRUX_CLI_TEXT_UNKNOWN "unknown command\n"
#define CRUX_CLI_TEXT_INVALID_LINE "invalid line\n"
#define CRUX_CLI_TEXT_TOO_LONG "line too long\n"
#define CRUX_CLI_TEXT_INPUT_TOO_LONG "input too long\n"
#define CRUX_CLI_TEXT_NOT_FOR_EXEC "not available for exec\n"
#define CRUX_CLI_TEXT_FAILED "command failed\n"
#define CRUX_CLI_TEXT_DENIED "permission denied\n"
#define CRUX_CLI_TEXT_UNAVAILABLE "service unavailable\n"
#define CRUX_CLI_TEXT_SESSION_ENDED "session ended\n"
#define CRUX_CLI_TEXT_MUST_CHANGE "password change required, use passwd\n"
#define CRUX_CLI_TEXT_LOGIN_UNAVAILABLE "login unavailable\n"
#define CRUX_CLI_TEXT_BUSY "busy, try again\n"
#define CRUX_CLI_TEXT_DIFFER "passwords differ\n"
#define CRUX_CLI_TEXT_REJECTED "password not accepted"
#define CRUX_CLI_TEXT_STORE "could not save, a later boot may use either\n"

#define CRUX_CLI_EXIT_OK 0
#define CRUX_CLI_EXIT_FAILED 1
#define CRUX_CLI_EXIT_USAGE 2
#define CRUX_CLI_EXIT_UNKNOWN 127

struct crux_cli_call {
    crux_cli_context_t *context;
    const crux_cli_io_t *io;
    crux_access_channel_t channel;
    crux_access_session_t session;
    bool exec;
    bool logout_requested;
    bool session_lost;  /* Access control said the session is over, or stopped answering. */
    bool output_failed; /* A write failed, so the session cannot continue. */
    bool reported;      /* A built-in already printed its result. */
    int exit_status;    /* Set by the dispatcher, or by a built-in through reported. */
};

/* The only input actions a byte can have once escape sequences and line ends are resolved. */
typedef enum {
    CRUX_CLI_INPUT_NONE,
    CRUX_CLI_INPUT_BYTE,
    CRUX_CLI_INPUT_LINE_END,
    CRUX_CLI_INPUT_ERASE,
    CRUX_CLI_INPUT_KILL,
    CRUX_CLI_INPUT_INTERRUPT
} crux_cli_input_action_t;

typedef enum {
    CRUX_CLI_READ_OK,
    CRUX_CLI_READ_TOO_LONG,
    CRUX_CLI_READ_INTERRUPTED,
    CRUX_CLI_READ_IDLE,      /* A secret prompt with no session waited too long. */
    CRUX_CLI_READ_TRANSPORT, /* The transport ended or failed. */
    CRUX_CLI_READ_SESSION    /* The session is over. */
} crux_cli_read_t;

/* cli_input.c */
void crux_cli_input_reset(crux_cli_context_t *context);
crux_cli_input_action_t crux_cli_input_feed(crux_cli_context_t *context, uint8_t byte);
/* Reads into context->line and terminates it. A zero session skips the session checks. */
crux_cli_read_t crux_cli_read_line(crux_cli_context_t *context, const crux_cli_io_t *io,
                                   crux_access_session_t session, size_t *length);
/* Reads without echo. buffer and *length are zero on anything other than CRUX_CLI_READ_OK. */
crux_cli_read_t crux_cli_read_hidden(crux_cli_context_t *context, const crux_cli_io_t *io,
                                     crux_access_session_t session, char *buffer, size_t capacity,
                                     size_t *length);
/* A zero session bounds a stuck write by CRUX_CLI_WRITE_ATTEMPTS_NO_SESSION. */
crux_cli_result_t crux_cli_send(const crux_cli_io_t *io, crux_access_session_t session,
                                const void *data, size_t length);
crux_cli_result_t crux_cli_send_text(const crux_cli_io_t *io, crux_access_session_t session,
                                     const char *text);
/* Sends "password not accepted", the policy's rule if one is set, and a line end. */
crux_cli_result_t crux_cli_send_password_rejected(const crux_cli_io_t *io,
                                                  crux_access_session_t session);
/* Writes value in decimal with a terminator, into at least 11 bytes. Returns its length. */
size_t crux_cli_format_decimal(char *text, uint32_t value);
/* Sends "try again in N s", rounding up, and a line end. */
crux_cli_result_t crux_cli_send_retry(const crux_cli_io_t *io, crux_access_session_t session,
                                      uint32_t retry_after_ms);
void crux_cli_secure_zero(void *memory, size_t length);

/* cli_parse.c */
/* Splits line in place, and sets context->reserved.argv. CRUX_CLI_ERR_USAGE for a bad form or too
 * many tokens, in which case *too_many says which. */
crux_cli_result_t crux_cli_tokenize(crux_cli_context_t *context, size_t length, size_t *count,
                                    bool *too_many);

/* cli_registry.c */
bool crux_cli_registry_ready(void);
/* Finds the longest name matching the tokens. *words is how many tokens it took. */
const crux_cli_command_t *crux_cli_registry_find(const char *const *tokens, size_t count,
                                                 size_t *words);
/* Visits commands in order. Returns NULL past the last. */
const crux_cli_command_t *crux_cli_registry_at(size_t index);
const char *crux_cli_registry_password_rule(void);

/* cli_session.c */
/* crux_cli_run without its argument checks and without resetting the input state, so that the LF of
 * a CR LF which ended a password is still swallowed at the session's first prompt. */
crux_cli_result_t crux_cli_session_loop(crux_cli_context_t *context, const crux_cli_io_t *io,
                                        crux_access_channel_t channel,
                                        crux_access_session_t session);
/* Writes through the call, and remembers a failed write. */
void crux_cli_call_say(crux_cli_call_t *call, const char *text);
/* A built-in that prints its own result line: it sets the exit status, and the dispatcher adds
 * nothing. Returns CRUX_CLI_ERR_OUTPUT if a write has failed. text may be NULL. */
crux_cli_result_t crux_cli_call_report(crux_cli_call_t *call, const char *text, int exit_status);

/* cli_commands.c */
extern const crux_cli_command_t crux_cli_builtin_commands[];
extern const size_t crux_cli_builtin_command_count;
/* Whether this session's checks let it see the command, with no event. */
bool crux_cli_command_visible(const crux_cli_call_t *call, const crux_cli_command_t *command);

#endif
