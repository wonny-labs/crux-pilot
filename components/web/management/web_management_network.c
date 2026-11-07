/* GET and PUT /api/v1/network. A composition that offers network settings names this source to
 * crux_web_routes() beside web_management_routes.c, and compiles web_management_network_json.c.
 *
 * Both routes have the permission as their access class, which the core enforces before the handler
 * and records as a denial, so neither is on the list of session-only routes. The service checks the
 * same permission again, and these handlers add no rule of their own: they read the body, call the
 * service and translate what it answers. A refusal never carries an address, a path or native text.
 *
 * The handlers run on the server thread. The service does not wait on storage: a PUT queues the
 * save for the service's worker, and GET shows its outcome. */
#include "web_management_network.h"
#include "web_management_internal.h"

#include <crux/network_settings.h>

#include <stdio.h>
#include <string.h>

#define PERMISSION_READ CRUX_NETWORK_SETTINGS_PERMISSION_READ
#define PERMISSION_WRITE CRUX_NETWORK_SETTINGS_PERMISSION_WRITE
#define STATE_BODY_MAX 512u
#define FIELD_BODY_MAX 320u

static crux_web_result_t respond_not_ready(crux_web_request_t *request)
{
    return crux_web_respond_error(request, 503, "not_ready");
}

/* What the service's refusals mean on the wire, for both routes. */
static crux_web_result_t respond_failure(crux_web_request_t *request, crux_network_result_t result)
{
    switch (result) {
    case CRUX_NETWORK_ERR_SESSION_INVALID:
        return crux_web_respond_error(request, 401, "unauthorized");
    case CRUX_NETWORK_ERR_DENIED:
        return crux_web_respond_error(request, 403, "permission_denied");
    case CRUX_NETWORK_ERR_BUSY:
        return crux_web_respond_error(request, 503, "busy");
    case CRUX_NETWORK_ERR_NOT_SUPPORTED:
        return crux_web_respond_error(request, 501, "not_supported");
    case CRUX_NETWORK_OK:
    case CRUX_NETWORK_ERR_INVALID_ARGUMENT:
    case CRUX_NETWORK_ERR_NOT_READY:
    case CRUX_NETWORK_ERR_REJECTED:
    case CRUX_NETWORK_ERR_STORE:
    default:
        return respond_not_ready(request);
    }
}

/* application/json, in any case, alone or with parameters after a semicolon. */
static bool json_content(const char *value)
{
    static const char expected[] = "application/json";

    if (value == NULL) {
        return false;
    }
    for (size_t index = 0; index < sizeof(expected) - 1u; ++index) {
        char character = value[index];

        if (character >= 'A' && character <= 'Z') {
            character = (char)(character + ('a' - 'A'));
        }
        if (character != expected[index]) {
            return false;
        }
    }
    value += sizeof(expected) - 1u;
    while (*value == ' ' || *value == '\t') {
        ++value;
    }
    return *value == '\0' || *value == ';';
}

/* GET. */

static void write_settings(web_mgmt_writer_t *writer, const crux_network_settings_t *settings)
{
    char text[CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE];

    crux_network_settings_format_ipv4(settings->address, text);
    web_mgmt_write(writer, "{\"address\":\"");
    web_mgmt_write(writer, text);
    web_mgmt_write(writer, "\",\"prefix_length\":");
    web_mgmt_write_u32(writer, settings->prefix_length);
    if (settings->has_gateway) {
        crux_network_settings_format_ipv4(settings->gateway, text);
        web_mgmt_write(writer, ",\"gateway\":\"");
        web_mgmt_write(writer, text);
        web_mgmt_write(writer, "\"");
    }
    web_mgmt_write(writer, "}");
}

static crux_web_result_t on_network_get(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    crux_network_state_t state;
    char body[STATE_BODY_MAX];
    web_mgmt_writer_t writer;
    crux_network_result_t result;

    (void)context;
    result = crux_network_settings_get(session, &state);
    if (result != CRUX_NETWORK_OK) {
        return respond_failure(request, result);
    }
    web_mgmt_writer_init(&writer, body, sizeof(body));
    web_mgmt_write(&writer, "{");
    if (state.has_running) {
        web_mgmt_write(&writer, "\"running\":");
        write_settings(&writer, &state.running);
        web_mgmt_write(&writer, ",");
    }
    if (state.has_saved) {
        web_mgmt_write(&writer, "\"saved\":");
        write_settings(&writer, &state.saved);
        web_mgmt_write(&writer, ",");
    }
    web_mgmt_write(&writer, state.source == CRUX_NETWORK_SOURCE_SAVED ? "\"source\":\"saved\""
                                                                      : "\"source\":\"default\"");
    web_mgmt_write(&writer, state.reboot_required ? ",\"reboot_required\":true"
                                                  : ",\"reboot_required\":false");
    if (state.save == CRUX_NETWORK_SAVE_PENDING) {
        web_mgmt_write(&writer, ",\"save\":{\"state\":\"pending\"}");
    } else if (state.save == CRUX_NETWORK_SAVE_FAILED) {
        web_mgmt_write(&writer, ",\"save\":{\"state\":\"failed\"}");
    }
    web_mgmt_write(&writer, "}");
    if (writer.overflow ||
        crux_web_respond(request, 200, NULL, writer.data, writer.used) != CRUX_WEB_OK) {
        return respond_not_ready(request);
    }
    return CRUX_WEB_OK;
}

/* PUT. */

static const char *field_name(crux_network_field_t field)
{
    switch (field) {
    case CRUX_NETWORK_FIELD_ADDRESS:
        return "address";
    case CRUX_NETWORK_FIELD_PREFIX_LENGTH:
        return "prefix_length";
    case CRUX_NETWORK_FIELD_GATEWAY:
    default:
        return "gateway";
    }
}

static const char *problem_name(crux_network_problem_t problem)
{
    switch (problem) {
    case CRUX_NETWORK_PROBLEM_NOT_UNICAST:
        return "not_unicast";
    case CRUX_NETWORK_PROBLEM_PREFIX_NOT_USABLE:
        return "prefix_not_usable";
    case CRUX_NETWORK_PROBLEM_GATEWAY_OUTSIDE_SUBNET:
    default:
        return "gateway_outside_subnet";
    }
}

static crux_web_result_t respond_findings(crux_web_request_t *request,
                                          const crux_network_findings_t *findings)
{
    char body[FIELD_BODY_MAX];
    web_mgmt_writer_t writer;

    web_mgmt_writer_init(&writer, body, sizeof(body));
    web_mgmt_write(&writer, "{\"code\":\"invalid_field\",\"fields\":[");
    for (size_t index = 0; index < findings->count; ++index) {
        web_mgmt_write(&writer, index > 0u ? ",{\"name\":\"" : "{\"name\":\"");
        web_mgmt_write(&writer, field_name(findings->items[index].field));
        web_mgmt_write(&writer, "\",\"code\":\"");
        web_mgmt_write(&writer, problem_name(findings->items[index].problem));
        web_mgmt_write(&writer, "\"}");
    }
    web_mgmt_write(&writer, "]}");
    if (writer.overflow ||
        crux_web_respond(request, 400, NULL, writer.data, writer.used) != CRUX_WEB_OK) {
        return respond_not_ready(request);
    }
    return CRUX_WEB_OK;
}

/* The body is read whole, and nothing in it is a secret, so there is nothing to wipe. The
 * connection closes after the answer all the same, as it does after every update of the other
 * management routes, so that a client's request is not kept in the server. The order is the content
 * type, the body, then the service: permission, rules, whether anything can be saved, and whether a
 * save is running. */
static crux_web_result_t on_network_put(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    uint8_t body[WEB_MGMT_NETWORK_BODY_MAX];
    size_t total = 0u;
    size_t transferred = 0u;
    crux_network_settings_t settings;
    crux_network_findings_t findings;
    crux_network_result_t result;

    (void)context;
    (void)crux_web_close_after_response(request);
    if (!json_content(crux_web_content_type(request))) {
        return crux_web_respond_error(request, 400, "bad_request");
    }
    do {
        if (crux_web_read_body(request, body + total, sizeof(body) - total, &transferred) !=
            CRUX_WEB_OK) {
            return crux_web_respond_error(request, 400, "bad_request");
        }
        total += transferred;
    } while (transferred != 0u && total < sizeof(body));
    if (!web_mgmt_parse_network(body, total, &settings)) {
        return crux_web_respond_error(request, 400, "bad_request");
    }
    result = crux_network_settings_request(session, &settings, &findings);
    switch (result) {
    case CRUX_NETWORK_OK:
        return crux_web_respond(request, 202, NULL, "{\"state\":\"pending\"}",
                                sizeof("{\"state\":\"pending\"}") - 1u);
    case CRUX_NETWORK_ERR_REJECTED:
        return respond_findings(request, &findings);
    case CRUX_NETWORK_ERR_INVALID_ARGUMENT:
    case CRUX_NETWORK_ERR_NOT_READY:
    case CRUX_NETWORK_ERR_SESSION_INVALID:
    case CRUX_NETWORK_ERR_DENIED:
    case CRUX_NETWORK_ERR_BUSY:
    case CRUX_NETWORK_ERR_NOT_SUPPORTED:
    case CRUX_NETWORK_ERR_STORE:
    default:
        return respond_failure(request, result);
    }
}

CRUX_WEB_ROUTE(mgmt_network_get, "/api/v1/network", CRUX_WEB_GET, "network.read", on_network_get,
               NULL);
CRUX_WEB_ROUTE_BODY(mgmt_network_put, "/api/v1/network", CRUX_WEB_PUT, "network.write",
                    on_network_put, NULL, WEB_MGMT_NETWORK_BODY_MAX);
