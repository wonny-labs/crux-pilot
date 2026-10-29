#include "logging_port.h"
#include <stdatomic.h>
#include <stddef.h>

static atomic_int threshold = CRUX_LOG_INFO;

static bool valid_level(crux_log_level_t level)
{
    return (unsigned)level <= (unsigned)CRUX_LOG_DEBUG;
}

static crux_status_t validate(const char *tag, const char *text)
{
    if (tag == NULL || text == NULL || tag[0] == '\0') {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    size_t n = 0;
    for (; n <= CRUX_LOG_TAG_MAX && tag[n] != '\0'; ++n) {
        unsigned char c = (unsigned char)tag[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.')) {
            return CRUX_ERR_INVALID_ARGUMENT;
        }
    }
    if (n > CRUX_LOG_TAG_MAX) {
        return CRUX_ERR_OUT_OF_RANGE;
    }

    for (n = 0; n <= CRUX_LOG_TEXT_MAX && text[n] != '\0'; ++n) {
        if (text[n] == '\r' || text[n] == '\n') {
            return CRUX_ERR_INVALID_ARGUMENT;
        }
    }
    return n > CRUX_LOG_TEXT_MAX ? CRUX_ERR_OUT_OF_RANGE : CRUX_OK;
}

crux_status_t crux_log_set_level(crux_log_level_t maximum_level)
{
    if (!valid_level(maximum_level)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!crux_log_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    atomic_store_explicit(&threshold, maximum_level, memory_order_relaxed);
    return CRUX_OK;
}

crux_status_t crux_log_write(crux_log_level_t level, const char *tag, const char *text)
{
    if (!valid_level(level) || level == CRUX_LOG_OFF) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    crux_status_t result = validate(tag, text);
    if (result != CRUX_OK) {
        return result;
    }
    if (!crux_log_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }

    int sampled = atomic_load_explicit(&threshold, memory_order_relaxed);
    if ((int)level > sampled) {
        return CRUX_OK;
    }
    return crux_log_impl_write(level, tag, text);
}
