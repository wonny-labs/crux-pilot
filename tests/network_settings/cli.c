/* The recovery command on the real registry, with the scripted access control of the command line
 * suites, the real Configuration service over the fake provider, and the real service. */
#include "cli_session_test.h"
#include "fault_provider.h"
#include "network_settings_internal.h"
#include "network_settings_port.h"

#include <crux/config_service.h>
#include <crux/network_settings.h>
#include <crux/network_settings_cli.h>

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

/* The apply and event ports, in the smallest form the service needs. */
static unsigned events_changed, events_failed, events_ignored;
bool crux_network_settings_port_apply(const crux_network_settings_t *settings)
{
    (void)settings;
    return true;
}
bool crux_network_settings_port_running(crux_network_settings_t *settings)
{
    (void)settings;
    return false;
}
void crux_network_settings_port_event(crux_network_settings_event_t event,
                                      crux_access_session_t session)
{
    (void)session;
    events_changed += event == CRUX_NETWORK_SETTINGS_EVENT_CHANGED;
    events_failed += event == CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED;
    events_ignored += event == CRUX_NETWORK_SETTINGS_EVENT_SAVED_IGNORED;
}

static const crux_cli_command_t *table;
static size_t table_count;

static void start_everything(void)
{
    size_t count = 0;
    const crux_config_service_entry_t *entries = crux_network_settings_entries(&count);
    crux_network_settings_config_t config = {{0xC000020Au, 24u, false, 0u}};

    config_fault_reset();
    events_changed = events_failed = events_ignored = 0;
    table = crux_network_settings_cli_commands(&table_count);
    cli_test_begin_table(table, table_count);
    CHECK(crux_network_settings_boot(&config, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_config_service_start(entries, count) == CRUX_CONFIG_SERVICE_OK);
    CHECK(crux_network_settings_start() == CRUX_NETWORK_OK);
}

static void the_table_is_what_the_registry_takes(void)
{
    start_everything();
    CHECK(table_count == 1u);
    CHECK(strcmp(table[0].name, "network recover") == 0);
    CHECK(table[0].access_class == CRUX_CLI_ACCESS_PERMISSION);
    CHECK(strcmp(table[0].permission, "network.write") == 0);
    CHECK(table[0].min_args == 0u && table[0].max_args == 0u);
    /* Serial only, and it reads no prompt. */
    CHECK(table[0].flags == CRUX_CLI_CHANNEL_SERIAL);
    CHECK(crux_cli_registry_ready());
}

static void recover_on_serial(void)
{
    uint8_t bytes[8];

    start_everything();
    cli_test_grant("network.write");
    cli_test_output_clear();
    CHECK(cli_test_session("network recover\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(strstr(cli_test_output(), "saved address ignored at the next boot\n") != NULL);
    CHECK_TEXT(cli_fake_access.last_permission, "network.write");
    /* Only the flag was saved, and as false. */
    CHECK(config_fault_write_log_count() == 1u);
    CHECK(strcmp(config_fault_write_log_key(0), CRUX_NETWORK_SETTINGS_KEY_USE_SAVED) == 0);
    CHECK(config_fault_bytes(CRUX_NETWORK_SETTINGS_KEY_USE_SAVED, bytes, sizeof(bytes)) == 5u);
    CHECK(bytes[4] == 0u);
    CHECK(events_ignored == 1u && events_failed == 0u && events_changed == 0u);
}

static void no_permission_means_no_command(void)
{
    start_everything();
    cli_test_output_clear();
    CHECK(cli_test_session("network recover\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK_TEXT(cli_test_output(), "> unknown command\n> ");
    CHECK(config_fault_writes() == 0u && events_ignored == 0u);
    CHECK(cli_fake_access.check_denials == 1);
}

static void not_on_ssh_in_any_form(void)
{
    int status = -1;

    start_everything();
    cli_test_grant("network.write");
    cli_test_output_clear();
    CHECK(cli_test_exec("network recover", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 127);
    CHECK_TEXT(cli_test_output(), "unknown command\n");
    cli_test_output_clear();
    CHECK(cli_test_exec("help network recover", CRUX_ACCESS_CHANNEL_SSH, &status) == CRUX_CLI_OK);
    CHECK(status == 127);
    CHECK(config_fault_writes() == 0u);
}

static void a_failed_save_is_said_in_words(void)
{
    start_everything();
    cli_test_grant("network.write");
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_ERR_IO, false});
    cli_test_output_clear();
    CHECK(cli_test_session("network recover\nhelp\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(strstr(cli_test_output(), "not saved, run the command again\n") != NULL);
    /* The session went on. */
    CHECK(strstr(cli_test_output(), "network recover - ") != NULL);
    CHECK(events_failed == 1u && events_ignored == 0u);
}

static void a_save_in_progress_is_said_in_words(void)
{
    crux_network_settings_t set = {0xC6336414u, 24u, false, 0u};

    start_everything();
    cli_test_grant("network.write");
    CHECK(crux_network_settings_request(CLI_FAKE_ACCESS_SESSION, &set, NULL) == CRUX_NETWORK_OK);
    cli_test_output_clear();
    CHECK(cli_test_session("network recover\n", false, CRUX_ACCESS_CHANNEL_SERIAL) ==
          CRUX_CLI_ERR_OUTPUT);
    CHECK(strstr(cli_test_output(), "busy, try again\n") != NULL);
    CHECK(config_fault_writes() == 0u);
}

/* The services start once, so each case runs in a child that starts them. */
static unsigned cases_run, cases_failed;

static void run_case(const char *name, void (*body)(void))
{
    int status = 0;
    pid_t child;

    fflush(NULL);
    child = fork();
    if (child == 0) {
        body();
        fflush(NULL);
        _exit(cli_test_failures == 0 ? 0 : 1);
    }
    waitpid(child, &status, 0);
    ++cases_run;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        printf("CASE FAILED: %s (status %d)\n", name, status);
        ++cases_failed;
    }
}

int main(void)
{
    run_case("the table is what the registry takes", the_table_is_what_the_registry_takes);
    run_case("recover on serial", recover_on_serial);
    run_case("no permission, no command", no_permission_means_no_command);
    run_case("not on SSH in any form", not_on_ssh_in_any_form);
    run_case("a failed save is said in words", a_failed_save_is_said_in_words);
    run_case("a save in progress is said in words", a_save_in_progress_is_said_in_words);
    printf("network_settings_cli: %u cases, %u failed\n", cases_run, cases_failed);
    return cases_failed == 0 ? 0 : 1;
}
