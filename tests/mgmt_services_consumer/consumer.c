/* What a channel adapter or a project writes, from public headers alone. It is compiled and not
 * run: no provider exists for a product image yet. */
#include <crux/config_service.h>
#include <crux/management_status.h>

int consumer_list_config(crux_access_session_t session);
int consumer_set_and_save(crux_access_session_t session);
int consumer_status(crux_access_session_t session, char *out, size_t capacity);
int consumer_status_stream(crux_access_session_t session);
int consumer_project(const char *key);

int consumer_list_config(crux_access_session_t session)
{
    size_t cursor = 0;
    crux_config_service_item_t item;
    crux_config_service_result_t result;
    int visible = 0;
    while ((result = crux_config_service_next(session, &cursor, &item)) == CRUX_CONFIG_SERVICE_OK) {
        char text[CRUX_CONFIG_SERVICE_TEXT_SIZE];
        size_t length = 0;
        crux_config_type_t type = CRUX_CONFIG_BOOL;
        if (item.readable && crux_config_service_read(session, item.name, text, sizeof(text),
                                                      &length, &type) == CRUX_CONFIG_SERVICE_OK) {
            ++visible;
        }
    }
    return result == CRUX_CONFIG_SERVICE_END ? visible : -1;
}

int consumer_set_and_save(crux_access_session_t session)
{
    crux_config_service_item_t applied;
    if (crux_config_service_update(session, "host_name", "crux", 4, &applied) !=
        CRUX_CONFIG_SERVICE_OK) {
        return 1;
    }
    /* The adapter says what the application declared: accepted is not applied. */
    if (applied.activation == CRUX_CONFIG_ACTIVATION_RESTART) {
        return 2;
    }
    crux_config_service_save_report_t report;
    return crux_config_service_save(session, &report) == CRUX_CONFIG_SERVICE_OK ? 0 : 3;
}

int consumer_status(crux_access_session_t session, char *out, size_t capacity)
{
    size_t length = 0;
    return crux_mgmt_status_report_json(session, out, capacity, &length) == CRUX_OK ? (int)length
                                                                                    : -1;
}

int consumer_status_stream(crux_access_session_t session)
{
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry;
    int items = 0;
    while (crux_mgmt_status_next(session, &cursor, &entry) == CRUX_OK) {
        char json[CRUX_MGMT_STATUS_JSON_ENTRY_SIZE];
        size_t length = 0;
        if (crux_mgmt_status_entry_json(&entry, json, sizeof(json), &length) == CRUX_OK &&
            crux_mgmt_status_result_name(entry.status) != NULL) {
            ++items;
        }
    }
    return items;
}

int consumer_project(const char *key)
{
    char text[CRUX_CONFIG_STRING_MAX + 1];
    crux_config_value_t value;
    if (crux_config_service_try_read(key, &value, text, sizeof(text)) != CRUX_CONFIG_SERVICE_OK) {
        return 1;
    }
    if (crux_config_service_try_update(key, &value) != CRUX_CONFIG_SERVICE_OK) {
        return 2;
    }
    crux_config_service_save_report_t report;
    return crux_config_service_save_local(&report) == CRUX_CONFIG_SERVICE_OK ? 0 : 3;
}
