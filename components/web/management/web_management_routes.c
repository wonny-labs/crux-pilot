/* The status and configuration routes of the management API specification. A composition names this
 * source to crux_web_routes(), and components/web/reviewed-routes.txt lists the session-only routes
 * for review.
 *
 * Every route is session-only except the save, whose access class is config.save. The status and
 * configuration services decide what a session may see and do, so these handlers translate their
 * results and add no rule of their own, except in the one place noted in on_config_put(). A refusal
 * never carries a path, a parameter's value or native error text.
 *
 * The handlers run on the server thread, and the services poll their locks rather than wait on
 * them, so a held lock answers 503 `busy`. The save is the one slow operation, and it runs on the
 * worker of web_management_save.c. A validator of a parameter that the table exposes runs here, on
 * the server thread, so it must be quick, which the service already requires of every validator. */
#include "web_management_internal.h"

#include <stdio.h>
#include <string.h>

#define PERMISSION_SAVE CRUX_CONFIG_SERVICE_PERMISSION_SAVE
#define REQUEST_BODY_MAX CRUX_WEB_BODY_BUFFER

/* The body of one response, and the bodies of one update. They are used on the server thread only,
 * and an update clears what it held, since a value may be a secret. */
static char response_body[CRUX_WEB_MANAGEMENT_BODY_MAX];
static uint8_t request_body[REQUEST_BODY_MAX];
static char value_text[CRUX_CONFIG_SERVICE_TEXT_SIZE];

static void wipe(void *buffer, size_t length)
{
    volatile uint8_t *bytes = buffer;

    for (size_t index = 0; index < length; ++index) {
        bytes[index] = 0u;
    }
}

static void clear_update(void)
{
    wipe(request_body, sizeof(request_body));
    wipe(value_text, sizeof(value_text));
}

static crux_web_result_t respond_not_ready(crux_web_request_t *request)
{
    return crux_web_respond_error(request, 503, "not_ready");
}

/* A finished document, or 503 when it did not fit the buffer. */
static crux_web_result_t respond_document(crux_web_request_t *request, int status,
                                          const web_mgmt_writer_t *writer)
{
    if (writer->overflow ||
        crux_web_respond(request, status, NULL, writer->data, writer->used) != CRUX_WEB_OK) {
        return respond_not_ready(request);
    }
    return CRUX_WEB_OK;
}

static crux_web_result_t respond_field_error(crux_web_request_t *request, const char *code)
{
    char body[96];
    int length = snprintf(
        body, sizeof(body),
        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"value\",\"code\":\"%s\"}]}", code);

    return crux_web_respond(request, 400, NULL, body, length > 0 ? (size_t)length : 0u);
}

/* What the service's refusals mean on the wire, for every route of the configuration. */
static crux_web_result_t respond_config_failure(crux_web_request_t *request,
                                                crux_config_service_result_t result)
{
    switch (result) {
    case CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID:
        return crux_web_respond_error(request, 401, "unauthorized");
    case CRUX_CONFIG_SERVICE_ERR_DENIED:
    case CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY:
        return crux_web_respond_error(request, 403, "permission_denied");
    case CRUX_CONFIG_SERVICE_ERR_NOT_FOUND:
        return crux_web_respond_error(request, 404, "not_found");
    case CRUX_CONFIG_SERVICE_ERR_BUSY:
        return crux_web_respond_error(request, 503, "busy");
    case CRUX_CONFIG_SERVICE_OK:
    case CRUX_CONFIG_SERVICE_END:
    case CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT:
    case CRUX_CONFIG_SERVICE_ERR_NOT_READY:
    case CRUX_CONFIG_SERVICE_ERR_REJECTED:
    case CRUX_CONFIG_SERVICE_ERR_STORE:
    case CRUX_CONFIG_SERVICE_ERR_TOO_SMALL:
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

/* Status. */

static crux_web_result_t on_status(crux_web_request_t *request, crux_access_session_t session,
                                   void *context)
{
    size_t length = 0u;

    (void)context;
    switch (crux_mgmt_status_report_json(session, response_body, sizeof(response_body), &length)) {
    case CRUX_OK:
        if (crux_web_respond(request, 200, NULL, response_body, length) == CRUX_WEB_OK) {
            return CRUX_WEB_OK;
        }
        return respond_not_ready(request);
    case CRUX_ERR_INVALID_STATE:
        return crux_web_respond_error(request, 401, "unauthorized");
    default:
        /* The service is not ready, or this composition's items do not fit one document. */
        return respond_not_ready(request);
    }
}

/* Configuration. */

static const char *type_name(crux_config_type_t type)
{
    switch (type) {
    case CRUX_CONFIG_BOOL:
        return "bool";
    case CRUX_CONFIG_U32:
        return "u32";
    case CRUX_CONFIG_STRING:
        return "string";
    default:
        return NULL;
    }
}

static const char *activation_name(crux_config_activation_t activation)
{
    switch (activation) {
    case CRUX_CONFIG_ACTIVATION_APPLICATION:
        return "application";
    case CRUX_CONFIG_ACTIVATION_RESTART:
        return "restart";
    case CRUX_CONFIG_ACTIVATION_SAFE_STATE:
        return "safe_state";
    default:
        return NULL;
    }
}

static void write_flag(web_mgmt_writer_t *writer, const char *name, bool flag)
{
    web_mgmt_write(writer, ",\"");
    web_mgmt_write(writer, name);
    web_mgmt_write(writer, flag ? "\":true" : "\":false");
}

/* False for a type or activation this API has no word for, which no table can hold. */
static bool write_item(web_mgmt_writer_t *writer, const crux_config_service_item_t *item)
{
    const char *type = type_name(item->type);
    const char *activation = activation_name(item->activation);

    if (type == NULL || activation == NULL || item->name == NULL) {
        return false;
    }
    web_mgmt_write(writer, "{\"name\":");
    web_mgmt_write_string(writer, item->name, strlen(item->name));
    web_mgmt_write(writer, ",\"type\":\"");
    web_mgmt_write(writer, type);
    web_mgmt_write(writer, "\",\"activation\":\"");
    web_mgmt_write(writer, activation);
    web_mgmt_write(writer, "\"");
    write_flag(writer, "sensitive", item->sensitive);
    write_flag(writer, "readable", item->readable);
    write_flag(writer, "writable", item->writable);
    write_flag(writer, "unsaved", item->unsaved);
    web_mgmt_write(writer, "}");
    return true;
}

/* The save that the list reports, for a session that holds config.save, once one was requested. The
 * check is the quiet one, so a poll leaves no event and does not refresh the idle timer. */
static void write_save(web_mgmt_writer_t *writer, crux_access_session_t session)
{
    bool permitted = false;
    web_save_view_t view;

    if (crux_access_permitted(session, PERMISSION_SAVE, &permitted) != CRUX_ACCESS_OK ||
        !permitted) {
        return;
    }
    view = web_mgmt_save_view();
    switch (view.state) {
    case WEB_SAVE_NONE:
        return;
    case WEB_SAVE_QUEUED:
    case WEB_SAVE_RUNNING:
        web_mgmt_write(writer, ",\"save\":{\"state\":\"pending\"}");
        return;
    case WEB_SAVE_SAVED:
        web_mgmt_write(writer, ",\"save\":{\"state\":\"saved\"}");
        return;
    case WEB_SAVE_FAILED:
    default:
        web_mgmt_write(writer, ",\"save\":{\"state\":\"failed\",\"code\":\"");
        web_mgmt_write(writer, view.code != NULL ? view.code : "not_ready");
        web_mgmt_write(writer, "\"}");
        return;
    }
}

static crux_web_result_t on_config_list(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    web_mgmt_writer_t writer;
    size_t cursor = 0u;

    (void)context;
    web_mgmt_writer_init(&writer, response_body, sizeof(response_body));
    web_mgmt_write(&writer, "{\"items\":[");
    /* The table holds at most CRUX_CONFIG_SERVICE_MAX entries, so the loop is bounded. */
    for (size_t count = 0; count <= CRUX_CONFIG_SERVICE_MAX; ++count) {
        crux_config_service_item_t item;
        crux_config_service_result_t result = crux_config_service_next(session, &cursor, &item);

        if (result == CRUX_CONFIG_SERVICE_END) {
            web_mgmt_write(&writer, "]");
            write_save(&writer, session);
            web_mgmt_write(&writer, "}");
            return respond_document(request, 200, &writer);
        }
        if (result != CRUX_CONFIG_SERVICE_OK) {
            return respond_config_failure(request, result);
        }
        if (count != 0u) {
            web_mgmt_write(&writer, ",");
        }
        if (!write_item(&writer, &item)) {
            return respond_not_ready(request);
        }
    }
    return respond_not_ready(request);
}

/* The parameter's name from the path. False when it cannot be a key: nothing is that long. */
static bool path_name(crux_web_request_t *request, char name[WEB_MGMT_NAME_SIZE])
{
    return crux_web_path_param(request, 0u, name, WEB_MGMT_NAME_SIZE) == CRUX_WEB_OK;
}

static crux_web_result_t on_config_get(crux_web_request_t *request, crux_access_session_t session,
                                       void *context)
{
    char name[WEB_MGMT_NAME_SIZE];
    char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
    web_mgmt_writer_t writer;
    crux_config_service_result_t result;
    crux_config_type_t type = CRUX_CONFIG_BOOL;
    size_t length = 0u;

    (void)context;
    if (!path_name(request, name)) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    result = crux_config_service_read(session, name, text, sizeof(text), &length, &type);
    if (result != CRUX_CONFIG_SERVICE_OK) {
        return respond_config_failure(request, result);
    }
    web_mgmt_writer_init(&writer, response_body, sizeof(response_body));
    web_mgmt_write(&writer, "{\"name\":");
    web_mgmt_write_string(&writer, name, strlen(name));
    web_mgmt_write(&writer, ",\"type\":\"");
    web_mgmt_write(&writer, type_name(type) != NULL ? type_name(type) : "string");
    web_mgmt_write(&writer, "\",\"value\":");
    if (type == CRUX_CONFIG_STRING) {
        web_mgmt_write_string(&writer, text, length);
    } else {
        /* A bool's text is true or false and a u32's is decimal, which are the JSON forms. */
        web_mgmt_write(&writer, text);
    }
    web_mgmt_write(&writer, "}");
    return respond_document(request, 200, &writer);
}

/* The parameter as this session may know it, which tells the type that the body must have. The
 * service lists only what the session can read or write, so END means that it cannot see this one.
 */
static crux_config_service_result_t find_item(crux_access_session_t session, const char *name,
                                              crux_config_service_item_t *found)
{
    size_t cursor = 0u;

    for (size_t count = 0; count <= CRUX_CONFIG_SERVICE_MAX; ++count) {
        crux_config_service_result_t result = crux_config_service_next(session, &cursor, found);

        if (result != CRUX_CONFIG_SERVICE_OK) {
            return result;
        }
        if (strcmp(found->name, name) == 0) {
            return CRUX_CONFIG_SERVICE_OK;
        }
    }
    return CRUX_CONFIG_SERVICE_END;
}

/* Reads the whole body into request_body, and the member into value. */
static bool read_value(crux_web_request_t *request, web_value_t *value)
{
    size_t total = 0u;
    size_t transferred = 0u;

    value->text = value_text;
    value->size = sizeof(value_text);
    if (!json_content(crux_web_content_type(request))) {
        return false;
    }
    do {
        if (crux_web_read_body(request, request_body + total, sizeof(request_body) - total,
                               &transferred) != CRUX_WEB_OK) {
            return false;
        }
        total += transferred;
    } while (transferred != 0u && total < sizeof(request_body));
    return web_mgmt_parse_update(request_body, total, value);
}

static bool kind_matches(web_value_kind_t kind, crux_config_type_t type)
{
    return (kind == WEB_VALUE_BOOL && type == CRUX_CONFIG_BOOL) ||
           (kind == WEB_VALUE_NUMBER && type == CRUX_CONFIG_U32) ||
           (kind == WEB_VALUE_STRING && type == CRUX_CONFIG_STRING);
}

/* The service's own text form of the value, which is what its update parses. */
static size_t update_text(const web_value_t *value, char *text, size_t size)
{
    switch (value->kind) {
    case WEB_VALUE_BOOL:
        return (size_t)snprintf(text, size, "%s", value->boolean ? "true" : "false");
    case WEB_VALUE_NUMBER:
        return (size_t)snprintf(text, size, "%lu", (unsigned long)value->number);
    case WEB_VALUE_STRING:
    default:
        /* A string that did not fit has its first bytes here. Only a refusal or a parameter that
         * the session cannot update reaches the service with it, and neither reads the text. */
        return value->length < size ? value->length : size - 1u;
    }
}

static crux_web_result_t respond_updated(crux_web_request_t *request,
                                         const crux_config_service_item_t *applied)
{
    web_mgmt_writer_t writer;

    web_mgmt_writer_init(&writer, response_body, sizeof(response_body));
    if (!write_item(&writer, applied)) {
        return respond_not_ready(request);
    }
    return respond_document(request, 200, &writer);
}

/* Calls the service and translates its answer. The text is the service's form of the value. */
static crux_web_result_t update_and_respond(crux_web_request_t *request,
                                            crux_access_session_t session, const char *name,
                                            const char *text, size_t length)
{
    crux_config_service_item_t applied;
    crux_config_service_result_t result =
        crux_config_service_update(session, name, text, length, &applied);

    switch (result) {
    case CRUX_CONFIG_SERVICE_OK:
        return respond_updated(request, &applied);
    case CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT:
        return respond_field_error(request, "wrong_type");
    case CRUX_CONFIG_SERVICE_ERR_REJECTED:
        return respond_field_error(request, "rule_not_met");
    case CRUX_CONFIG_SERVICE_END:
    case CRUX_CONFIG_SERVICE_ERR_NOT_READY:
    case CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID:
    case CRUX_CONFIG_SERVICE_ERR_DENIED:
    case CRUX_CONFIG_SERVICE_ERR_NOT_FOUND:
    case CRUX_CONFIG_SERVICE_ERR_WRITE_ONLY:
    case CRUX_CONFIG_SERVICE_ERR_BUSY:
    case CRUX_CONFIG_SERVICE_ERR_STORE:
    case CRUX_CONFIG_SERVICE_ERR_TOO_SMALL:
    default:
        return respond_config_failure(request, result);
    }
}

/* Any parameter may be sensitive, and a refusal must not show which, so every response to an update
 * closes the connection: the server then clears its copy of the request, as it does after a login.
 * The order is the body, then what the session may see, then what it may write, then the type, so a
 * body never decides whether a parameter exists. The type check is the one rule here that the
 * service does not hold: it parses text, and so it would take a string "5" for a u32. */
static crux_web_result_t on_config_put(crux_web_request_t *request, crux_access_session_t session,
                                       void *context)
{
    char name[WEB_MGMT_NAME_SIZE];
    char scalar[12];
    web_value_t value;
    crux_config_service_item_t item;
    crux_config_service_result_t found;
    crux_web_result_t answer;

    (void)context;
    memset(&value, 0, sizeof(value));
    (void)crux_web_close_after_response(request);
    if (!read_value(request, &value)) {
        clear_update();
        return crux_web_respond_error(request, 400, "bad_request");
    }
    if (!path_name(request, name)) {
        clear_update();
        return crux_web_respond_error(request, 404, "not_found");
    }
    found = find_item(session, name, &item);
    if (found == CRUX_CONFIG_SERVICE_END) {
        /* Not visible. The service answers NOT_FOUND, and records the denial. */
        answer = update_and_respond(request, session, name, value_text,
                                    update_text(&value, value_text, sizeof(value_text)));
    } else if (found != CRUX_CONFIG_SERVICE_OK) {
        answer = respond_config_failure(request, found);
    } else if (!item.writable) {
        /* Visible and not writable: the service answers DENIED, whatever the body holds. */
        answer = update_and_respond(request, session, name, "", 0u);
    } else if (!kind_matches(value.kind, item.type)) {
        answer = respond_field_error(request, "wrong_type");
    } else if (value.kind == WEB_VALUE_NUMBER && !value.number_ok) {
        answer = respond_field_error(request, "out_of_range");
    } else if (value.kind == WEB_VALUE_STRING && value.length >= value.size) {
        answer = respond_field_error(request, "rule_not_met");
    } else if (value.kind == WEB_VALUE_STRING) {
        answer = update_and_respond(request, session, name, value_text, value.length);
    } else {
        size_t length = update_text(&value, scalar, sizeof(scalar));

        answer = update_and_respond(request, session, name, scalar, length);
    }
    clear_update();
    return answer;
}

/* The access class is config.save, which the core has enforced, and the service checks it again on
 * the worker. The body limit is zero, so a request with a body never reaches here. */
static crux_web_result_t on_config_save(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    (void)context;
    switch (web_mgmt_save_queue(session)) {
    case WEB_SAVE_QUEUE_OK:
        return crux_web_respond(request, 202, NULL, "{\"state\":\"pending\"}",
                                sizeof("{\"state\":\"pending\"}") - 1u);
    case WEB_SAVE_QUEUE_BUSY:
        return crux_web_respond_error(request, 503, "busy");
    case WEB_SAVE_QUEUE_NOT_READY:
    default:
        return respond_not_ready(request);
    }
}

CRUX_WEB_ROUTE(mgmt_status, "/api/v1/status", CRUX_WEB_GET, CRUX_WEB_SESSION, on_status, NULL);
CRUX_WEB_ROUTE(mgmt_config_list, "/api/v1/config", CRUX_WEB_GET, CRUX_WEB_SESSION, on_config_list,
               NULL);
CRUX_WEB_ROUTE(mgmt_config_get, "/api/v1/config/{name}", CRUX_WEB_GET, CRUX_WEB_SESSION,
               on_config_get, NULL);
CRUX_WEB_ROUTE_BODY(mgmt_config_put, "/api/v1/config/{name}", CRUX_WEB_PUT, CRUX_WEB_SESSION,
                    on_config_put, NULL, REQUEST_BODY_MAX);
/* The generator reads the access class as a literal, which is the value of
 * CRUX_CONFIG_SERVICE_PERMISSION_SAVE, and the suites show it. */
CRUX_WEB_ROUTE_BODY(mgmt_config_save, "/api/v1/config/save", CRUX_WEB_POST, "config.save",
                    on_config_save, NULL, 0u);

#ifdef CRUX_WEB_MANAGEMENT_TESTING
bool web_management_routes_hold_bytes(const void *bytes, size_t length);

static bool holds(const void *memory, size_t size, const void *bytes, size_t length)
{
    for (size_t offset = 0; offset + length <= size; ++offset) {
        if (memcmp((const uint8_t *)memory + offset, bytes, length) == 0) {
            return true;
        }
    }
    return false;
}

/* The handlers' own buffers, to show that an update leaves no value in them. */
bool web_management_routes_hold_bytes(const void *bytes, size_t length)
{
    return holds(request_body, sizeof(request_body), bytes, length) ||
           holds(value_text, sizeof(value_text), bytes, length) ||
           holds(response_body, sizeof(response_body), bytes, length);
}
#endif
