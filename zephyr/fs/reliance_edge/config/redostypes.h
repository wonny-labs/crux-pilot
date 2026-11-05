/* SPDX-License-Identifier: Apache-2.0 */
/* Types named by the OS service interface. */
#ifndef CRUX_RELIANCE_EDGE_REDOSTYPES_H
#define CRUX_RELIANCE_EDGE_REDOSTYPES_H

#include <stdint.h>

/* Timestamps are off, and the services return 0. */
typedef uint64_t REDTIMESTAMP;

/* The block device is named at mount time, not configured through this context. */
typedef void *REDBDEVCTX;

#endif /* CRUX_RELIANCE_EDGE_REDOSTYPES_H */
