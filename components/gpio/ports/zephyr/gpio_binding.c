#include "gpio_binding.h"

#include "gpio_port.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include <stddef.h>

#if !DT_HAS_ALIAS(crux_gpio0)
#error "Crux GPIO requires a crux-gpio0 Devicetree alias"
#endif

#define GPIO_NODE DT_ALIAS(crux_gpio0)
#if !DT_NODE_HAS_STATUS(GPIO_NODE, okay)
#error "Crux GPIO selected resource must be enabled"
#endif
#if !DT_NODE_HAS_PROP(GPIO_NODE, gpios)
#error "Crux GPIO selected resource must provide a gpios property"
#endif

static const struct gpio_dt_spec line0 = GPIO_DT_SPEC_GET(GPIO_NODE, gpios);

BUILD_ASSERT(DT_NODE_HAS_STATUS(DT_GPIO_CTLR_BY_IDX(GPIO_NODE, gpios, 0), okay),
             "Crux GPIO controller must be enabled");

const struct gpio_dt_spec *crux_gpio_zephyr_spec(crux_gpio_line_t line)
{
    return line == CRUX_GPIO_LINE_0 ? &line0 : NULL;
}
