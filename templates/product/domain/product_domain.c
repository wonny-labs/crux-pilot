/* SPDX-License-Identifier: Apache-2.0 */
#include "product_domain.h"

#include "product_port.h"

/* The filter moves a quarter of the way to each new sample. */
#define FILTER_DIVISOR 4

void product_domain_init(struct product_domain *domain, int32_t limit_millikelvin)
{
    domain->limit_millikelvin = limit_millikelvin;
    domain->filtered_millikelvin = 0;
    domain->primed = false;
    domain->alarm = false;
}

void product_domain_set_limit(struct product_domain *domain, int32_t limit_millikelvin)
{
    domain->limit_millikelvin = limit_millikelvin;
}

int product_domain_step(struct product_domain *domain)
{
    int32_t sample;
    const int status = product_port_read_temperature(&sample);

    if (status != 0) {
        return status;
    }
    if (domain->primed) {
        domain->filtered_millikelvin += (sample - domain->filtered_millikelvin) / FILTER_DIVISOR;
    } else {
        domain->filtered_millikelvin = sample;
        domain->primed = true;
    }
    domain->alarm = domain->filtered_millikelvin > domain->limit_millikelvin;
    return 0;
}

int32_t product_domain_filtered(const struct product_domain *domain)
{
    return domain->filtered_millikelvin;
}

bool product_domain_alarm(const struct product_domain *domain)
{
    return domain->alarm;
}
