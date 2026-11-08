/* Helpers for the suites that run sessions with the scripted access control. */
#ifndef CRUX_CLI_SESSION_TEST_H
#define CRUX_CLI_SESSION_TEST_H

#include "cli_test.h"
#include "fake_access.h"

extern cli_fake_t cli_test_fake;
extern crux_cli_context_t cli_test_context;

/* Back to a clean state: transport, events, access control and an uninitialised registry. */
void cli_test_begin(void);
/* cli_test_begin, then registers one table, which must be valid. */
void cli_test_begin_table(const crux_cli_command_t *table, size_t count);

/* Runs a session on the channel with this input. The transport ends when the input runs out. */
crux_cli_result_t cli_test_session(const char *input, bool echo, crux_access_channel_t channel);
/* The same, with whatever the suite has already scripted in cli_test_fake. */
crux_cli_result_t cli_test_session_scripted(bool echo, crux_access_channel_t channel);
crux_cli_result_t cli_test_exec(const char *line, crux_access_channel_t channel, int *exit_status);
crux_cli_result_t cli_test_exec_bytes(const char *line, size_t length,
                                      crux_access_channel_t channel, int *exit_status);
const char *cli_test_output(void);
void cli_test_output_clear(void);

/* A session that holds these permissions. */
void cli_test_grant(const char *permission);

#endif
