#ifndef LOGGING_CONSUMER_RUNTIME_H
#define LOGGING_CONSUMER_RUNTIME_H

#include <crux/logging.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Each call formats one event in owned stack storage: code 0 is INFO; other
 * codes are ERROR. The caller owns the global filter policy. A returned OK
 * means handled/native attempt, never confirmed acceptance or delivery.
 * Failure is returned unchanged; these helpers do not retry. */
crux_status_t logging_runtime_record(uint32_t sequence, int code);
crux_status_t logging_cpp_record(uint32_t sequence, int code);

#ifdef __cplusplus
}
#endif
#endif
