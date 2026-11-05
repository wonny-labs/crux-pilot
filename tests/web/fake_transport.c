/* See fake_transport.h. */
#include "fake_transport.h"

#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define FAKE_CLIENTS 4u
#define FAKE_RESOURCES 64u

static fake_client_t fake_clients[FAKE_CLIENTS];
static fake_client_t *fake_holders[FAKE_RESOURCES];

static void store_response(fake_client_t *client, const crux_web_response_t *response)
{
    fake_reply_t *reply = &client->reply;

    reply->origin = FAKE_FROM_CORE;
    reply->status = response->status;
    reply->body_length =
        response->body_length < FAKE_BODY_MAX - 1u ? response->body_length : FAKE_BODY_MAX - 1u;
    if (response->body_length != 0u) {
        memcpy(reply->body, response->body, reply->body_length);
    }
    reply->body[reply->body_length] = '\0';
    reply->header_count =
        response->header_count < FAKE_HEADERS_MAX ? response->header_count : FAKE_HEADERS_MAX;
    for (size_t index = 0; index < reply->header_count; ++index) {
        snprintf(reply->names[index], sizeof(reply->names[index]), "%s",
                 response->headers[index].name);
        snprintf(reply->values[index], sizeof(reply->values[index]), "%s",
                 response->headers[index].value);
    }
}

static void server_reply(fake_client_t *client, int status, const char *text)
{
    memset(&client->reply, 0, sizeof(client->reply));
    client->reply.origin = FAKE_FROM_SERVER;
    client->reply.status = status;
    snprintf(client->reply.body, sizeof(client->reply.body), "%s", text);
    client->reply.body_length = strlen(text);
}

/* The transaction is over: the server clears its holder and tells the callback. */
static void finish(fake_client_t *client, bool aborted)
{
    if (client->exchange != NULL) {
        crux_web_exchange_close(client->exchange, aborted);
        client->exchange = NULL;
    }
    if (client->holding >= 0) {
        fake_holders[client->holding] = NULL;
        client->holding = -1;
    }
}

static void drop(fake_client_t *client)
{
    finish(client, true);
    client->open = false;
    client->reply.closed = true;
}

static void apply_step(fake_client_t *client, crux_web_step_t step,
                       const crux_web_response_t *response)
{
    switch (step) {
    case CRUX_WEB_STEP_MORE:
        break;
    case CRUX_WEB_STEP_RESPOND:
        store_response(client, response);
        finish(client, false);
        break;
    case CRUX_WEB_STEP_RESPOND_CLOSE:
        store_response(client, response);
        drop(client);
        break;
    case CRUX_WEB_STEP_CLOSE:
        client->reply.origin = FAKE_NONE;
        drop(client);
        break;
    }
}

void fake_transport_reset(void)
{
    memset(fake_clients, 0, sizeof(fake_clients));
    for (size_t index = 0; index < FAKE_RESOURCES; ++index) {
        fake_holders[index] = NULL;
    }
}

fake_client_t *fake_connect(void)
{
    for (size_t index = 0; index < FAKE_CLIENTS; ++index) {
        if (!fake_clients[index].open) {
            memset(&fake_clients[index], 0, sizeof(fake_clients[index]));
            fake_clients[index].open = true;
            fake_clients[index].holding = -1;
            return &fake_clients[index];
        }
    }
    return NULL;
}

void fake_disconnect(fake_client_t *client)
{
    drop(client);
}

int fake_resource_for(const char *url)
{
    size_t path_length = strcspn(url, "?");

    for (size_t index = 0; index < fake_resource_spec_count; ++index) {
        const char *pattern = fake_resource_specs[index].pattern;
        if (strlen(pattern) == path_length && strncmp(pattern, url, path_length) == 0) {
            return (int)index;
        }
    }
    for (size_t index = 0; index < fake_resource_spec_count; ++index) {
        if (fnmatch(fake_resource_specs[index].pattern, url, FNM_PATHNAME | FNM_LEADING_DIR) == 0) {
            return (int)index;
        }
    }
    return -1;
}

const char *fake_resource_pattern(int index)
{
    return index < 0 ? NULL : fake_resource_specs[index].pattern;
}

bool fake_resource_held(const char *pattern)
{
    for (size_t index = 0; index < fake_resource_spec_count; ++index) {
        if (strcmp(fake_resource_specs[index].pattern, pattern) == 0) {
            return fake_holders[index] != NULL;
        }
    }
    return false;
}

bool fake_begin(fake_client_t *client, unsigned method, const char *url,
                crux_web_request_head_t head)
{
    crux_web_response_t response;
    crux_web_step_t step;
    int resource;

    memset(&client->reply, 0, sizeof(client->reply));
    snprintf(client->url, sizeof(client->url), "%s", url);
    resource = fake_resource_for(client->url);
    if (resource < 0) {
        server_reply(client, 404, "Not Found");
        return false;
    }
    if ((fake_resource_specs[resource].methods & method) == 0u) {
        server_reply(client, 405, "Method Not Allowed");
        return false;
    }
    if (fake_holders[resource] != NULL && fake_holders[resource] != client) {
        server_reply(client, 409, "");
        drop(client);
        return false;
    }
    fake_holders[resource] = client;
    client->holding = resource;
    client->exchange = crux_web_exchange_open();
    if (client->exchange == NULL) {
        drop(client);
        return false;
    }
    client->head = head;
    client->head.method = method;
    client->head.url = client->url;
    step = crux_web_exchange_start(client->exchange, &client->head, &response);
    apply_step(client, step, &response);
    return client->exchange != NULL;
}

void fake_body(fake_client_t *client, const void *data, size_t length, bool last)
{
    crux_web_response_t response;
    crux_web_step_t step;

    if (client->exchange == NULL) {
        return;
    }
    step = crux_web_exchange_data(client->exchange, data, length, last, &response);
    apply_step(client, step, &response);
}

const fake_reply_t *fake_request(fake_client_t *client, const char *url, unsigned method,
                                 fake_cookie_t cookie)
{
    crux_web_request_head_t head;

    memset(&head, 0, sizeof(head));
    head.cookie = cookie;
    if (fake_begin(client, method, url, head)) {
        fake_body(client, NULL, 0u, true);
    }
    return &client->reply;
}

const char *fake_reply_header(const fake_reply_t *reply, const char *name)
{
    for (size_t index = 0; index < reply->header_count; ++index) {
        if (strcasecmp(reply->names[index], name) == 0) {
            return reply->values[index];
        }
    }
    return NULL;
}
