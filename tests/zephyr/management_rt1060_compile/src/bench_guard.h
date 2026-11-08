/*
 * The C level of the bench guards of the RT1060-EVKB bench image (D1 of
 * docs/rt1060-management-image-feasibility.md). Every source of this image that holds a test form
 * includes it. The CMake level is ../bench_guard.cmake and the Kconfig level is ../Kconfig.bench.
 *
 * The native guards (ssh_test_host_key_guard.h, test_tls_credentials.h, entropy_guard.h) are not
 * touched: they still refuse every board. This header accepts the test forms on one board and one
 * bench, which only ../CMakeLists.txt declares, and refuses everything else.
 * ../check_bench_guard.py compiles it with each setting broken in turn.
 */
#ifndef CRUX_BENCH_GUARD_H
#define CRUX_BENCH_GUARD_H

/* The declaration reaches C twice, from CMake and from Kconfig, and a source needs both. */
#if !defined(CRUX_MANAGEMENT_BENCH_IMAGE)
#error "A bench test form reaches a build that CMake did not declare as the RT1060-EVKB bench image"
#endif
#if !defined(CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE)
#error                                                                                             \
    "A bench test form reaches a build whose Kconfig does not declare the RT1060-EVKB bench image"
#endif
#if defined(CONFIG_BOARD_NATIVE_SIM) || !defined(CONFIG_BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI) ||   \
    !defined(CONFIG_BOARD_REVISION_B)
#error "The bench image is restricted to RT1060-EVKB revision B, QSPI"
#endif
#if defined(CONFIG_TEST_RANDOM_GENERATOR) || defined(CONFIG_FAKE_ENTROPY_NATIVE_SIM) ||            \
    !defined(CONFIG_ENTROPY_MCUX_TRNG)
#error "The bench image draws its random values from the PSA provider over the TRNG driver"
#endif
#if !defined(CRUX_BENCH_ADDRESS) || !defined(CRUX_BENCH_NETMASK) || !defined(CRUX_BENCH_IMAGE_ID)
#error "The bench declaration did not reach the compiler: address, netmask and image identity"
#endif

/* D10: both listeners are bound to the bench address and no other. The text is compared at compile
 * time, because the Zephyr server falls back to every address for a text that is not an address. */
#if !defined(CONFIG_CRUX_WEB_BIND_ADDRESS) || !defined(CONFIG_CRUX_SSH_BIND_ADDRESS)
#error "The bench image sets both listener bind addresses"
#endif
_Static_assert(__builtin_strcmp(CONFIG_CRUX_WEB_BIND_ADDRESS, CRUX_BENCH_ADDRESS) == 0,
               "The HTTPS listener must listen on the bench address only");
_Static_assert(__builtin_strcmp(CONFIG_CRUX_SSH_BIND_ADDRESS, CRUX_BENCH_ADDRESS) == 0,
               "The SSH listener must listen on the bench address only");

/* A test form's own source adds its line, as ssh_target.c does:
 *   #ifndef CONFIG_CRUX_BENCH_TEST_HOST_KEY
 *   #error "..."
 *   #endif
 */

#endif
