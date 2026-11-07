/* GET and POST /api/v1/system/reboot. A composition that includes the reboot service names this
 * source to crux_web_routes(), and a composition that does not leaves it out, so that it has no
 * reboot route at all: a path with no route is answered 404 by the core before any handler.
 *
 * Both routes have the permission as their access class, which the core enforces before the handler
 * and records as a denial, so neither is on the list of session-only routes. The service checks the
 * same permission again. These handlers add no rule of their own: they translate what the service
 * answers. A refusal never carries a path or native text, and the only text from the application is
 * the reason of a refusal, which the service has held to the character rule and to 63 bytes.
 *
 * The handlers run on the server thread. The service waits for nothing there: a POST records the
 * event, answers, and leaves the reset to the service's worker, and GET shows how it went. */
#include "crux_web_adapter.h"

#include <crux/reboot.h>
#include <crux/web.h>

#include <stdio.h>
#include <string.h>

#define BODY_MAX 256u

static crux_web_result_t respond_not_ready(crux_web_request_t *request)
{
    return crux_web_respond_error(request, 503, "not_ready");
}

/* What the service's refusals mean on the wire, for both routes. */
static crux_web_result_t respond_failure(crux_web_request_t *request, crux_reboot_result_t result)
{
    switch (result) {
    case CRUX_REBOOT_ERR_SESSION_INVALID:
        return crux_web_respond_error(request, 401, "unauthorized");
    case CRUX_REBOOT_ERR_DENIED:
        return crux_web_respond_error(request, 403, "permission_denied");
    case CRUX_REBOOT_ERR_BUSY:
        return crux_web_respond_error(request, 503, "busy");
    case CRUX_REBOOT_OK:
    case CRUX_REBOOT_ERR_INVALID_ARGUMENT:
    case CRUX_REBOOT_ERR_NOT_READY:
    case CRUX_REBOOT_ERR_REFUSED:
    case CRUX_REBOOT_ERR_NO_POLICY:
    default:
        return respond_not_ready(request);
    }
}

/* A bounded writer, which the reason's escapes cannot overflow without notice. */
typedef struct {
    char data[BODY_MAX];
    size_t used;
    bool overflow;
} writer_t;

static void put(writer_t *writer, const char *text, size_t length)
{
    if (writer->overflow || length > sizeof(writer->data) - writer->used) {
        writer->overflow = true;
        return;
    }
    memcpy(writer->data + writer->used, text, length);
    writer->used += length;
}

static void put_text(writer_t *writer, const char *text)
{
    put(writer, text, strlen(text));
}

/* The reason as a JSON string. The service has held it to well-formed UTF-8 without a control
 * character, so the quote and the backslash are the only bytes that need an escape. */
static void put_reason(writer_t *writer, const char *reason)
{
    put_text(writer, "\"");
    for (const char *cursor = reason; *cursor != '\0'; ++cursor) {
        if (*cursor == '"' || *cursor == '\\') {
            put(writer, "\\", 1u);
        }
        put(writer, cursor, 1u);
    }
    put_text(writer, "\"");
}

static crux_web_result_t respond_writer(crux_web_request_t *request, int status,
                                        const writer_t *writer)
{
    if (writer->overflow ||
        crux_web_respond(request, status, NULL, writer->data, writer->used) != CRUX_WEB_OK) {
        return respond_not_ready(request);
    }
    return CRUX_WEB_OK;
}

/* GET. */

static crux_web_result_t on_reboot_get(crux_web_request_t *request, crux_access_session_t session,
                                       void *context)
{
    crux_reboot_state_t state;
    writer_t writer = {{0}, 0u, false};
    crux_reboot_result_t result;

    (void)context;
    result = crux_reboot_get(session, &state);
    if (result != CRUX_REBOOT_OK) {
        return respond_failure(request, result);
    }
    put_text(&writer, "{\"state\":");
    switch (state.state) {
    case CRUX_REBOOT_STATE_PENDING:
        put_text(&writer, "\"pending\"");
        break;
    case CRUX_REBOOT_STATE_REFUSED:
        put_text(&writer, "\"refused\",\"reason\":");
        put_reason(&writer, state.reason);
        break;
    case CRUX_REBOOT_STATE_FAILED:
        put_text(&writer, "\"failed\"");
        break;
    case CRUX_REBOOT_STATE_NONE:
    default:
        put_text(&writer, "\"none\"");
        break;
    }
    put_text(&writer, "}");
    return respond_writer(request, 200, &writer);
}

/* POST. The route takes no body, so the core answers 413 to any, before any session lookup. The
 * connection closes after every answer, as after every update of the other management routes. */
static crux_web_result_t on_reboot_post(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    char reason[CRUX_REBOOT_REASON_SIZE];
    writer_t writer = {{0}, 0u, false};
    crux_reboot_result_t result;

    (void)context;
    (void)crux_web_close_after_response(request);
    result = crux_reboot_request(session, reason);
    switch (result) {
    case CRUX_REBOOT_OK:
        return crux_web_respond(request, 202, NULL, "{\"state\":\"pending\"}",
                                sizeof("{\"state\":\"pending\"}") - 1u);
    case CRUX_REBOOT_ERR_REFUSED:
        put_text(&writer, "{\"code\":\"reboot_refused\",\"reason\":");
        put_reason(&writer, reason);
        put_text(&writer, "}");
        return respond_writer(request, 423, &writer);
    case CRUX_REBOOT_ERR_INVALID_ARGUMENT:
    case CRUX_REBOOT_ERR_NOT_READY:
    case CRUX_REBOOT_ERR_SESSION_INVALID:
    case CRUX_REBOOT_ERR_DENIED:
    case CRUX_REBOOT_ERR_BUSY:
    case CRUX_REBOOT_ERR_NO_POLICY:
    default:
        return respond_failure(request, result);
    }
}

/* The generator reads each access class as a literal, which is the value of
 * CRUX_REBOOT_PERMISSION, and the suites show it. */
CRUX_WEB_ROUTE(mgmt_reboot_get, "/api/v1/system/reboot", CRUX_WEB_GET, "system.reboot",
               on_reboot_get, NULL);
CRUX_WEB_ROUTE_BODY(mgmt_reboot_post, "/api/v1/system/reboot", CRUX_WEB_POST, "system.reboot",
                    on_reboot_post, NULL, 0u);
