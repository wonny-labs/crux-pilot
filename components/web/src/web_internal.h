/* Private to the portable web core. */
#ifndef CRUX_WEB_INTERNAL_H
#define CRUX_WEB_INTERNAL_H

#include "crux_web_adapter.h"

#ifndef CRUX_WEB_EXCHANGE_MAX
/* One for each client the server allows: a client has one request in progress. */
#define CRUX_WEB_EXCHANGE_MAX 3
#endif
#ifndef CRUX_WEB_RESPONSE_MAX
/* Bytes of one copied response body. One buffer serves every exchange. */
#define CRUX_WEB_RESPONSE_MAX 4096u
#endif
#ifndef CRUX_WEB_EXTRA_HEADER_BYTES
/* Bytes for names and values of crux_web_add_header(): two Set-Cookie lines and a little more. */
#define CRUX_WEB_EXTRA_HEADER_BYTES 384u
#endif
#ifndef CRUX_WEB_EXTRA_HEADERS
#define CRUX_WEB_EXTRA_HEADERS 4u
#endif
/* Content-Type and the four security headers. */
#define CRUX_WEB_FIXED_HEADERS 5u
#define CRUX_WEB_PATH_PARAMS_MAX 4u

/* Copied so that a composition can place a request's lifetime rules in one type. */
struct crux_web_request {
    const crux_web_route_t *route;
    const crux_web_request_head_t *head;
    size_t path_length;
    size_t param_count;
    struct {
        size_t offset;
        size_t length;
    } params[CRUX_WEB_PATH_PARAMS_MAX];
    size_t body_length;
    size_t body_read;
    uint8_t *body;
    bool responded;
    bool static_response;
    bool close_after;
    crux_web_response_t *response; /* Set only while the handler runs. */
};

/* Whether the route's pattern takes the whole path, filling in its parameters. */
bool crux_web_route_matches(const char *pattern, const char *path, size_t path_length,
                            crux_web_request_t *request);

/* Find the route for a path and method. NULL answers 404, whichever part did not match. */
const crux_web_route_t *crux_web_route_find(const char *path, size_t path_length, unsigned method,
                                            crux_web_request_t *request);

/* Whether one table of routes is well formed and matches what the build read. */
bool crux_web_routes_check(const crux_web_route_t *const *table, const crux_web_route_t *expected,
                           size_t count);

/* The core's own answer, with the security headers. Status and code are the caller's constants. */
void crux_web_error_response(crux_web_response_t *response, int status, const char *code);

/* Empty the shared response headers before a handler runs. */
void crux_web_response_begin(void);

#ifdef CRUX_WEB_TESTING
/* Replaces the table's answer for the next lookups, to show what a hand-made route does. */
extern const crux_web_route_t *crux_web_core_test_route;
#endif

#endif /* CRUX_WEB_INTERNAL_H */
