/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CRUX_WOLF_ENTROPY_TEST_ENTROPY_H
#define CRUX_WOLF_ENTROPY_TEST_ENTROPY_H

#include <stdbool.h>

/* How many reads of the test device were asked for, whether or not they were served. */
unsigned int crux_test_entropy_reads(void);
/* Every read from now on fails with -EIO, or is served again. */
void crux_test_entropy_set_failing(bool failing);

#endif
