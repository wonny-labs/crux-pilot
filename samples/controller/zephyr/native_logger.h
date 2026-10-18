#ifndef CRUX_SAMPLE_NATIVE_LOGGER_H
#define CRUX_SAMPLE_NATIVE_LOGGER_H

#include <stddef.h>

/* Sample-owned observations of the native, statically allocated logger. */
struct m3_logger_resources {
    unsigned int found;
    int status;
    size_t usable;
    size_t unused;
    int priority;
};

struct m3_logger_resources m3_logger_resources_get(void);

#endif
