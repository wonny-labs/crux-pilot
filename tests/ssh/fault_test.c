/*
 * Shows that the conformance cases can fail: each deliberate defect in the fake must be caught by
 * the case named for it. It keeps a defect from passing unseen when a real provider runs these
 * cases.
 */
#include "conformance.h"
#include "fake_provider.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    crux_ssh_fake_fault_t fault;
    const char *caught_by;
} expectation_t;

static const expectation_t expectations[] = {
    {CRUX_SSH_FAKE_FAULT_START_ACCEPTS_NO_HOST_KEY, "start_rejects_bad_arguments"},
    {CRUX_SSH_FAKE_FAULT_START_BORROWS_CALLBACKS, "start_copies_its_arguments"},
    {CRUX_SSH_FAKE_FAULT_RESTART_REFUSED, "start_and_stop_lifecycle"},
    {CRUX_SSH_FAKE_FAULT_UNKNOWN_RESULT_ACCEPTS, "unknown_result_denies"},
    {CRUX_SSH_FAKE_FAULT_NO_USER_CHECK, "bad_user_never_reaches_a_callback"},
    {CRUX_SSH_FAKE_FAULT_NO_ATTEMPT_LIMIT, "attempts_are_bounded"},
    {CRUX_SSH_FAKE_FAULT_NO_AUTH_TIME_LIMIT, "auth_time_is_bounded"},
    {CRUX_SSH_FAKE_FAULT_NO_CONNECTION_LIMIT, "connections_are_bounded"},
    {CRUX_SSH_FAKE_FAULT_ACCEPTS_RSA, "rsa_keys_are_refused"},
    {CRUX_SSH_FAKE_FAULT_SESSION_FROM_PROBE, "probe_never_creates_a_session"},
    {CRUX_SSH_FAKE_FAULT_SIGNATURE_NOT_CHECKED, "failed_signature_reports_and_denies"},
    {CRUX_SSH_FAKE_FAULT_KEY_FAILURE_SILENT, "failed_signature_reports_and_denies"},
    {CRUX_SSH_FAKE_FAULT_VERIFIED_RESULT_IGNORED, "verified_refusal_denies"},
    {CRUX_SSH_FAKE_FAULT_REQUEST_BEFORE_AUTH, "no_request_before_authentication"},
    {CRUX_SSH_FAKE_FAULT_ACCEPTS_OTHER_SUBSYSTEM, "other_requests_are_refused"},
    {CRUX_SSH_FAKE_FAULT_ACCEPTS_FORWARDING, "other_requests_are_refused"},
    {CRUX_SSH_FAKE_FAULT_ACCEPTS_SECOND_CHANNEL, "one_session_channel_per_connection"},
    {CRUX_SSH_FAKE_FAULT_WRONG_ATTACHMENT, "connections_keep_their_own_attachment"},
    {CRUX_SSH_FAKE_FAULT_NO_EXIT_STATUS, "shell_runs_and_returns_status"},
    {CRUX_SSH_FAKE_FAULT_EXIT_STATUS_NOT_CLAMPED, "exit_status_is_clamped"},
    {CRUX_SSH_FAKE_FAULT_COMMAND_NUL_PASSED, "exec_with_embedded_nul_is_refused"},
    {CRUX_SSH_FAKE_FAULT_READ_EOF_AS_TIMEOUT, "read_delivers_then_reports_eof"},
    {CRUX_SSH_FAKE_FAULT_READ_AFTER_CLOSE, "close_ends_this_side"},
    {CRUX_SSH_FAKE_FAULT_WRITE_IGNORES_WINDOW, "write_respects_the_window"},
    {CRUX_SSH_FAKE_FAULT_WRITE_DROP_HIDDEN, "write_reports_a_dropped_connection"},
    {CRUX_SSH_FAKE_FAULT_NO_EOF_ON_CLOSE, "close_ends_this_side"},
    {CRUX_SSH_FAKE_FAULT_FILE_HANDLE_NOT_SET, "sftp_attaches_a_file_handle"},
    {CRUX_SSH_FAKE_FAULT_NO_SFTP_CLOSE, "sftp_attaches_a_file_handle"},
    {CRUX_SSH_FAKE_FAULT_CLOSED_BEFORE_SFTP_CLOSE, "disconnect_during_sftp_closes_in_order"},
    {CRUX_SSH_FAKE_FAULT_NO_CONNECTION_CLOSED, "connection_closed_runs_once"},
    {CRUX_SSH_FAKE_FAULT_CONNECTION_CLOSED_TWICE, "connection_closed_runs_once"},
    {CRUX_SSH_FAKE_FAULT_STOP_KEEPS_CONNECTIONS, "stop_ends_connections"},
    {CRUX_SSH_FAKE_FAULT_STOP_FROM_CALLBACK, "stop_from_a_callback_is_refused"},
    {CRUX_SSH_FAKE_FAULT_PASSWORD_WRONG_LENGTH, "password_accept_attaches"},
    {CRUX_SSH_FAKE_FAULT_PASSWORD_WRONG_BYTES, "password_accept_attaches"},
    {CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_PASSWORD, "callbacks_get_their_own_peer"},
    {CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_PROBE, "callbacks_get_their_own_peer"},
    {CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_SIGNED, "callbacks_get_their_own_peer"},
    {CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_VERIFIED, "callbacks_get_their_own_peer"},
    {CRUX_SSH_FAKE_FAULT_PEER_WRONG_ON_FAILED, "callbacks_get_their_own_peer"},
    {CRUX_SSH_FAKE_FAULT_SIGNED_CHECK_EMPTY_KEY, "session_only_after_verified_signature"},
    {CRUX_SSH_FAKE_FAULT_NOT_COUNTED_REFUSED_SIGNED, "each_refusal_counts"},
    {CRUX_SSH_FAKE_FAULT_NOT_COUNTED_VERIFIED_REFUSAL, "each_refusal_counts"},
    {CRUX_SSH_FAKE_FAULT_NOT_COUNTED_REFUSED_TYPE, "each_refusal_counts"},
    {CRUX_SSH_FAKE_FAULT_SFTP_CLOSE_WRONG_HANDLE, "sftp_attaches_a_file_handle"},
    {CRUX_SSH_FAKE_FAULT_SFTP_NULL_HANDLE_ACCEPTED,
     "sftp_open_without_a_handle_ends_the_connection"},
    {CRUX_SSH_FAKE_FAULT_STOP_SKIPS_SFTP_CLOSE, "stop_during_sftp_closes_in_order"},
    {CRUX_SSH_FAKE_FAULT_READ_AFTER_DROP_OK, "write_reports_a_dropped_connection"},
    {CRUX_SSH_FAKE_FAULT_SECOND_LOGIN_OVERWRITES, "second_login_is_refused"},
    {CRUX_SSH_FAKE_FAULT_TERMINAL_ALWAYS_FALSE, "shell_is_told_about_a_terminal"},
    {CRUX_SSH_FAKE_FAULT_TERMINAL_ALWAYS_TRUE, "shell_is_told_about_a_terminal"},
    {CRUX_SSH_FAKE_FAULT_TERMINAL_REQUEST_REFUSED, "shell_is_told_about_a_terminal"},
    {CRUX_SSH_FAKE_FAULT_SESSION_ALIVE_IGNORED, "sftp_ends_when_the_session_has_gone"},
    {CRUX_SSH_FAKE_FAULT_SESSION_ALIVE_WRONG_ATTACHMENT, "sftp_ends_when_the_session_has_gone"},
    {CRUX_SSH_FAKE_FAULT_OPENING_NONE_COUNTED, "opening_none_does_not_count"},
    {CRUX_SSH_FAKE_FAULT_NAME_CHANGE_ALLOWED, "changing_the_user_ends_the_connection"},
    {CRUX_SSH_FAKE_FAULT_EMPTY_NAME_KEEPS_CONNECTION, "bad_user_never_reaches_a_callback"},
};

static crux_ssh_fake_fault_t active_fault;

static void reset_with_fault(void)
{
    crux_ssh_fake_reset();
    crux_ssh_fake_set_fault(active_fault);
}

static const crux_ssh_conformance_case_t *find_case(const char *name)
{
    for (size_t i = 0; i < crux_ssh_conformance_case_count; ++i) {
        if (strcmp(crux_ssh_conformance_cases[i].name, name) == 0) {
            return &crux_ssh_conformance_cases[i];
        }
    }
    return NULL;
}

int main(void)
{
    const size_t count = sizeof(expectations) / sizeof(expectations[0]);
    unsigned problems = 0;
    bool covered[CRUX_SSH_FAKE_FAULT_COUNT] = {false};

    for (size_t i = 0; i < count; ++i) {
        const crux_ssh_conformance_case_t *test = find_case(expectations[i].caught_by);

        covered[expectations[i].fault] = true;
        active_fault = expectations[i].fault;
        if (test == NULL) {
            printf("No case named %s\n", expectations[i].caught_by);
            ++problems;
            continue;
        }
        if (crux_ssh_conformance_run_case(test, crux_ssh_fake_client(), reset_with_fault, false) ==
            0) {
            printf("Fault %d went unseen by %s\n", (int)active_fault, test->name);
            ++problems;
        }
    }
    for (int fault = CRUX_SSH_FAKE_FAULT_NONE + 1; fault < CRUX_SSH_FAKE_FAULT_COUNT; ++fault) {
        if (!covered[fault]) {
            printf("Fault %d has no expectation\n", fault);
            ++problems;
        }
    }
    crux_ssh_fake_reset();
    printf("%zu deliberate defects, %u problems\n", count, problems);
    return problems == 0 ? 0 : 1;
}
