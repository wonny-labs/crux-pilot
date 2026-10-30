/**
 * @file
 * @brief Synchronous access to statically composed logical GPIO lines.
 *
 * The initial provider profile exposes one line, CRUX_GPIO_LINE_0. Values are
 * logical active/inactive states; native polarity remains composition-owned.
 * Calls are synchronous, task-context only, and must be serialized by callers
 * for a line. No interrupt or callback interface is provided.
 */
#ifndef CRUX_GPIO_H
#define CRUX_GPIO_H

#include <crux/status.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t crux_gpio_line_t;

enum { CRUX_GPIO_LINE_0 = 0 };

typedef enum {
    CRUX_GPIO_INPUT = 0,
    CRUX_GPIO_OUTPUT = 1
} crux_gpio_direction_t;

/** Configure the statically selected line as input or output. */
crux_status_t crux_gpio_configure(crux_gpio_line_t line, crux_gpio_direction_t direction);

/**
 * Read the logical active state of a configured input. The output is unchanged
 * on error. Returns NOT_READY before configuration and NOT_SUPPORTED for output.
 */
crux_status_t crux_gpio_read(crux_gpio_line_t line, bool *active);

/** Set the logical active state of a configured output. */
crux_status_t crux_gpio_write(crux_gpio_line_t line, bool active);

#ifdef __cplusplus
}
#endif

#endif
