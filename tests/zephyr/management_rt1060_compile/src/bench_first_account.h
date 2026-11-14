/* The first account of the RT1060-EVKB bench image (D3), from a password in a file outside the
 * repository. Row C4 of docs/rt1060-management-image-feasibility.md implements it: see
 * bench_first_account.c and
 * ../bench_first_account.cmake. */
#ifndef CRUX_BENCH_FIRST_ACCOUNT_H
#define CRUX_BENCH_FIRST_ACCOUNT_H

#include <stdbool.h>

/* Creates the first account, which main() asks for only when the store is empty: the declared name,
 * the administrator role, and the password of the bench file, which is never printed. Returns false
 * when it did not, and then the image must not serve. */
bool crux_bench_first_account(void);

#endif
