#ifndef CRUX_DELAY_TEST_KERNEL_H
#define CRUX_DELAY_TEST_KERNEL_H
#include <stdint.h>
uint32_t delay_test_clock_hz(void);
#define sys_clock_hw_cycles_per_sec() delay_test_clock_hz()
void k_busy_wait(uint32_t usec);
#endif
