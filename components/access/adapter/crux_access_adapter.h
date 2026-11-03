/**
 * @file
 * @brief Session creation for management channel adapters. Projects must not include this header.
 *
 * Thread context only. Buffers are borrowed for the call. Password login runs a slow verifier, so
 * an adapter calls it from a worker and never from a thread that other clients depend on. No
 * operation here sleeps: a delay is reported through a retry-after result.
 *
 * User names are C strings of 1 to 31 characters from [a-z0-9_.-]. A protocol's user name is
 * length-delimited, so an adapter rejects an embedded NUL before calling this contract. For IPv6,
 * an adapter passes the /64 prefix as the source.
 */
#ifndef CRUX_ACCESS_ADAPTER_H
#define CRUX_ACCESS_ADAPTER_H

#include <crux/access.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CRUX_ACCESS_CHANNEL_SERIAL = 1,
    CRUX_ACCESS_CHANNEL_WEB = 2,
    CRUX_ACCESS_CHANNEL_SSH = 3
} crux_access_channel_t;

/** Where an attempt comes from. */
typedef struct {
    crux_access_channel_t channel;
    uint8_t source[16];    /**< Network address, used only for delay tracking and events. */
    uint8_t source_length; /**< Bytes used in source, at most 16. */
} crux_access_origin_t;

/**
 * Verify a password and create a session.
 *
 * Results for a wrong password, an unknown name and a disabled account are the same.
 *
 * @param password Borrowed; the caller owns and clears it.
 * @param length Password length. Over 128 bytes is denied without verification.
 * @param[out] session Zero unless the result is CRUX_ACCESS_OK.
 * @param[out] retry_after_ms Remaining delay for CRUX_ACCESS_ERR_RETRY_LATER, otherwise zero.
 * @retval CRUX_ACCESS_OK Session created.
 * @retval CRUX_ACCESS_ERR_DENIED Rejected.
 * @retval CRUX_ACCESS_ERR_RETRY_LATER Inside a delay window; nothing was verified or counted.
 * @retval CRUX_ACCESS_ERR_BUSY Another verification is running on this channel's slot.
 * @retval CRUX_ACCESS_ERR_NO_RESOURCES No session slot; nothing was verified.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT A missing argument, or a name no account can hold.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised, store unusable, or a crypto provider failure.
 */
crux_access_result_t crux_access_login_password(const char *user, const char *password,
                                                size_t length, const crux_access_origin_t *origin,
                                                crux_access_session_t *session,
                                                uint32_t *retry_after_ms);

/**
 * Check whether an account lists a public key, for the SSH pre-check before a signature is verified
 * and for the signed request. Never creates a session.
 *
 * Applies the source delay but not the name delay. An account that cannot log in by key, because it
 * is unknown, disabled, under a forced password change or without a role, lists no keys. A signed
 * request rejected here counts as a key failure and produces an event. An unsigned probe records
 * nothing.
 *
 * @param signed_request True when the request carries a signature.
 * @param[out] retry_after_ms Remaining delay for CRUX_ACCESS_ERR_RETRY_LATER, otherwise zero.
 * @retval CRUX_ACCESS_OK The key is listed.
 * @retval CRUX_ACCESS_ERR_DENIED The key is not listed.
 * @retval CRUX_ACCESS_ERR_RETRY_LATER Inside a delay window; nothing was counted.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT A missing argument, or a name no account can hold.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised, store unusable, or a crypto provider failure.
 */
crux_access_result_t crux_access_key_is_authorised(const char *user, const uint8_t *key,
                                                   size_t key_length, bool signed_request,
                                                   const crux_access_origin_t *origin,
                                                   uint32_t *retry_after_ms);

/**
 * Create a session after the provider has verified a signature. Call it only after that check.
 *
 * Checks again that the key belongs to the account, refuses a disabled account or one with a forced
 * password change, and leaves delay state alone. A refusal produces an event only.
 *
 * @param[out] session Zero unless the result is CRUX_ACCESS_OK.
 * @retval CRUX_ACCESS_OK Session created.
 * @retval CRUX_ACCESS_ERR_DENIED Refused.
 * @retval CRUX_ACCESS_ERR_NO_RESOURCES No session slot.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT A missing argument, or a name no account can hold.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised or store unusable.
 */
crux_access_result_t crux_access_login_verified(const char *user, const uint8_t *key,
                                                size_t key_length,
                                                const crux_access_origin_t *origin,
                                                crux_access_session_t *session);

/**
 * Record a failed signature against the source delay, from the provider's failure branch.
 * @param user Supplied name, used in the event only if an account has it. May be NULL.
 * @param origin Where the attempt came from. Without it nothing is recorded.
 */
void crux_access_report_key_failure(const char *user, const crux_access_origin_t *origin);

/**
 * Create the first account, on the serial channel only, while the table loaded at start was empty
 * and no account has been created since.
 *
 * @param role Role name from the policy, which must hold access.manage.
 * @param password Borrowed; the caller owns and clears it.
 * @retval CRUX_ACCESS_OK Account created.
 * @retval CRUX_ACCESS_ERR_DENIED Not the serial channel, or provisioning is closed, which includes
 * an unusable store.
 * @retval CRUX_ACCESS_ERR_INVALID_ARGUMENT A missing argument, a malformed name, or a role that is
 * unknown or does not hold access.manage.
 * @retval CRUX_ACCESS_ERR_PASSWORD_REJECTED The password fails the rule.
 * @retval CRUX_ACCESS_ERR_STORE The save failed. Provisioning stays open.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised, or a crypto provider failure.
 */
crux_access_result_t crux_access_provision_first_account(const char *user, const char *password,
                                                         size_t length, const char *role,
                                                         const crux_access_origin_t *origin);

/**
 * Refresh a session's idle timer on user activity. The absolute limit still applies.
 * @retval CRUX_ACCESS_OK Refreshed.
 * @retval CRUX_ACCESS_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_ACCESS_ERR_NOT_READY Not initialised.
 */
crux_access_result_t crux_access_touch(crux_access_session_t session);

/**
 * End a session if it is valid, and clear the caller's reference.
 * @param[in,out] session May be NULL. Cleared in every other case.
 */
void crux_access_logout(crux_access_session_t *session);

#ifdef __cplusplus
}
#endif

#endif
