#include "cli_session_test.h"

cli_fake_t cli_test_fake;
crux_cli_context_t cli_test_context;

void cli_test_begin(void)
{
    crux_cli_test_reset();
    cli_fake_reset(&cli_test_fake);
    cli_fake_events_clear();
    cli_fake_access_reset();
    memset(&cli_test_context, 0xA5, sizeof(cli_test_context));
}

void cli_test_begin_table(const crux_cli_command_t *table, size_t count)
{
    cli_test_begin();
    /* The registry borrows these for as long as it stays ready. */
    static const crux_cli_command_t *tables[1];
    static size_t counts[1];
    tables[0] = table;
    counts[0] = count;
    CHECK(crux_cli_init(tables, counts, 1) == CRUX_CLI_OK);
}

crux_cli_result_t cli_test_session_scripted(bool echo, crux_access_channel_t channel)
{
    crux_cli_io_t io = cli_fake_io(&cli_test_fake, echo);
    return crux_cli_run(&cli_test_context, &io, channel, CLI_FAKE_ACCESS_SESSION);
}

crux_cli_result_t cli_test_session(const char *input, bool echo, crux_access_channel_t channel)
{
    cli_fake_input(&cli_test_fake, input);
    return cli_test_session_scripted(echo, channel);
}

crux_cli_result_t cli_test_exec_bytes(const char *line, size_t length,
                                      crux_access_channel_t channel, int *exit_status)
{
    crux_cli_io_t io = cli_fake_io(&cli_test_fake, false);
    return crux_cli_exec(&cli_test_context, &io, channel, CLI_FAKE_ACCESS_SESSION, line, length,
                         exit_status);
}

crux_cli_result_t cli_test_exec(const char *line, crux_access_channel_t channel, int *exit_status)
{
    return cli_test_exec_bytes(line, strlen(line), channel, exit_status);
}

const char *cli_test_output(void)
{
    return cli_test_fake.output;
}

void cli_test_output_clear(void)
{
    cli_fake_output_clear(&cli_test_fake);
}

void cli_test_grant(const char *permission)
{
    cli_fake_access.grants[cli_fake_access.grant_count] = permission;
    cli_fake_access.grant_count += 1;
}
