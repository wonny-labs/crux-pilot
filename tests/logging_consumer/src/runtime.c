#include "runtime.h"

#include <inttypes.h>
#include <stdio.h>

crux_status_t logging_runtime_record(uint32_t sequence, int code)
{
    char text[64];
    int count = snprintf(text, sizeof(text), "sequence=%" PRIu32 " code=%d", sequence, code);
    if (count < 0)
        return CRUX_ERR_IO;
    if ((size_t)count >= sizeof(text))
        return CRUX_ERR_OUT_OF_RANGE;
    return crux_log_write(code == 0 ? CRUX_LOG_INFO : CRUX_LOG_ERROR, "runtime.c", text);
}
