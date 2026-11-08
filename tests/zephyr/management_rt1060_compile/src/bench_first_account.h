/* The first account of the RT1060-EVKB bench image (D3). Row C4 of
 * docs/rt1060-management-image-feasibility.md implements it. */
#ifndef CRUX_BENCH_FIRST_ACCOUNT_H
#define CRUX_BENCH_FIRST_ACCOUNT_H

#include <stdbool.h>

/* Creates the first account on an empty store, with a password that comes from outside the
 * repository and is never printed. Returns false when it did not, and then the image must not
 * serve. Until row C4 it always returns false: the native images' public test password is not used
 * on a bench board (D3), and nothing else supplies one yet. */
bool crux_bench_first_account(void);

#endif
