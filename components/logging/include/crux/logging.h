#ifndef CRUX_LOGGING_H
#define CRUX_LOGGING_H
#include <crux/status.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    CRUX_LOG_OFF = 0,
    CRUX_LOG_ERROR = 1,
    CRUX_LOG_WARN = 2,
    CRUX_LOG_INFO = 3,
    CRUX_LOG_DEBUG = 4
} crux_log_level_t;
enum { CRUX_LOG_TAG_MAX = 24, CRUX_LOG_TEXT_MAX = 256 };
/* Task context after startup. Default INFO; OFF suppresses all records.
 * Concurrent submissions sample this process/boot-local threshold once.
 * Already submitted native records are unaffected. No native backend switching. */
crux_status_t crux_log_set_level(crux_log_level_t maximum_level);
/* Completed text, never a format string. Nonnull NUL-terminated tag (1..24
 * ASCII letters/digits/_.-) and text (0..256 bytes, no CR/LF), excluding NUL.
 * Borrow readable, unchanged strings until return; no caller storage retained.
 * OK means valid filtered call or native attempt, NOT queue/output/delivery
 * acknowledgement. Zephyr may drop-new after OK and reports aggregate native
 * loss. Host synchronous output may block; IO may follow a partial record.
 * No whole-call latency, reliable delivery, public drain or panic/ISR support.
 * See the Logging contract for qualified composition and native limitations. */
crux_status_t crux_log_write(crux_log_level_t level, const char *tag, const char *text);
#ifdef __cplusplus
}
#endif
#endif
