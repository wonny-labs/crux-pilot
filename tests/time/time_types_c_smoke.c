#include <crux/time_types.h>

/* This translation unit intentionally compiles as strict C17. */
static const crux_duration_t k_static_duration = CRUX_DURATION_MS_INIT(-3);
_Static_assert(sizeof(crux_duration_t) == sizeof(int64_t), "Duration storage");
_Static_assert(sizeof(crux_monotonic_time_t) == sizeof(int64_t), "Monotonic storage");

int crux_time_types_c_smoke(void) {
    crux_duration_t from_function = crux_duration_from_ms(-3);
    return k_static_duration.private_ns == from_function.private_ns &&
           crux_duration_to_us_ceil(crux_duration_from_ns(-1500)) == -1;
}
