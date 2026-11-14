/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The product's own behaviour: plain C, no Zephyr, no thread and no lock.
 *
 * Only one thread, the owner in the shell, calls these functions. See the shared-state pattern
 * in docs/choosing-an-api.md of Crux.
 */
#ifndef PRODUCT_DOMAIN_H
#define PRODUCT_DOMAIN_H

#include <stdbool.h>
#include <stdint.h>

struct product_domain {
    int32_t limit_millikelvin;
    int32_t filtered_millikelvin;
    bool primed;
    bool alarm;
};

/** Start with an alarm limit in millikelvin. */
void product_domain_init(struct product_domain *domain, int32_t limit_millikelvin);

/** Change the alarm limit. The next step applies it. */
void product_domain_set_limit(struct product_domain *domain, int32_t limit_millikelvin);

/** Read the port once, filter the sample and update the alarm. Return 0 or the port's error. */
int product_domain_step(struct product_domain *domain);

/** The filtered temperature in millikelvin. */
int32_t product_domain_filtered(const struct product_domain *domain);

/** True while the filtered temperature is above the limit. */
bool product_domain_alarm(const struct product_domain *domain);

#endif
