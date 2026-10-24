#include "runtime.h"

#include <cinttypes>
#include <cstdio>

extern "C" crux_status_t logging_cpp_record(uint32_t sequence, int code) {
    char text[64];
    int count = std::snprintf(text, sizeof(text), "sequence=%" PRIu32 " code=%d", sequence, code);
    if (count < 0)
        return CRUX_ERR_IO;
    if (static_cast<std::size_t>(count) >= sizeof(text))
        return CRUX_ERR_OUT_OF_RANGE;
    return crux_log_write(code == 0 ? CRUX_LOG_INFO : CRUX_LOG_ERROR, "runtime.cpp", text);
}
