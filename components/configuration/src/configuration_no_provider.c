/* The port a composition selects while no Configuration provider exists for it. A load or save then
 * returns NOT_SUPPORTED, which the mechanism passes on, and each parameter keeps its default. */
#include "configuration_port.h"

crux_config_result_t crux_config_record_read(const char *key, uint8_t *buffer, size_t capacity,
                                             crux_config_record_info_t *record)
{
    (void)key;
    (void)buffer;
    (void)capacity;
    (void)record;
    return CRUX_CONFIG_ERR_NOT_SUPPORTED;
}

crux_config_result_t crux_config_record_write(const char *key, const uint8_t *buffer, size_t length)
{
    (void)key;
    (void)buffer;
    (void)length;
    return CRUX_CONFIG_ERR_NOT_SUPPORTED;
}
