/** @file
 * @brief Bounded text logging with a process-local severity filter.
 *
 * Thread context after startup only; no ISR or panic-path support.
 */
#ifndef CRUX_LOGGING_H
#define CRUX_LOGGING_H

#include <crux/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CRUX_LOG_OFF is a filter setting, not a record severity. */
typedef enum {
    CRUX_LOG_OFF = 0,
    CRUX_LOG_ERROR = 1,
    CRUX_LOG_WARN = 2,
    CRUX_LOG_INFO = 3,
    CRUX_LOG_DEBUG = 4
} crux_log_level_t;

enum { CRUX_LOG_TAG_MAX = 24, CRUX_LOG_TEXT_MAX = 256 };

/** Set the maximum enabled log level (initially INFO); OFF disables logging.
 * @param maximum_level Filter threshold; already submitted records are unaffected.
 * @return CRUX_OK or a crux_status_t error.
 */
crux_status_t crux_log_set_level(crux_log_level_t maximum_level);

/** Submit a completed text record, not a printf-style format string.
 * @param level Severity subject to the current filter.
 * @param tag NUL-terminated ASCII [A-Za-z0-9_.-], 1..CRUX_LOG_TAG_MAX bytes.
 * @param text NUL-terminated text, at most CRUX_LOG_TEXT_MAX bytes, without CR/LF.
 * @return CRUX_OK means filtered or submitted, not delivered or persisted.
 * Strings are borrowed until return; backends may drop records. Host output may block.
 */
crux_status_t crux_log_write(crux_log_level_t level, const char *tag, const char *text);

#ifdef __cplusplus
}
#endif

#endif
