/**
 * @file
 * @brief Status and configuration routes of the management web server.
 *
 * Internal to Crux, like crux_web_session.h: a composition compiles these sources with the web core
 * and names web_management_routes.c to crux_web_routes(). The routes call the status and
 * configuration services with the session that the web core found, and add nothing to what those
 * services decide:
 *
 * - GET /api/v1/status lists the status items this session may see.
 * - GET /api/v1/config lists the configuration parameters this session may see.
 * - GET and PUT /api/v1/config/{name} read and update one parameter, in memory.
 * - POST /api/v1/config/save queues a save on one bounded worker, and the list reports its outcome.
 *
 * The account routes are those of crux_web_session.h. The routes are described in openapi.yaml, the
 * management API specification, and docs/web-management-apis.md holds their design.
 */
#ifndef CRUX_WEB_MANAGEMENT_H
#define CRUX_WEB_MANAGEMENT_H

#include "crux_web_adapter.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRUX_WEB_MANAGEMENT_BODY_MAX
/**
 * Bytes of the largest response body of these routes, which is one document held in one buffer. A
 * list or report that does not fit gets 503 with the code `not_ready`. The web core's
 * CRUX_WEB_RESPONSE_MAX is the same size by default, and a composition that raises one raises both.
 */
#define CRUX_WEB_MANAGEMENT_BODY_MAX 4096u
#endif

/** Counters, for tests and the design's measurements. */
typedef struct {
    uint32_t saves_queued;   /**< Saves accepted for the worker. */
    uint32_t saves_busy;     /**< Saves refused because one was queued or running. */
    uint32_t saves_finished; /**< Saves the worker ended, whatever their result. */
} crux_web_management_stats_t;

/**
 * Reset the save state and take the lock and signal of the worker. After the services have
 * started, before crux_web_start(), and on one thread.
 *
 * @retval CRUX_WEB_ERR_FAILED The lock or the worker's signal could not be created.
 */
crux_web_result_t crux_web_management_init(void);

/** Start the one worker thread that runs configuration saves, at low priority. */
crux_web_result_t crux_web_management_start_worker(void);

/** A snapshot of the counters. */
void crux_web_management_stats_get(crux_web_management_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_WEB_MANAGEMENT_H */
