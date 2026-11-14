/* SPDX-License-Identifier: Apache-2.0 */
/* The boot check of the random source, for an image on the Wolf profile
 * (docs/rt1060-wolf-entropy.md).
 *
 * crux_entropy_boot_check() makes the first random read through the same function that makes every
 * key, the PSA one (wolfPSA, then wolfCrypt's Hash-DRBG, then wc_GenerateSeed and the entropy
 * driver of the devicetree's zephyr,entropy node), prints ONE line, and returns true only if the
 * read succeeded. A composition calls it once at boot, before it makes any key, and stops if it
 * returns false. The line has a fixed format, so that a host can check it on the UART:
 *
 *   CRUX-ENTROPY first-read=<ok|failed> source=<node name> compatible=<first compatible of the
 * node> bytes=32 status=<PSA status, 0 is success> device-ready=<1|0>
 *
 * (one line, single spaces). The node is the chosen zephyr,entropy, which is the one device that
 * wolfCrypt's Zephyr seed function reads. Compiling this file refuses a build where wolfCrypt would
 * use sys_rand_get() instead. The bytes read are wiped and are used for nothing. Nothing is said
 * about their quality.
 */
#ifndef CRUX_ENTROPY_BOOT_H
#define CRUX_ENTROPY_BOOT_H

#include <stdbool.h>

#define CRUX_ENTROPY_BOOT_BYTES 32u

bool crux_entropy_boot_check(void);

/* The line that the last call printed, without the newline, for a test. Empty before the first
 * call. */
const char *crux_entropy_boot_last_line(void);

#endif
