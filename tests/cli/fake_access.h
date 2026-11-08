/* A scripted crux_access for the CLI's Host tests, so that each row of a result table can be chosen
 * directly. The suites in login_real.c run the same tables on the real core. A test composition: it
 * defines the Access functions the CLI calls, so it can never link beside a real core. */
#ifndef CRUX_CLI_FAKE_ACCESS_H
#define CRUX_CLI_FAKE_ACCESS_H

#include <crux_access_adapter.h>

#define CLI_FAKE_ACCESS_GRANTS_MAX 8u
#define CLI_FAKE_ACCESS_ACCOUNTS_MAX 12u
#define CLI_FAKE_ACCESS_TEXT_MAX 256u

/* The session a successful fake login hands out. */
#define CLI_FAKE_ACCESS_SESSION ((crux_access_session_t){0x5A5Au})

typedef void (*cli_fake_access_hook_t)(void *context);

typedef struct {
    /* What each function answers. Everything starts at CRUX_ACCESS_OK. */
    crux_access_result_t login_result;
    uint32_t login_retry_after_ms;
    crux_access_result_t provision_result;
    crux_access_result_t touch_result;
    crux_access_result_t user_result;
    crux_access_result_t must_change_result;
    crux_access_result_t check_result; /* Not OK overrides the grants below. */
    crux_access_result_t permitted_result;
    crux_access_result_t change_result;
    uint32_t change_retry_after_ms;
    crux_access_result_t list_result;
    bool must_change;
    /* The permissions the session holds. */
    const char *grants[CLI_FAKE_ACCESS_GRANTS_MAX];
    size_t grant_count;
    crux_access_account_info_t accounts[CLI_FAKE_ACCESS_ACCOUNTS_MAX];
    size_t account_count;

    /* What the CLI did. */
    unsigned login_calls;
    unsigned provision_calls;
    unsigned touch_calls;
    unsigned user_calls;
    unsigned must_change_calls;
    unsigned check_calls;
    unsigned check_denials; /* Each is where the real core would emit a denial event. */
    unsigned permitted_calls;
    unsigned change_calls;
    unsigned list_calls;
    unsigned logout_calls;
    char last_user[CLI_FAKE_ACCESS_TEXT_MAX];
    char last_password[CLI_FAKE_ACCESS_TEXT_MAX];
    size_t last_length;
    char last_current[CLI_FAKE_ACCESS_TEXT_MAX];
    size_t last_current_length;
    char last_role[CLI_FAKE_ACCESS_TEXT_MAX];
    char last_permission[CLI_FAKE_ACCESS_TEXT_MAX];
    crux_access_origin_t last_origin;
    crux_access_session_t last_session;
    crux_access_session_t last_logout;

    /* Run inside a login or provisioning call, after its arguments are recorded. */
    cli_fake_access_hook_t on_verify;
    void *hook_context;
} cli_fake_access_t;

extern cli_fake_access_t cli_fake_access;

void cli_fake_access_reset(void);

#endif
