/*
 * The entropy guard of the services composition (Issue #242). Entropy on native_sim is test-only,
 * so test entropy must never reach a build that has not declared itself a native_sim test image,
 * and Zephyr's test random generator must never be the source at all: the PSA provider draws from
 * the entropy driver. services.cmake refuses the same at configuration, and check_cmake_guard.py
 * compiles this header with each setting. See docs/psa-crypto-design.md.
 */
#ifndef CRUX_NATIVE_MANAGEMENT_ENTROPY_GUARD_H
#define CRUX_NATIVE_MANAGEMENT_ENTROPY_GUARD_H

#if defined(CONFIG_TEST_RANDOM_GENERATOR)
#error "The services composition draws random values from its PSA provider, not a test generator"
#endif
#if defined(CONFIG_FAKE_ENTROPY_NATIVE_SIM) &&                                                     \
    !(defined(CONFIG_BOARD_NATIVE_SIM) && defined(CRUX_NATIVE_MANAGEMENT_TEST_IMAGE))
#error "Test entropy reaches a build that is not a declared native_sim test image"
#endif

#endif
