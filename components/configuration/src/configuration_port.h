#ifndef CRUX_CONFIGURATION_PORT_H
#define CRUX_CONFIGURATION_PORT_H

#include <crux/configuration.h>

#include <stddef.h>

/* Private, composition-selected port; no backend/plugin API is public.
 * Read OK reports trustworthy metadata plus actual bytes copied <= capacity.
 * DEFAULT_MISSING means explicit absence, never a short read or hidden failure.
 * Read/write errors: NOT_READY, IO, NO_SPACE, NOT_SUPPORTED. Other returns are IO.
 * No key/buffer retained. A write error may follow a change to stored bytes. */
typedef struct {
    size_t length;
    size_t transferred;
} crux_config_record_info_t;

crux_config_result_t crux_config_record_read(const char *key, uint8_t *buffer, size_t capacity,
                                             crux_config_record_info_t *record);
crux_config_result_t crux_config_record_write(const char *key, const uint8_t *buffer,
                                              size_t length);

#endif
