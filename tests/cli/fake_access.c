#include "fake_access.h"

#include <string.h>

#ifndef CRUX_CLI_TEST_COMPOSITION
#error "The fake access control is a test composition, and must not reach a product image"
#endif

cli_fake_access_t cli_fake_access;

void cli_fake_access_reset(void)
{
    memset(&cli_fake_access, 0, sizeof(cli_fake_access));
}

static void copy_text(char *destination, const char *text)
{
    size_t length = text != NULL ? strlen(text) : 0;
    if (length >= CLI_FAKE_ACCESS_TEXT_MAX) {
        length = CLI_FAKE_ACCESS_TEXT_MAX - 1u;
    }
    if (length > 0) {
        memcpy(destination, text, length);
    }
    destination[length] = '\0';
}

static void copy_bytes(char *destination, const char *bytes, size_t length)
{
    if (length >= CLI_FAKE_ACCESS_TEXT_MAX) {
        length = CLI_FAKE_ACCESS_TEXT_MAX - 1u;
    }
    if (length > 0) {
        memcpy(destination, bytes, length);
    }
    destination[length] = '\0';
}

static void verify_hook(void)
{
    if (cli_fake_access.on_verify != NULL) {
        cli_fake_access.on_verify(cli_fake_access.hook_context);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the contract fixes this signature.
crux_access_result_t crux_access_login_password(const char *user, const char *password,
                                                size_t length, const crux_access_origin_t *origin,
                                                crux_access_session_t *session,
                                                uint32_t *retry_after_ms)
{
    cli_fake_access.login_calls += 1;
    copy_text(cli_fake_access.last_user, user);
    copy_bytes(cli_fake_access.last_password, password, length);
    cli_fake_access.last_length = length;
    cli_fake_access.last_origin = *origin;
    verify_hook();
    session->value = 0;
    *retry_after_ms = cli_fake_access.login_retry_after_ms;
    if (cli_fake_access.login_result == CRUX_ACCESS_OK) {
        *session = CLI_FAKE_ACCESS_SESSION;
    }
    return cli_fake_access.login_result;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the contract fixes this signature.
crux_access_result_t crux_access_provision_first_account(const char *user, const char *password,
                                                         size_t length, const char *role,
                                                         const crux_access_origin_t *origin)
{
    cli_fake_access.provision_calls += 1;
    copy_text(cli_fake_access.last_user, user);
    copy_bytes(cli_fake_access.last_password, password, length);
    cli_fake_access.last_length = length;
    copy_text(cli_fake_access.last_role, role);
    cli_fake_access.last_origin = *origin;
    verify_hook();
    return cli_fake_access.provision_result;
}

crux_access_result_t crux_access_touch(crux_access_session_t session)
{
    cli_fake_access.touch_calls += 1;
    cli_fake_access.last_session = session;
    return cli_fake_access.touch_result;
}

void crux_access_logout(crux_access_session_t *session)
{
    cli_fake_access.logout_calls += 1;
    if (session != NULL) {
        cli_fake_access.last_logout = *session;
        session->value = 0;
    }
}

crux_access_result_t crux_access_session_user(crux_access_session_t session, char *name,
                                              size_t size)
{
    cli_fake_access.user_calls += 1;
    cli_fake_access.last_session = session;
    if (cli_fake_access.user_result == CRUX_ACCESS_OK) {
        copy_bytes(name, "alice", size > 5u ? 5u : size - 1u);
    }
    return cli_fake_access.user_result;
}

crux_access_result_t crux_access_session_must_change_password(crux_access_session_t session,
                                                              bool *required)
{
    cli_fake_access.must_change_calls += 1;
    cli_fake_access.last_session = session;
    if (cli_fake_access.must_change_result == CRUX_ACCESS_OK) {
        *required = cli_fake_access.must_change;
    }
    return cli_fake_access.must_change_result;
}

static bool granted(const char *permission)
{
    for (size_t index = 0; index < cli_fake_access.grant_count; ++index) {
        if (strcmp(cli_fake_access.grants[index], permission) == 0) {
            return true;
        }
    }
    return false;
}

crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission)
{
    cli_fake_access.check_calls += 1;
    cli_fake_access.last_session = session;
    copy_text(cli_fake_access.last_permission, permission);
    if (cli_fake_access.check_result != CRUX_ACCESS_OK) {
        return cli_fake_access.check_result;
    }
    if (cli_fake_access.must_change || !granted(permission)) {
        cli_fake_access.check_denials += 1;
        return CRUX_ACCESS_ERR_DENIED;
    }
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_permitted(crux_access_session_t session, const char *permission,
                                           bool *permitted)
{
    cli_fake_access.permitted_calls += 1;
    cli_fake_access.last_session = session;
    if (cli_fake_access.permitted_result != CRUX_ACCESS_OK) {
        return cli_fake_access.permitted_result;
    }
    *permitted = !cli_fake_access.must_change && granted(permission);
    return CRUX_ACCESS_OK;
}

crux_access_result_t crux_access_account_change_password(crux_access_session_t session,
                                                         const char *current, size_t current_length,
                                                         const char *password, size_t length,
                                                         uint32_t *retry_after_ms)
{
    cli_fake_access.change_calls += 1;
    cli_fake_access.last_session = session;
    copy_bytes(cli_fake_access.last_current, current, current_length);
    cli_fake_access.last_current_length = current_length;
    copy_bytes(cli_fake_access.last_password, password, length);
    cli_fake_access.last_length = length;
    verify_hook();
    *retry_after_ms = cli_fake_access.change_retry_after_ms;
    if (cli_fake_access.change_result == CRUX_ACCESS_OK) {
        cli_fake_access.must_change = false;
    }
    return cli_fake_access.change_result;
}

crux_access_result_t crux_access_account_list(crux_access_session_t session,
                                              crux_access_account_info_t *accounts, size_t capacity,
                                              size_t *count)
{
    cli_fake_access.list_calls += 1;
    cli_fake_access.last_session = session;
    if (cli_fake_access.list_result != CRUX_ACCESS_OK) {
        return cli_fake_access.list_result;
    }
    size_t filled =
        cli_fake_access.account_count < capacity ? cli_fake_access.account_count : capacity;
    if (filled > 0) {
        memcpy(accounts, cli_fake_access.accounts, filled * sizeof(*accounts));
    }
    *count = cli_fake_access.account_count;
    return CRUX_ACCESS_OK;
}
