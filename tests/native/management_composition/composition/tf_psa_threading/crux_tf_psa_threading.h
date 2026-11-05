#ifndef CRUX_TF_PSA_THREADING_H
#define CRUX_TF_PSA_THREADING_H

#include <stdint.h>

/* How many times the core has taken a lock since boot, which shows that the locks are in use. */
uint32_t crux_tf_psa_threading_lock_count(void);

#endif
