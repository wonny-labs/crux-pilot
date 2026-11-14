/**
 * @file
 * @brief Accounts, roles and permission checks shared by every management channel.
 *
 * Status: Experimental (ADR-0001 decision 4). Access has no Zephyr composition outside tests/ yet,
 * and a change is listed in the release notes.
 *
 * Thread context only. Buffers are borrowed for the call, and the policy stays borrowed for the
 * program's life. crux_access_init() must return before any other operation starts, and until it
 * succeeds every operation returns CRUX_ACCESS_ERR_NOT_READY.
 *
 * crux_access_check() and the session queries never wait on storage or password derivation. Account
 * operations can wait on storage, and those that set a password also take as long as the verifier,
 * so a server runs them on a worker. See docs/access-contract.md.
 */
#ifndef CRUX_ACCESS_H
#define CRUX_ACCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Buffer size for a user or role name, including the terminator. */
#define CRUX_ACCESS_NAME_SIZE 32u
/** Longest password any account can hold, in bytes. */
#define CRUX_ACCESS_PASSWORD_MAX 128u
/** Longest permission identifier in a policy, excluding the terminator. */
#define CRUX_ACCESS_PERMISSION_MAX 63u
/** Permission that makes a role an administrator role. */
#define CRUX_ACCESS_PERMISSION_MANAGE "access.manage"
/** Permission every role holds implicitly: change one's own password and keys, and log out. */
#define CRUX_ACCESS_PERMISSION_SELF "access.self"

/** Session reference holding a slot and a generation. Zero is never valid. Do not inspect it. */
typedef struct {
    uint32_t value;
} crux_access_session_t;

/** Access results; zero is success and every other value is a refusal. */
typedef enum {
    CRUX_ACCESS_OK = 0,
    CRUX_ACCESS_ERR_INVALID_ARGUMENT = -1,
    /** Not initialised, invalid policy, store unusable, or a crypto provider failure. */
    CRUX_ACCESS_ERR_NOT_READY = -2,
    CRUX_ACCESS_ERR_DENIED = -3,          /**< Bad credential, or no permission. */
    CRUX_ACCESS_ERR_SESSION_INVALID = -4, /**< Never valid, ended or expired. */
    CRUX_ACCESS_ERR_RETRY_LATER = -5,     /**< Delay window; nothing was verified. */
    CRUX_ACCESS_ERR_BUSY = -6,            /**< Another verification is running. */
    CRUX_ACCESS_ERR_NO_RESOURCES = -7,    /**< Session, account or key table full. */
    CRUX_ACCESS_ERR_NOT_FOUND = -8,
    CRUX_ACCESS_ERR_ALREADY_EXISTS = -9,
    CRUX_ACCESS_ERR_LAST_ADMINISTRATOR = -10,
    CRUX_ACCESS_ERR_PASSWORD_REJECTED = -11, /**< Fails the password rule. */
    CRUX_ACCESS_ERR_STORE = -12              /**< Save failed; RAM state is unchanged. */
} crux_access_result_t;

/** A role is a name and an exact list of permissions, with no wildcards or inheritance. */
typedef struct {
    const char *name;
    const char *const *permissions;
    size_t permission_count;
} crux_access_role_t;

/** Product policy. Name and permission strings are borrowed for the program's life. */
typedef struct {
    const crux_access_role_t *roles;
    size_t role_count;
    uint32_t session_idle_limit_s;
    uint32_t session_absolute_limit_s;
    /** NULL selects the reference rule of 12 to 128 bytes, with no composition rules. */
    bool (*password_acceptable)(const char *password, size_t length);
} crux_access_policy_t;

/** One account as listed to an administrator. Holds no credential. */
typedef struct {
    char user[CRUX_ACCESS_NAME_SIZE];
    char role[CRUX_ACCESS_NAME_SIZE];
    bool enabled;
    bool must_change_password;
    size_t key_count;
} crux_access_account_info_t;

/**
 * Validate a policy, draw the delay hash key and load the account table.
 *
 * Rejects an empty table, an empty or over-long name, a duplicate role or permission, zero limits,
 * an idle limit above the absolute limit, and a policy with no role holding access.manage. A
 * failure from the policy or the random source can be retried. A store that reports itself unusable
 * cannot: later calls return the same result without reading it again, so that a medium inserted
 * while running cannot reopen provisioning.
 *
 * @param policy Product policy, borrowed for the program's life.
 * @retval CRUX_ACCESS_OK Ready. The table is empty or valid.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT The policy is missing or invalid.
 * @retval CRUX_ACCESS_ERR_NOT_READY The random source or the store is unusable.
 * @retval CRUX_ACCESS_ERR_ALREADY_EXISTS Already initialised; nothing changed.
 */
crux_access_result_t crux_access_init(const crux_access_policy_t *policy);

/**
 * Decide whether a session may use a permission, by exact comparison with its role's list.
 *
 * Does not refresh the idle timer. A session under a forced password change is denied everything,
 * including access.self: it may only use crux_access_account_change_password() and log out.
 *
 * @retval CRUX_ACCESS_OK Permitted.
 * @retval CRUX_ACCESS_ERR_DENIED Not permitted, or the permission is unknown.
 * @retval CRUX_ACCESS_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT The permission is missing or empty.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised.
 */
crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission);

/**
 * Report whether a session may use a permission, for hiding what it cannot use.
 *
 * Decides as crux_access_check() does, and reports false during a forced password change. Emits no
 * event, refreshes no timer, touches no storage and changes no state, so an expired session stays
 * for the next operation to end and report.
 *
 * @param[out] permitted Unchanged on failure.
 * @retval CRUX_ACCESS_OK Reported, whichever the answer.
 * @retval CRUX_ACCESS_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT The permission or the output is missing, or the
 * permission is empty.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised.
 */
crux_access_result_t crux_access_permitted(crux_access_session_t session, const char *permission,
                                           bool *permitted);

/**
 * Copy where a session came from, for a service's own event records.
 *
 * Emits no event, refreshes no timer and changes no state.
 *
 * @param[out] channel 1 for serial, 2 for web and 3 for SSH. Unchanged on failure.
 * @param[out] source Network address, with unused bytes zero. Unchanged on failure.
 * @param[out] source_length Bytes used in source. Unchanged on failure.
 * @retval CRUX_ACCESS_OK Copied.
 * @retval CRUX_ACCESS_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT An output is missing.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised.
 */
crux_access_result_t crux_access_session_origin(crux_access_session_t session, uint8_t *channel,
                                                uint8_t source[16], uint8_t *source_length);

/**
 * Copy a session's user name.
 * @param[out] name Unchanged on failure.
 * @param size Size of name. CRUX_ACCESS_NAME_SIZE is always enough.
 * @retval CRUX_ACCESS_OK Copied.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT The buffer is missing or too small.
 * @retval CRUX_ACCESS_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised.
 */
crux_access_result_t crux_access_session_user(crux_access_session_t session, char *name,
                                              size_t size);

/**
 * Copy a session's role name.
 * @param[out] name Unchanged on failure.
 * @param size Size of name. CRUX_ACCESS_NAME_SIZE is always enough.
 * @retval CRUX_ACCESS_OK Copied.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT The buffer is missing or too small.
 * @retval CRUX_ACCESS_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised.
 */
crux_access_result_t crux_access_session_role(crux_access_session_t session, char *name,
                                              size_t size);

/**
 * Report whether a session may only change its password and log out.
 * @param[out] required Unchanged on failure.
 * @retval CRUX_ACCESS_OK Reported.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT The output is missing.
 * @retval CRUX_ACCESS_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised.
 */
crux_access_result_t crux_access_session_must_change_password(crux_access_session_t session,
                                                              bool *required);

/*
 * Account operations. Unless stated otherwise each needs access.manage. Each returns
 * CRUX_ACCESS_ERR_NOT_READY before initialisation, CRUX_ACCESS_ERR_SESSION_INVALID for a session
 * that is not valid, CRUX_ACCESS_ERR_DENIED without the permission,
 * CRUX_ACCESS_ERR_INVALID_ARGUMENT for a malformed argument, and CRUX_ACCESS_ERR_STORE when a save
 * fails, leaving RAM state unchanged. See docs/access-contract.md for what the medium then holds.
 */

/**
 * List accounts in table order.
 * @param[out] accounts Receives up to capacity entries. May be NULL when capacity is zero.
 * @param capacity Number of entries accounts can hold.
 * @param[out] count Total number of accounts, which can exceed capacity.
 */
crux_access_result_t crux_access_account_list(crux_access_session_t session,
                                              crux_access_account_info_t *accounts, size_t capacity,
                                              size_t *count);

/**
 * Create an enabled account.
 * @param role Role name from the policy.
 * @param password Borrowed; the caller owns and clears it.
 * @retval CRUX_ACCESS_ERR_ALREADY_EXISTS The name is in use.
 * @retval CRUX_ACCESS_ERR_NO_RESOURCES The account table is full.
 * @retval CRUX_ACCESS_ERR_PASSWORD_REJECTED The password fails the rule.
 */
crux_access_result_t crux_access_account_create(crux_access_session_t session, const char *user,
                                                const char *role, const char *password,
                                                size_t length);

/**
 * Remove an account and end its sessions.
 * @retval CRUX_ACCESS_ERR_NOT_FOUND The account does not exist.
 * @retval CRUX_ACCESS_ERR_LAST_ADMINISTRATOR It is the last enabled administrator.
 */
crux_access_result_t crux_access_account_remove(crux_access_session_t session, const char *user);

/**
 * Disable an account and end its sessions. Disabling a disabled account changes nothing.
 * @retval CRUX_ACCESS_ERR_NOT_FOUND The account does not exist.
 * @retval CRUX_ACCESS_ERR_LAST_ADMINISTRATOR It is the last enabled administrator.
 */
crux_access_result_t crux_access_account_disable(crux_access_session_t session, const char *user);

/**
 * Enable an account. Enabling an enabled account changes nothing.
 * @retval CRUX_ACCESS_ERR_NOT_FOUND The account does not exist.
 */
crux_access_result_t crux_access_account_enable(crux_access_session_t session, const char *user);

/**
 * Move an account to another role and end its sessions. Its current role changes nothing.
 * @param role Role name from the policy.
 * @retval CRUX_ACCESS_ERR_NOT_FOUND The account does not exist.
 * @retval CRUX_ACCESS_ERR_LAST_ADMINISTRATOR It is the last enabled administrator.
 */
crux_access_result_t crux_access_account_set_role(crux_access_session_t session, const char *user,
                                                  const char *role);

/**
 * Set a new password as an administrator, force a change at next login and end the account's
 * sessions. Key login is refused until the change. Keys are not removed.
 * @param password Borrowed; the caller owns and clears it.
 * @retval CRUX_ACCESS_ERR_NOT_FOUND The account does not exist.
 * @retval CRUX_ACCESS_ERR_PASSWORD_REJECTED The password fails the rule.
 */
crux_access_result_t crux_access_account_reset_password(crux_access_session_t session,
                                                        const char *user, const char *password,
                                                        size_t length);

/**
 * Change the session's own password, needing access.self only, including under a forced change.
 *
 * Verifies the current password under the login delay and ends the account's other sessions. The
 * session that made the change continues.
 *
 * @param current Borrowed; the caller owns and clears it.
 * @param password New password, borrowed; the caller owns and clears it.
 * @param[out] retry_after_ms Remaining delay for CRUX_ACCESS_ERR_RETRY_LATER, otherwise zero.
 * @retval CRUX_ACCESS_ERR_DENIED The current password is wrong, or the account changed meanwhile.
 * @retval CRUX_ACCESS_ERR_RETRY_LATER Inside a delay window; nothing was verified.
 * @retval CRUX_ACCESS_ERR_BUSY Another verification is running on this channel.
 * @retval CRUX_ACCESS_ERR_PASSWORD_REJECTED The new password fails the rule.
 */
crux_access_result_t crux_access_account_change_password(crux_access_session_t session,
                                                         const char *current, size_t current_length,
                                                         const char *password, size_t length,
                                                         uint32_t *retry_after_ms);

/**
 * Add a public key. An account's own keys need access.self only. Sessions continue.
 * @param key SSH public key blob of type ssh-ed25519, ecdsa-sha2-nistp256, ecdsa-sha2-nistp384 or
 * ecdsa-sha2-nistp521, compared byte for byte. Any other type or form is an invalid argument.
 * @retval CRUX_ACCESS_ERR_NOT_FOUND The account does not exist.
 * @retval CRUX_ACCESS_ERR_ALREADY_EXISTS The account already lists this key.
 * @retval CRUX_ACCESS_ERR_NO_RESOURCES The account's key table is full.
 */
crux_access_result_t crux_access_account_add_key(crux_access_session_t session, const char *user,
                                                 const uint8_t *key, size_t key_length);

/**
 * Remove a public key and end the account's sessions. An account's own keys need access.self only.
 * @retval CRUX_ACCESS_ERR_NOT_FOUND The account or the key does not exist.
 */
crux_access_result_t crux_access_account_remove_key(crux_access_session_t session, const char *user,
                                                    const uint8_t *key, size_t key_length);

#ifdef __cplusplus
}
#endif

#endif
