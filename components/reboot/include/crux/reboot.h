/**
 * @file
 * @brief A restart that an authorised user requests and the application can refuse.
 *
 * Thread context only. crux_system_reset() does not flush storage, drain logs or perform
 * safe-state actions, so a request for a restart goes through this service. It checks the
 * permission `system.reboot` and the composition's policy, records an event, and leaves the reset
 * to a worker, which gives events and saves a bounded time to finish before it checks the policy
 * again and resets.
 *
 * A composition that includes the service must state its policy. It registers a callback that can
 * refuse, or it selects the always-allow option by name. A configuration with neither fails
 * crux_reboot_start(), so an image cannot run the service by leaving the policy unset.
 *
 * Rebooting a controller while it runs a machine is a matter that only the application can judge.
 * The policy callback is that judgement, and it is asked twice: when the request arrives, and again
 * on the worker just before the reset, because the machine may have started in between.
 *
 * Boot order of a composition: Access and the Security events, crux_reboot_start() with the
 * policy, then crux_reboot_start_worker(). See docs/reboot-service.md.
 */
#ifndef CRUX_REBOOT_H
#define CRUX_REBOOT_H

#include <crux/access.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Permission that a request and a read of the state need. */
#define CRUX_REBOOT_PERMISSION "system.reboot"

/** Size of a refusal's reason with its terminator, so 63 bytes of text, as a status text value. */
#define CRUX_REBOOT_REASON_SIZE 64u

/** Wait sources that a configuration may name. */
#define CRUX_REBOOT_WAIT_SOURCE_MAX 8u

/**
 * What the service shows when the policy gave no usable reason: text that is empty, is not
 * terminated inside CRUX_REBOOT_REASON_SIZE, or breaks the character rule of
 * crux_config_text_valid().
 */
#define CRUX_REBOOT_DEFAULT_REASON "Refused by the application"

/** Results; zero is success. */
typedef enum {
    CRUX_REBOOT_OK = 0,
    CRUX_REBOOT_ERR_INVALID_ARGUMENT = -1,
    CRUX_REBOOT_ERR_NOT_READY = -2,       /**< Not started, or a lock could not be created. */
    CRUX_REBOOT_ERR_SESSION_INVALID = -3, /**< Never valid, ended or expired. */
    CRUX_REBOOT_ERR_DENIED = -4,          /**< The session lacks `system.reboot`. */
    CRUX_REBOOT_ERR_REFUSED = -5,         /**< The composition's policy refused. See the reason. */
    CRUX_REBOOT_ERR_BUSY = -6,            /**< A lock did not come in time. Retry later. */
    CRUX_REBOOT_ERR_NO_POLICY = -7        /**< The configuration states no policy. */
} crux_reboot_result_t;

/** How the composition states its policy. Zero is unset, and an unset policy fails the start. */
typedef enum {
    CRUX_REBOOT_POLICY_UNSET = 0,
    CRUX_REBOOT_POLICY_CALLBACK = 1,    /**< The callback decides. */
    CRUX_REBOOT_POLICY_ALWAYS_ALLOW = 2 /**< The application states that it never refuses. */
} crux_reboot_policy_kind_t;

/** Where a callback writes the reason of a refusal. Zeroed before every call. */
typedef struct {
    /** Well-formed UTF-8 of 1 to 63 bytes with no control character, ending in a NUL. */
    char reason[CRUX_REBOOT_REASON_SIZE];
} crux_reboot_refusal_t;

/**
 * The application's decision. It runs on the server thread for a request, and on the service's
 * worker thread for the second check, so it must be thread-safe, quick, and make no call into this
 * service. It may be called more than once for one request.
 *
 * @param context The configuration's policy_context.
 * @param[out] refusal Receives the reason when the callback refuses. Text that does not follow the
 * rule on reason is replaced by CRUX_REBOOT_DEFAULT_REASON.
 * @return True to allow the restart, false to refuse it.
 */
typedef bool (*crux_reboot_policy_t)(void *context, crux_reboot_refusal_t *refusal);

/**
 * Whether work that a restart would interrupt is still in flight. It runs on the worker thread
 * only, so it may take a short lock, and it must not wait on storage.
 *
 * @return True while the work is pending.
 */
typedef bool (*crux_reboot_busy_t)(void *context);

/**
 * One kind of work that the worker waits for before it resets, for a save that the service cannot
 * see on its own, such as the application's own save. Events are always waited for, and they need
 * no entry.
 */
typedef struct {
    crux_reboot_busy_t busy;
    void *context;
    /**
     * True if the restart must not happen while this work is still pending when the wait ends. The
     * request then fails and the work is left untouched. False if the restart goes ahead and the
     * work may be cut.
     */
    bool blocks_reset;
} crux_reboot_wait_source_t;

/** What the composition gives the service. A zeroed value is invalid: the policy is unset. */
typedef struct {
    crux_reboot_policy_kind_t policy_kind;
    /** Required with CRUX_REBOOT_POLICY_CALLBACK, and NULL with the other kinds. */
    crux_reboot_policy_t policy;
    void *policy_context;
    /** Copied. May be NULL when the count is zero. */
    const crux_reboot_wait_source_t *wait_sources;
    size_t wait_source_count; /**< At most CRUX_REBOOT_WAIT_SOURCE_MAX. */
} crux_reboot_config_t;

/** The state of the latest request, as a client sees it. */
typedef enum {
    CRUX_REBOOT_STATE_NONE = 0,    /**< No request since this boot. */
    CRUX_REBOOT_STATE_PENDING = 1, /**< Accepted, and the reset has not happened. */
    CRUX_REBOOT_STATE_REFUSED = 2, /**< The policy refused, at either check. */
    CRUX_REBOOT_STATE_FAILED = 3   /**< The restart did not happen. */
} crux_reboot_state_kind_t;

/** Why a restart failed, for an operator. It never reaches a client. */
typedef enum {
    CRUX_REBOOT_FAILURE_NONE = 0,
    CRUX_REBOOT_FAILURE_RESET_RETURNED =
        1, /**< The reset came back, which it only does on error. */
    CRUX_REBOOT_FAILURE_SAVE_IN_FLIGHT =
        2 /**< A blocking wait source stayed busy for the whole wait. */
} crux_reboot_failure_t;

/** What a read shows. The state stays until the next accepted request. */
typedef struct {
    crux_reboot_state_kind_t state;
    char reason[CRUX_REBOOT_REASON_SIZE]; /**< With REFUSED, otherwise empty. */
    crux_reboot_failure_t failure;        /**< With FAILED, otherwise NONE. */
} crux_reboot_state_t;

/**
 * Check the policy statement, create the locks and reset the state.
 *
 * @param config The composition's policy and wait sources. Copied, except that the callbacks and
 * contexts are borrowed for the program's life.
 * @retval CRUX_REBOOT_OK Ready. A second call after a success changes nothing and returns OK.
 * @retval CRUX_REBOOT_ERR_INVALID_ARGUMENT The configuration is missing, names a callback with
 * the always-allow option, names too many wait sources, or holds a wait source with no function.
 * @retval CRUX_REBOOT_ERR_NO_POLICY The policy is unset, or the callback kind has no callback.
 * @retval CRUX_REBOOT_ERR_NOT_READY A lock or signal could not be created.
 */
crux_reboot_result_t crux_reboot_start(const crux_reboot_config_t *config);

/**
 * Start the one worker thread that carries out requests, at low priority. A composition with no
 * thread calls crux_reboot_work_once() itself.
 *
 * @retval CRUX_REBOOT_OK Started, or already running.
 * @retval CRUX_REBOOT_ERR_NOT_READY Not started, or the thread could not be created.
 */
crux_reboot_result_t crux_reboot_start_worker(void);

/**
 * The state of the latest request, for GET.
 *
 * Needs `system.reboot`, checked with crux_access_check(), which records a denial. Polls its lock
 * for a short time and never waits on anything else.
 *
 * @param[out] state Unchanged on failure.
 * @retval CRUX_REBOOT_OK Copied.
 * @retval CRUX_REBOOT_ERR_INVALID_ARGUMENT The state is missing.
 * @retval CRUX_REBOOT_ERR_NOT_READY Not started.
 * @retval CRUX_REBOOT_ERR_BUSY The lock did not come in time.
 * @retval CRUX_REBOOT_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_REBOOT_ERR_DENIED The session lacks `system.reboot`.
 */
crux_reboot_result_t crux_reboot_get(crux_access_session_t session, crux_reboot_state_t *state);

/**
 * Ask for a restart.
 *
 * Needs `system.reboot`. A request made while another is pending is accepted and starts nothing
 * new: it does not run the policy. Otherwise the policy decides. A refusal records
 * `system.reboot_refused` and shows `refused` with its reason. An accepted request records
 * `system.reboot_requested` before it returns, and the worker then carries it out after a delay.
 * Never waits on anything but a short lock, so it may run on the server thread.
 *
 * @param[out] reason On a refusal, the reason of CRUX_REBOOT_REASON_SIZE bytes. May be NULL.
 * @retval CRUX_REBOOT_OK Accepted, and pending.
 * @retval CRUX_REBOOT_ERR_NOT_READY Not started.
 * @retval CRUX_REBOOT_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_REBOOT_ERR_DENIED The session lacks `system.reboot`.
 * @retval CRUX_REBOOT_ERR_REFUSED The policy refused. Nothing will happen.
 * @retval CRUX_REBOOT_ERR_BUSY A lock did not come in time. Nothing changed.
 */
crux_reboot_result_t crux_reboot_request(crux_access_session_t session,
                                         char reason[CRUX_REBOOT_REASON_SIZE]);

/**
 * Carry out the accepted request, if there is one, on the calling thread: the delay, the bounded
 * wait for events and saves, the second policy check, then the reset. The worker loops on it, and
 * a Host test calls it in place of the worker. It sleeps, and it does not return when a real reset
 * works.
 *
 * @return True if a request was carried out and ended in a refusal or a failure.
 */
bool crux_reboot_work_once(void);

#ifdef __cplusplus
}
#endif

#endif
