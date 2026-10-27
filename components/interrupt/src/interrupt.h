#ifndef CRUX_INTERRUPT_PRIVATE_H
#define CRUX_INTERRUPT_PRIVATE_H
#include <stdbool.h>
/* Private context mechanism only; component eligibility remains local.
 * Linux returns false for ordinary threads, not a signal-safety guarantee. */
bool crux_is_in_isr(void);
#endif
