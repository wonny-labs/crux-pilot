#ifndef M3_TEST_FAULT_GPIO_H
#define M3_TEST_FAULT_GPIO_H

#include <stdbool.h>
void m3_test_gpio_value(int raw);
void m3_test_gpio_fail_read(bool fail);

#endif
