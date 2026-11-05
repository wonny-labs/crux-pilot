/* What the generated resources need from the Zephyr adapter. A project never includes this: the
 * build compiles the generated file with it. */
#ifndef CRUX_WEB_ZEPHYR_RESOURCES_H
#define CRUX_WEB_ZEPHYR_RESOURCES_H

#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/sys/util.h>

int crux_web_zephyr_callback(struct http_client_ctx *client, enum http_transaction_status status,
                             const struct http_request_ctx *request,
                             struct http_response_ctx *server_response, void *user_data);

/* The server sorts a service's resources by this name, so the generator numbers them. */
#define CRUX_WEB_ZEPHYR_RESOURCE(index, path, mask)                                                \
    static struct http_resource_detail_dynamic crux_web_detail_##index = {                         \
        .common = {.bitmask_of_supported_http_methods = (mask),                                    \
                   .type = HTTP_RESOURCE_TYPE_DYNAMIC},                                            \
        .cb = crux_web_zephyr_callback,                                                            \
    };                                                                                             \
    HTTP_RESOURCE_DEFINE(crux_web_resource_##index, crux_web_service, path,                        \
                         &crux_web_detail_##index)

#endif /* CRUX_WEB_ZEPHYR_RESOURCES_H */
