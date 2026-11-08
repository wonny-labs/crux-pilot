/**
 * @file
 * @brief Project routes, requests and responses of the management web server.
 *
 * A project defines a route with CRUX_WEB_ROUTE() and names the source file to the build, which
 * generates the route table. The access class is mandatory, and it is enforced before the handler
 * runs. Handlers see neither the HTTP server nor a socket.
 *
 * A handler runs on the single server thread and stalls every other client while it runs, so it has
 * a declared time limit and sends slow work to a worker. Every function here is for that thread,
 * for the request it was given, and only while its handler runs. See docs/web-contract.md.
 */
#ifndef CRUX_WEB_H
#define CRUX_WEB_H

#include <crux/access.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Method bits for a route. HEAD and OPTIONS are not routed, and the server answers them itself. */
#define CRUX_WEB_GET 0x01u
#define CRUX_WEB_POST 0x02u
#define CRUX_WEB_PUT 0x04u
#define CRUX_WEB_DELETE 0x08u
#define CRUX_WEB_PATCH 0x10u
#define CRUX_WEB_METHODS_ALL 0x1fu

/** Access class: any valid session. Handlers and services check per-resource permissions. */
#define CRUX_WEB_SESSION "@session"
/** Access class: no session. Reserved for login, its attempt result and static sign-in assets. */
#define CRUX_WEB_PUBLIC "@public"
/* Any other access class is a permission name, a string literal compared exactly by crux_access. */

#ifndef CRUX_WEB_BODY_BUFFER
/** Largest request body that any route may accept, set by the composition. */
#define CRUX_WEB_BODY_BUFFER 1024u
#endif
/** Request body limit of CRUX_WEB_ROUTE(). A larger Content-Length gets 413 before any is read. */
#define CRUX_WEB_BODY_DEFAULT 512u

/** Web results; zero is success. */
typedef enum {
    CRUX_WEB_OK = 0,
    CRUX_WEB_ERR_INVALID_ARGUMENT = -1,
    CRUX_WEB_ERR_TOO_LARGE = -2, /**< Does not fit; nothing changed. */
    CRUX_WEB_ERR_ALREADY_RESPONDED = -3,
    CRUX_WEB_ERR_NOT_FOUND = -4, /**< No such path parameter. */
    CRUX_WEB_ERR_FAILED = -5     /**< A handler's own failure. */
} crux_web_result_t;

/** One request, valid while its handler runs. */
typedef struct crux_web_request crux_web_request_t;

/**
 * A route's handler. The core has already compared the whole path and enforced the access class.
 *
 * @param session The caller's session. Its value is zero on a public route, which every crux_access
 * operation refuses.
 * @return CRUX_WEB_OK after a response. Returning anything else with no response made, or OK with
 * no response made, gives the client 503 with the code `not_ready`.
 */
typedef crux_web_result_t (*crux_web_handler_t)(crux_web_request_t *request,
                                                crux_access_session_t session, void *context);

/** Filled in by CRUX_WEB_ROUTE(). A route that the macro did not define is not in the table. */
typedef struct {
    const char *path;
    unsigned methods;
    const char *access;
    crux_web_handler_t handler;
    void *context;
    size_t body_max;
} crux_web_route_t;

/** @cond INTERNAL_HIDDEN */
#ifdef __cplusplus
#define CRUX_WEB_STATIC_ASSERT(condition, message) static_assert(condition, message)
#define CRUX_WEB_ROUTE_LINKAGE extern "C"
#else
#define CRUX_WEB_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#define CRUX_WEB_ROUTE_LINKAGE
#endif
/** @endcond */

/**
 * Define a route with a body limit.
 *
 * Wrapping @p access in empty literals makes anything but a string literal fail to compile, so
 * NULL, a variable or a number are all refused, and an empty access class fails the assertion.
 * Leaving the access class out is a wrong argument count. The build generator requires a literal
 * path, plain method bits and one of the access forms, and it stops on anything else.
 *
 * @param name Unique across the image. A duplicate fails to link.
 * @param path Fixed segments and whole-segment parameters: `/api/v1/session/attempt/{id}`.
 * @param methods One or more CRUX_WEB_GET to CRUX_WEB_PATCH joined with `|`.
 * @param access A permission name, CRUX_WEB_SESSION or CRUX_WEB_PUBLIC.
 * @param handler Called once, after the whole body has arrived.
 * @param context Passed to the handler, and borrowed for the program's life.
 * @param body_max Largest Content-Length accepted, at most CRUX_WEB_BODY_BUFFER.
 */
#define CRUX_WEB_ROUTE_BODY(name, path, methods, access, handler, context, body_max)               \
    CRUX_WEB_STATIC_ASSERT(sizeof("" access "") > 1u, "a route needs an access class");            \
    CRUX_WEB_STATIC_ASSERT(sizeof("" path "") > 1u, "a route needs a path");                       \
    CRUX_WEB_STATIC_ASSERT((methods) != 0u && ((methods) & ~CRUX_WEB_METHODS_ALL) == 0u,           \
                           "a route needs known methods");                                         \
    CRUX_WEB_STATIC_ASSERT((body_max) <= CRUX_WEB_BODY_BUFFER,                                     \
                           "a route's body limit must fit the body buffer");                       \
    CRUX_WEB_ROUTE_LINKAGE const crux_web_route_t crux_web_route_##name = {                        \
        "" path "", (methods), "" access "", (handler), (void *)(context), (body_max)}

/** Define a route with the default body limit. See CRUX_WEB_ROUTE_BODY(). */
#define CRUX_WEB_ROUTE(name, path, methods, access, handler, context)                              \
    CRUX_WEB_ROUTE_BODY(name, path, methods, access, handler, context, CRUX_WEB_BODY_DEFAULT)

/** The method bit of this request. */
unsigned crux_web_method(const crux_web_request_t *request);

/**
 * Copy a path parameter, as a terminated string. A parameter holds only unreserved URL characters,
 * so nothing in it is encoded, and it is never `.` or `..`.
 *
 * @param index Counts the route's parameters from zero, left to right.
 * @retval CRUX_WEB_ERR_NOT_FOUND The route has no such parameter.
 * @retval CRUX_WEB_ERR_TOO_LARGE It does not fit; the buffer holds an empty string.
 */
crux_web_result_t crux_web_path_param(const crux_web_request_t *request, size_t index, char *buffer,
                                      size_t size);

/**
 * Copy the raw query, without its `?`, as a terminated string. It is empty when there is none. It
 * is not decoded or validated.
 *
 * @retval CRUX_WEB_ERR_TOO_LARGE It does not fit; the buffer holds an empty string.
 */
crux_web_result_t crux_web_query(const crux_web_request_t *request, char *buffer, size_t size);

/** The request's Content-Type value, or NULL. It holds what the client sent, unvalidated. */
const char *crux_web_content_type(const crux_web_request_t *request);

/** Total bytes in the request body, all of which have arrived before the handler runs. */
size_t crux_web_body_length(const crux_web_request_t *request);

/**
 * Copy the next bytes of the body. The core clears its copy when the request ends, in every case.
 *
 * @param[out] transferred Zero at the end of the body.
 */
crux_web_result_t crux_web_read_body(crux_web_request_t *request, void *buffer, size_t length,
                                     size_t *transferred);

/**
 * Add a response header, copied, before responding. CR, LF and control characters are refused, as
 * are a name that the server or the core owns: Transfer-Encoding, Content-Length, Connection,
 * Content-Type and the security headers.
 *
 * @retval CRUX_WEB_ERR_TOO_LARGE The header store is full; nothing changed.
 */
crux_web_result_t crux_web_add_header(crux_web_request_t *request, const char *name,
                                      const char *value);

/**
 * Respond once, with the body copied. The response carries the security headers and no-store.
 *
 * @param status 200 to 599, but not 204 or 304: the server always sends a chunked body.
 * @param content_type NULL selects application/json.
 * @retval CRUX_WEB_ERR_TOO_LARGE The body exceeds the response buffer; the handler may still
 * respond.
 */
crux_web_result_t crux_web_respond(crux_web_request_t *request, int status,
                                   const char *content_type, const void *body, size_t length);

/**
 * Respond once with the JSON error form, `{"code":"..."}`.
 *
 * @param code A stable code from the ErrorCode list in web/api/openapi.yaml, of lower-case letters
 * and underscores.
 */
crux_web_result_t crux_web_respond_error(crux_web_request_t *request, int status, const char *code);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_WEB_H */
