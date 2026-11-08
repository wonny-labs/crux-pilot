/* The JSON forms of web/api/openapi.yaml. Separate from the service, so that the rules of each stay
 * readable. tests/mgmt_status/check_openapi_status.py holds the output to the merged schema. */
#include <crux/configuration.h>
#include <crux/management_status.h>

#include <string.h>

/* Longer than any built-in name, and short enough for CRUX_MGMT_STATUS_JSON_ENTRY_SIZE. */
#define JSON_NAME_MAX 63u

typedef struct {
    char *out;
    size_t capacity;
    size_t length;
    bool overflow;
} writer_t;

static void put_bytes(writer_t *writer, const char *bytes, size_t count)
{
    /* One byte always stays for the terminator. */
    if (writer->overflow || writer->capacity == 0 ||
        count > writer->capacity - 1u - writer->length) {
        writer->overflow = true;
        return;
    }
    memcpy(writer->out + writer->length, bytes, count);
    writer->length += count;
}

static void put(writer_t *writer, const char *text)
{
    put_bytes(writer, text, strlen(text));
}

/* The character rule keeps out every control, so only these two need an escape. */
static void put_escaped(writer_t *writer, const char *text)
{
    for (; *text != '\0'; ++text) {
        if (*text == '"' || *text == '\\') {
            put_bytes(writer, "\\", 1);
        }
        put_bytes(writer, text, 1);
    }
}

static void put_u64(writer_t *writer, uint64_t value)
{
    char digits[20];
    size_t position = sizeof(digits);
    do {
        digits[--position] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    put_bytes(writer, &digits[position], sizeof(digits) - position);
}

const char *crux_mgmt_status_result_name(crux_status_t status)
{
    switch (status) {
    case CRUX_OK:
        return "ok";
    case CRUX_ERR_INVALID_ARGUMENT:
        return "invalid_argument";
    case CRUX_ERR_OUT_OF_RANGE:
        return "out_of_range";
    case CRUX_ERR_NOT_READY:
        return "not_ready";
    case CRUX_ERR_NO_DATA:
        return "no_data";
    case CRUX_ERR_IO:
        return "io";
    case CRUX_ERR_BUSY:
        return "busy";
    case CRUX_ERR_TIMEOUT:
        return "timeout";
    case CRUX_ERR_NO_RESOURCES:
        return "no_resources";
    case CRUX_ERR_NOT_SUPPORTED:
        return "not_supported";
    case CRUX_ERR_INVALID_STATE:
        return "invalid_state";
    default:
        return NULL;
    }
}

/* Reads no further than the terminator that the limit allows. */
static bool name_valid(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (size_t i = 0; name[i] != '\0'; ++i) {
        char c = name[i];
        bool allowed =
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!allowed || i == JSON_NAME_MAX) {
            return false;
        }
    }
    return true;
}

static bool value_valid(const crux_mgmt_status_value_t *value)
{
    switch (value->type) {
    case CRUX_MGMT_STATUS_BOOL:
    case CRUX_MGMT_STATUS_U32:
    case CRUX_MGMT_STATUS_U64:
        return true;
    case CRUX_MGMT_STATUS_TEXT: {
        const char *end = memchr(value->data.text, '\0', CRUX_MGMT_STATUS_TEXT_SIZE);
        return end != NULL &&
               crux_config_text_valid(value->data.text, (size_t)(end - value->data.text));
    }
    default:
        return false;
    }
}

static void put_value(writer_t *writer, const crux_mgmt_status_value_t *value)
{
    switch (value->type) {
    case CRUX_MGMT_STATUS_BOOL:
        put(writer, "\"bool\",\"value\":");
        put(writer, value->data.boolean ? "true" : "false");
        break;
    case CRUX_MGMT_STATUS_U32:
        put(writer, "\"u32\",\"value\":");
        put_u64(writer, value->data.u32);
        break;
    case CRUX_MGMT_STATUS_U64:
        /* Decimal text, because a JSON number loses precision above 2^53 in a browser. */
        put(writer, "\"u64\",\"value\":\"");
        put_u64(writer, value->data.u64);
        put(writer, "\"");
        break;
    default:
        put(writer, "\"text\",\"value\":\"");
        put_escaped(writer, value->data.text);
        put(writer, "\"");
        break;
    }
}

crux_status_t crux_mgmt_status_entry_json(const crux_mgmt_status_entry_t *entry, char *out,
                                          size_t capacity, size_t *length)
{
    if (entry == NULL || out == NULL || length == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    const char *word = crux_mgmt_status_result_name(entry->status);
    if (!name_valid(entry->name) || word == NULL ||
        (entry->status == CRUX_OK && !value_valid(&entry->value))) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    writer_t writer = {out, capacity, 0, false};
    put(&writer, "{\"name\":\"");
    put(&writer, entry->name);
    put(&writer, "\",\"status\":\"");
    put(&writer, word);
    put(&writer, "\"");
    if (entry->status == CRUX_OK) {
        put(&writer, ",\"type\":");
        put_value(&writer, &entry->value);
    }
    put(&writer, "}");
    if (writer.overflow) {
        return CRUX_ERR_NO_RESOURCES;
    }
    out[writer.length] = '\0';
    *length = writer.length;
    return CRUX_OK;
}

crux_status_t crux_mgmt_status_report_json(crux_access_session_t session, char *out,
                                           size_t capacity, size_t *length)
{
    /* Checks the session on each call, and does nothing else with an empty list. */
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    crux_status_t result = crux_mgmt_status_next(session, &cursor, &entry);
    if (result != CRUX_OK && result != CRUX_ERR_NO_DATA) {
        return result;
    }
    if (out == NULL || length == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    writer_t writer = {out, capacity, 0, false};
    put(&writer, "{\"items\":[");
    bool first = true;
    while (result == CRUX_OK) {
        if (!first) {
            put(&writer, ",");
        }
        first = false;
        if (writer.overflow) {
            return CRUX_ERR_NO_RESOURCES;
        }
        size_t written = 0;
        crux_status_t formatted = crux_mgmt_status_entry_json(&entry, out + writer.length,
                                                              capacity - writer.length, &written);
        if (formatted != CRUX_OK) {
            return formatted;
        }
        writer.length += written;
        result = crux_mgmt_status_next(session, &cursor, &entry);
    }
    if (result != CRUX_ERR_NO_DATA) {
        return result;
    }
    put(&writer, "]}");
    if (writer.overflow) {
        return CRUX_ERR_NO_RESOURCES;
    }
    out[writer.length] = '\0';
    *length = writer.length;
    return CRUX_OK;
}
