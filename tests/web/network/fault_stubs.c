/* The few functions of the fake Configuration store that the shared fixture calls, for the suites
 * that link the no-provider port instead of the store. With no provider there is no store, so they
 * do nothing. */
#include "net_test.h"

void config_fault_reset(void)
{
}

void config_fault_record(const char *key, const uint8_t *bytes, size_t length)
{
    (void)key;
    (void)bytes;
    (void)length;
}

void config_fault_write(config_fault_write_options_t options)
{
    (void)options;
}

void config_fault_write_hook(config_fault_hook_t hook, void *context)
{
    (void)hook;
    (void)context;
}
