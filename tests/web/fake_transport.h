/* A stand-in for the Zephyr HTTP server, so that Host tests drive the real web core.
 *
 * It models what the server does with resources at v4.4.2: the exact pass, the wildcard pass in
 * generated order with FNM_PATHNAME and FNM_LEADING_DIR, the method mask, the one holder per
 * resource with its plain 409 and close, a plain 404 and 405, and releasing a holder when a client
 * goes away. The native_sim suite holds these same rules to the real server. */
#ifndef CRUX_WEB_FAKE_TRANSPORT_H
#define CRUX_WEB_FAKE_TRANSPORT_H

#include "crux_web_adapter.h"

#include <stdio.h>

typedef struct {
    const char *pattern;
    unsigned methods;
} fake_resource_spec_t;

extern const fake_resource_spec_t fake_resource_specs[];
extern const size_t fake_resource_spec_count;

/* Who answered: nobody yet, the server's own plain text, or the core. */
typedef enum { FAKE_NONE, FAKE_FROM_SERVER, FAKE_FROM_CORE } fake_origin_t;

#define FAKE_BODY_MAX 8192u
#define FAKE_HEADERS_MAX 16u

typedef struct {
    fake_origin_t origin;
    int status;
    bool closed; /* The connection is gone after this. */
    char body[FAKE_BODY_MAX];
    size_t body_length;
    size_t header_count;
    char names[FAKE_HEADERS_MAX][64];
    char values[FAKE_HEADERS_MAX][256];
} fake_reply_t;

typedef struct {
    bool open;
    int holding; /* Index of the resource this client holds, or -1. */
    crux_web_exchange_t *exchange;
    crux_web_request_head_t head;
    char url[256];
    fake_reply_t reply;
} fake_client_t;

/* Reset the transport's holders and connections. Call after crux_web_core_init(). */
void fake_transport_reset(void);

fake_client_t *fake_connect(void);
/* The peer goes away, with or without a request in progress. */
void fake_disconnect(fake_client_t *client);

/* Deliver the headers of a request. @p head has its url filled in here, and its other fields are
 * the caller's. Returns true when the body may follow. */
bool fake_begin(fake_client_t *client, unsigned method, const char *url,
                crux_web_request_head_t head);
/* Deliver a body chunk. */
void fake_body(fake_client_t *client, const void *data, size_t length, bool last);

/* The Cookie value, or NULL. */
typedef const char *fake_cookie_t;

/* A whole request with no body on one call. */
const fake_reply_t *fake_request(fake_client_t *client, const char *url, unsigned method,
                                 fake_cookie_t cookie);

/* Which resource holds the pattern, or -1, for ordering tests. */
int fake_resource_for(const char *url);
const char *fake_resource_pattern(int index);
bool fake_resource_held(const char *pattern);

const char *fake_reply_header(const fake_reply_t *reply, const char *name);

#endif /* CRUX_WEB_FAKE_TRANSPORT_H */
