/**
 * @file
 * @brief Synchronous access to statically composed logical GPIO lines.
 *
 * The initial provider profile exposes one line, CRUX_GPIO_LINE_0. Values are
 * logical active/inactive states; native polarity remains composition-owned.
 * Calls are synchronous, task-context only, and must be serialized by callers
 * for a line. A provider that can detect interrupt context returns
 * CRUX_ERR_NOT_SUPPORTED from a call made there. When such a call also has an
 * invalid argument, which of the two statuses is returned is unspecified. The
 * Host provider has no interrupt context and does not check, and calls before
 * the kernel is running (pre-kernel or early startup) are not supported but
 * are not detected. A call may block as long as the native driver does; no
 * latency bound is guaranteed. No interrupt or callback interface is provided.
 */
#ifndef CRUX_GPIO_H
#define CRUX_GPIO_H

#include <crux/status.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Logical GPIO line identifier selected by static composition. */
typedef uint32_t crux_gpio_line_t;

/** The only line in the initial profile. */
enum { CRUX_GPIO_LINE_0 = 0 };

/**
 * @brief Configure the statically selected line as an input.
 *
 * @param line Logical line; only CRUX_GPIO_LINE_0 exists.
 * @retval CRUX_OK The line is configured. Repeating the call is allowed.
 * @retval CRUX_ERR_OUT_OF_RANGE @p line is not a supported line.
 * @retval CRUX_ERR_NOT_READY The native GPIO controller is not ready.
 * @retval CRUX_ERR_NOT_SUPPORTED Called from interrupt context, or the native
 *         driver reports the operation as unsupported.
 * @retval CRUX_ERR_IO The native driver reported any other failure.
 */
crux_status_t crux_gpio_configure_input(crux_gpio_line_t line);

/**
 * @brief Read the logical active state of a configured input.
 *
 * The output is unchanged on every error.
 *
 * @param line Logical line; only CRUX_GPIO_LINE_0 exists.
 * @param[out] active Receives true when the line is logically active.
 * @retval CRUX_OK @p active was written.
 * @retval CRUX_ERR_INVALID_ARGUMENT @p active is NULL.
 * @retval CRUX_ERR_OUT_OF_RANGE @p line is not a supported line.
 * @retval CRUX_ERR_NOT_READY The line was never successfully configured, or the native GPIO
 *         controller is not ready.
 * @retval CRUX_ERR_NOT_SUPPORTED Called from interrupt context, or the native
 *         driver reports the operation as unsupported.
 * @retval CRUX_ERR_IO The native driver reported any other failure.
 */
crux_status_t crux_gpio_read(crux_gpio_line_t line, bool *active);

#ifdef __cplusplus
}
#endif

#endif
