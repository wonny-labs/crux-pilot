#ifndef CRUX_RUNTIME_FIXTURE_H
#define CRUX_RUNTIME_FIXTURE_H

/* Callable from a host runner or an independently configured firmware app.
 * Single owner, no concurrent or recursive calls. Returns zero on success,
 * otherwise the source line of the failed check. */
int crux_runtime_fixture_run(void);

#endif
