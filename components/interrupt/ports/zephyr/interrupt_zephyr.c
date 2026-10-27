#include "interrupt.h"
#include <zephyr/kernel.h>
bool crux_is_in_isr(void)
{
    return k_is_in_isr();
}
