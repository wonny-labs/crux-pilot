/**
 * @file
 * @brief Session entry points for channel adapters. Projects must not include this header.
 *
 * Crux code only: the serial console and the SSH adapter of Issue #193 link crux_cli_adapter
 * PRIVATE. Thread context only. The thread and the context belong to the caller, one per session,
 * so blocking one session never affects another. The contract never allocates.
 *
 * Serial and SSH are two users of the transport below. Nothing here names a serial or SSH type.
 */
#ifndef CRUX_CLI_ADAPTER_H
#define CRUX_CLI_ADAPTER_H

#include <crux/cli.h>
#include <crux_access_adapter.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest input line, in bytes before the line end. A build setting can raise it. */
#ifndef CRUX_CLI_LINE_MAX
#define CRUX_CLI_LINE_MAX 320
#endif
/** Most tokens in a line, counting the command's words. A build setting can raise it. */
#ifndef CRUX_CLI_TOKENS_MAX
#define CRUX_CLI_TOKENS_MAX 10
#endif

/**
 * A session's transport. The CLI calls one function at a time, on the session's thread, and reads
 * one byte per call, so that a secret never sits in a CLI buffer of pending input.
 */
typedef struct {
    /**
     * Blocking with a bound.
     * @return Bytes read, 0 on timeout, negative when the transport has ended.
     */
    int (*read)(void *transport, void *buffer, size_t length, uint32_t timeout_ms);
    /**
     * Blocking with a bound. It may accept fewer bytes than asked.
     * @return Bytes accepted, 0 on timeout, negative on failure.
     */
    int (*write)(void *transport, const void *buffer, size_t length, uint32_t timeout_ms);
    void *transport; /**< Passed unchanged to both functions. */
    /** The CLI echoes input. True on serial and on an SSH channel with a terminal. */
    bool echo;
} crux_cli_io_t;

/** One session's working memory. The caller owns it and needs not initialise it. */
typedef struct {
    char line[CRUX_CLI_LINE_MAX + 1];
    /** Reserved. Do not use. */
    struct {
        const char *argv[CRUX_CLI_TOKENS_MAX];
        char secret[3][CRUX_ACCESS_PASSWORD_MAX + 1u];
        uint8_t escape_state;
        uint8_t escape_length;
        bool last_was_cr;
    } reserved;
} crux_cli_context_t;

/**
 * Serve one authenticated session until logout, expiry or a transport failure.
 *
 * Prints a notice when a forced password change is pending. The caller keeps the session and ends
 * it with crux_access_logout() afterwards.
 *
 * @param context The session's memory, used until return.
 * @param io Borrowed until return. read and write are required.
 * @param channel Serial or SSH. Web has no command line.
 * @retval CRUX_CLI_OK The user logged out.
 * @retval CRUX_CLI_ERR_DENIED The session expired, was ended, or access control stopped answering.
 * @retval CRUX_CLI_ERR_OUTPUT The transport failed.
 * @retval CRUX_CLI_ERR_NOT_READY crux_cli_init() has not succeeded. Nothing was written.
 * @retval CRUX_CLI_ERR_INVALID_ARGUMENT An argument is missing or the channel has no command line.
 */
crux_cli_result_t crux_cli_run(crux_cli_context_t *context, const crux_cli_io_t *io,
                               crux_access_channel_t channel, crux_access_session_t session);

/**
 * Run one command line through the same checks, with no prompt, line editing or echo.
 *
 * A control byte in the line is refused, because nothing edits it. A command that reads a prompt is
 * refused, and a forced password change refuses everything else too.
 *
 * @param context The session's memory, used until return.
 * @param io Borrowed until return. Only write is used, and read may be NULL.
 * @param channel Serial or SSH. Web has no command line.
 * @param line Borrowed for the call. It need not be terminated.
 * @param length Bytes in line.
 * @param[out] exit_status Set on every return, if not NULL: 0 for success, 1 for a failed or denied
 * command, 2 for a usage or length problem, 127 for an unknown or hidden command. It is 1 whenever
 * nothing could be looked up or the transport failed.
 * @retval CRUX_CLI_OK The line was handled, and exit_status says how.
 * @retval CRUX_CLI_ERR_DENIED The session expired, was ended, or access control stopped answering,
 * possibly after the line was refused.
 * @retval CRUX_CLI_ERR_OUTPUT The transport failed.
 * @retval CRUX_CLI_ERR_NOT_READY crux_cli_init() has not succeeded. Nothing was written.
 * @retval CRUX_CLI_ERR_INVALID_ARGUMENT An argument is missing or the channel has no command line.
 */
crux_cli_result_t crux_cli_exec(crux_cli_context_t *context, const crux_cli_io_t *io,
                                crux_access_channel_t channel, crux_access_session_t session,
                                const char *line, size_t length, int *exit_status);

/** State of crux_cli_write_terminal() for one transport. Zero-initialise it. */
typedef struct {
    bool carriage_return_sent;
} crux_cli_line_ends_t;

/**
 * Write through a terminal's line discipline, as one step of a transport's write function.
 *
 * The CLI ends its lines with LF and converts nothing. An adapter for a terminal calls this from
 * its write function, and it sends CR LF for each LF. It makes at most one write per run of bytes,
 * and keeps its place if the CR was accepted and the LF was not.
 *
 * @param state This transport's state.
 * @param write The transport's raw write, with the contract of crux_cli_io_t.write.
 * @param transport Passed unchanged to write.
 * @return Input bytes consumed, 0 on timeout, negative on failure.
 */
int crux_cli_write_terminal(crux_cli_line_ends_t *state,
                            int (*write)(void *transport, const void *buffer, size_t length,
                                         uint32_t timeout_ms),
                            void *transport, const void *buffer, size_t length,
                            uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
