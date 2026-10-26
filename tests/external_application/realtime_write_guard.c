#include <stdlib.h>
#include <time.h>

/* Trusted test composition: any accidental system-clock write fails the test. */
int __wrap_clock_settime(clockid_t clock, const struct timespec *value)
{
    (void)clock;
    (void)value;
    abort();
}
