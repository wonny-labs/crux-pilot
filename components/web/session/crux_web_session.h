/**
 * @file
 * @brief Login, sessions and request forgery protection of the management web server.
 *
 * Internal to Crux, like crux_web_adapter.h: a composition links crux_web_session PRIVATE, and a
 * project never sees this header. It implements crux_web_authenticate_t over crux_access, and the
 * session routes of web/api/openapi.yaml, which the composition names to crux_web_routes():
 *
 * - POST /api/v1/session queues a login on one bounded worker and returns an attempt identifier.
 * - GET /api/v1/session/attempt/{id} collects the result, and a success sets the cookies.
 * - DELETE /api/v1/session logs out, GET /api/v1/session/me reads the current user, and
 *   POST /api/v1/session/me/password queues a change of one's own password.
 *
 * The server thread never verifies a password. See docs/web-session.md.
 */
#ifndef CRUX_WEB_SESSION_H
#define CRUX_WEB_SESSION_H

#include "crux_web_adapter.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Random bytes in a session token, a CSRF token and an attempt identifier. */
#define CRUX_WEB_SESSION_TOKEN_BYTES 32u
/** Characters of one of those in unpadded base64url, without a terminator. */
#define CRUX_WEB_SESSION_TOKEN_TEXT 43u

/** The settings of the session module. Strings are borrowed for the program's life. */
typedef struct {
    /**
     * "https" in every product image. "http" only in a native_sim test image over plain HTTP,
     * where the composition must say so.
     */
    const char *scheme;
    /**
     * Exact Origin values to accept, compared whole. With none, the only accepted Origin is the
     * scheme, "://" and the request's own Host.
     */
    const char *const *origins;
    size_t origin_count;
} crux_web_session_config_t;

/** Counters, for tests and the design's measurements. */
typedef struct {
    uint32_t attempts;       /**< Attempts queued. */
    uint32_t busy;           /**< Logins and changes refused with 503 before queueing. */
    uint32_t verified;       /**< Attempts the worker finished. */
    uint32_t expired;        /**< Attempts that expired before anyone collected them. */
    uint32_t sessions;       /**< Sessions handed out by a poll. */
    uint32_t worker_max_ms;  /**< Longest attempt on the worker, by the web clock. */
} crux_web_session_stats_t;

/**
 * Reset the session and attempt tables and take the settings. Before crux_web_start(), on one
 * thread, and after crux_access_init().
 *
 * @retval CRUX_WEB_ERR_INVALID_ARGUMENT The settings are missing or the scheme is neither form.
 * @retval CRUX_WEB_ERR_FAILED The lock or the worker's signal could not be created.
 */
crux_web_result_t crux_web_session_init(const crux_web_session_config_t *config);

/** Start the one worker thread that verifies passwords, at low priority. */
crux_web_result_t crux_web_session_start_worker(void);

/**
 * The crux_web_authenticate_t of a composition, with a NULL context. It reads the crux_session
 * cookie, and on any method but GET it also checks Origin and the X-XSRF-TOKEN header, and refreshes
 * the session's idle timer. It never waits on storage or a password.
 */
crux_web_auth_t crux_web_session_authenticate(const crux_web_request_head_t *head,
                                              crux_access_session_t *session, void *context);

/** A snapshot of the counters. */
void crux_web_session_stats_get(crux_web_session_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_WEB_SESSION_H */
