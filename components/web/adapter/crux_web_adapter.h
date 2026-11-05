/**
 * @file
 * @brief What a transport adapter and the session and asset compositions use of the web core.
 *
 * Internal to Crux, like crux_access_adapter.h: link crux_web_adapter PRIVATE, so that this
 * directory never reaches a project. Nothing here names Zephyr or a socket, so a Host test drives
 * the same core with a fake transport.
 *
 * Every function runs on the one server thread. Calls for different exchanges may interleave
 * between body chunks, but a response must be sent before the next call, because all exchanges
 * share one response buffer.
 */
#ifndef CRUX_WEB_ADAPTER_H
#define CRUX_WEB_ADAPTER_H

#include <crux/web.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What the server parsed of a request. Strings stay valid until the exchange closes. */
typedef struct {
    unsigned method; /**< A CRUX_WEB_* bit, or zero for one that no route can take. */
    const char *url; /**< Path and optional query, after the server's dot-segment removal. */
    const char *cookie;
    const char *origin;
    const char *host;
    const char *xsrf_token;
    const char *crux_request;
    const char *content_type;
    const char *content_length;
    bool headers_incomplete; /**< A capture dropped a header, or none was available. Never no
                                session. */
    bool duplicate_header;   /**< A captured header arrived twice. */
    bool chunked;            /**< The request uses Transfer-Encoding: chunked. */
    bool upgrade;            /**< The request asks for a protocol upgrade. */
} crux_web_request_head_t;

/** The outcome of checking a request's credentials. */
typedef enum {
    CRUX_WEB_AUTH_OK = 0,          /**< The session is set. */
    CRUX_WEB_AUTH_NO_SESSION,      /**< 401 unauthorized. */
    CRUX_WEB_AUTH_CSRF_REJECTED,   /**< 403 csrf_rejected. */
    CRUX_WEB_AUTH_ORIGIN_REJECTED, /**< 403 origin_rejected. */
    CRUX_WEB_AUTH_NOT_READY        /**< 503 not_ready. */
} crux_web_auth_t;

/**
 * Finds the session of a request, on the server thread, and may not block. It runs for every route
 * that is not public, before the body is read and the access class is checked, and it owns the CSRF
 * and Origin rules and the idle timer. Issue #186 implements it.
 */
typedef crux_web_auth_t (*crux_web_authenticate_t)(const crux_web_request_head_t *head,
                                                   crux_access_session_t *session, void *context);

/** The core's fixed settings. */
typedef struct {
    crux_web_authenticate_t authenticate; /**< NULL answers every non-public route with 503. */
    void *authenticate_context;
    uint32_t request_deadline_ms; /**< From the first callback to the last body byte. */
    uint32_t handler_limit_ms;    /**< Declared time limit, counted when exceeded. */
} crux_web_config_t;

/** One name and value of a response. */
typedef struct {
    const char *name;
    const char *value;
} crux_web_header_t;

/**
 * The most headers on a response. Each is three sends on one callback, and each send can wait out
 * the send timeout on the server thread, so this keeps the worst stall under the inactivity timer
 * at the defaults: (6 + 3 x 11) x 250 ms = 9.75 s, against 10 s. See docs/web-contract.md.
 */
#define CRUX_WEB_RESPONSE_HEADERS_MAX 11u

/** A response for the transport to send. See crux_web_response_generation() for how long it lasts.
 */
typedef struct {
    int status;
    const crux_web_header_t *headers;
    size_t header_count;
    const void *body;
    size_t body_length;
} crux_web_response_t;

/** What the transport does next. */
typedef enum {
    CRUX_WEB_STEP_MORE = 0,      /**< Wait for more body. */
    CRUX_WEB_STEP_RESPOND,       /**< Send the response, and keep the connection. */
    CRUX_WEB_STEP_RESPOND_CLOSE, /**< Send the response if possible, then close: body may still
                                    come. */
    CRUX_WEB_STEP_CLOSE          /**< Close at once, sending nothing. */
} crux_web_step_t;

/** One request in progress. */
typedef struct crux_web_exchange crux_web_exchange_t;

/** Counters on the server thread, for tests and the design's measurements. */
typedef struct {
    uint32_t requests;
    uint32_t rejected;
    uint32_t handlers;
    uint32_t deadlines;
    uint32_t response_deadlines;
    uint32_t aborted;
    uint32_t handler_overruns;
    uint32_t handler_max_ms;
} crux_web_stats_t;

/** Reset every exchange and set the configuration. Before the transport starts. */
void crux_web_core_init(const crux_web_config_t *config);

/**
 * Check the route table that the image linked, against what the build read from its sources. Stops
 * a start if a macro changed a value between the two, or a route is malformed.
 */
bool crux_web_routes_valid(void);

/** Changes each time the core builds a response in its shared store. */
uint32_t crux_web_response_generation(void);

/** Take an exchange from the pool, or NULL when every one is in use. */
crux_web_exchange_t *crux_web_exchange_open(void);

/**
 * Begin a request from its headers: the request's form first, then the whole-path comparison, then
 * the body limit, then the access class, so that an unknown path costs no session lookup. Nothing
 * runs the handler here.
 */
crux_web_step_t crux_web_exchange_start(crux_web_exchange_t *exchange,
                                        const crux_web_request_head_t *head,
                                        crux_web_response_t *response);

/**
 * Hand over a piece of the body. After the last piece the core checks access again if it took more
 * than this one call, then runs the handler.
 *
 * @param last This is the final call for the request. @p data may be empty.
 */
crux_web_step_t crux_web_exchange_data(crux_web_exchange_t *exchange, const void *data,
                                       size_t length, bool last, crux_web_response_t *response);

/** True once a response has been produced, so another call for this exchange is a protocol fault.
 */
bool crux_web_exchange_finished(const crux_web_exchange_t *exchange);

/** Release an exchange and clear its body, whether it completed or was aborted. */
void crux_web_exchange_close(crux_web_exchange_t *exchange, bool aborted);

/** A snapshot of the counters. */
void crux_web_stats_get(crux_web_stats_t *stats);

/** The transport cut a response that ran over its deadline. */
void crux_web_stats_count_response_deadline(void);

/** The headers of a public route's request, for login and its attempt poll. */
const crux_web_request_head_t *crux_web_request_head(const crux_web_request_t *request);

/**
 * Respond once with a body that outlives the request, and a header list that the caller owns, for
 * firmware-embedded assets. The core adds nothing, so the caller supplies Content-Type and the
 * security headers.
 *
 * @param header_count One to CRUX_WEB_RESPONSE_HEADERS_MAX.
 */
crux_web_result_t crux_web_respond_static(crux_web_request_t *request, int status,
                                          const crux_web_header_t *headers, size_t header_count,
                                          const void *body, size_t length);

/* A composition starts and stops the transport. Implemented by the adapter of the platform. */

/** Start serving. @retval CRUX_WEB_OK Started. */
crux_web_result_t crux_web_start(const crux_web_config_t *config);
/** Stop serving, closing every client. */
crux_web_result_t crux_web_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_WEB_ADAPTER_H */
