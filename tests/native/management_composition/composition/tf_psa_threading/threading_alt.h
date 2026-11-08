/* The lock types of TF-PSA-Crypto's core, on Zephyr's kernel objects. */
#ifndef CRUX_TF_PSA_THREADING_ALT_H
#define CRUX_TF_PSA_THREADING_ALT_H

#include <zephyr/kernel.h>

typedef struct k_mutex mbedtls_platform_mutex_t;
typedef struct k_condvar mbedtls_platform_condition_variable_t;

#endif
