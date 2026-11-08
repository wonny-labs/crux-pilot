/*
 * What the bench image reports on the UART so that a board run can read its stack and heap limits
 * and check its listener certificate (row C2 of docs/rt1060-management-image-feasibility.md). The
 * lines and their grammar are in ../report/bench_report_format.h, and check_bench_uart.py judges them.
 */
#ifndef CRUX_MGMT_RT1060_BENCH_REPORT_H
#define CRUX_MGMT_RT1060_BENCH_REPORT_H

#include <stdbool.h>

/**
 * D4: prints once the SHA-256 fingerprint of the certificate that the certificate module made
 * active and that the services handed to the TLS credentials, as
 * `BENCH-CERT v=1 sha256=AA:BB:..:FF source=generated`. Call it after the services have started.
 * Returns false, with `BENCH-CERT unavailable` printed, when the module has no fingerprint or the
 * text is not 32 uppercase pairs: the image must not look ready then.
 */
bool crux_bench_report_certificate(bool generated);

/** Starts the thread that prints the stack and heap lines at boot, after each client and at idle. */
bool crux_bench_report_start(void);

/**
 * Records the calling thread's stack as the main thread's final figure. Call it as the last thing
 * main() does, because the main thread ends when main() returns and the kernel's list no longer
 * shows it. Safe before the report has started.
 */
void crux_bench_report_main_exit(void);

#endif
