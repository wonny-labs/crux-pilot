#ifndef CRUX_TIME_HOST_TEST_H
#define CRUX_TIME_HOST_TEST_H

#include <crux/status.h>
#include <crux/time.h>

#ifdef __cplusplus
extern "C" {
#endif

void crux_time_host_test_reset(void);
void crux_time_host_test_fail_next_init(void);
void crux_time_host_test_next_read_status(crux_status_t status);
void crux_time_host_test_fail_next_write(void);
crux_status_t crux_time_host_test_advance(int64_t seconds);
void crux_time_host_test_override_time(crux_time_instant_t value);
unsigned int crux_time_host_test_init_calls(void);
unsigned int crux_time_host_test_read_calls(void);
unsigned int crux_time_host_test_write_calls(void);

#ifdef __cplusplus
}
#endif

#endif
