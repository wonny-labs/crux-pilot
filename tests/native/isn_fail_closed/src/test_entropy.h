/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CRUX_ISN_TEST_ENTROPY_H
#define CRUX_ISN_TEST_ENTROPY_H

/* Zero: reads work. Else: every read fails with this (negative) value. */
extern volatile int crux_test_entropy_result;
unsigned int crux_test_entropy_reads(void);

#endif
