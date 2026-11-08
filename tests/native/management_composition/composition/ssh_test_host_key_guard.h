/*
 * The host key guard of the SSH composition (Issue #197). The composition holds the host key in
 * test form: the credential store generates it from test entropy, and the provider borrows it from
 * a static buffer that is never wiped, for the life of the process. No product handling of the host
 * key is defined, so this form must never reach a build that is not a declared native_sim test
 * image. ssh.cmake refuses the same at configuration, and check_ssh_guard.py compiles this header
 * with each setting. See docs/ssh-integrated-image.md.
 */
#ifndef CRUX_NATIVE_MANAGEMENT_SSH_TEST_HOST_KEY_GUARD_H
#define CRUX_NATIVE_MANAGEMENT_SSH_TEST_HOST_KEY_GUARD_H

#if !(defined(CONFIG_BOARD_NATIVE_SIM) && defined(CRUX_NATIVE_MANAGEMENT_TEST_IMAGE))
#error "The test host key reaches a build that is not a declared native_sim test image"
#endif
#if defined(CONFIG_TEST_RANDOM_GENERATOR)
#error "The SSH composition draws random values from its PSA provider, not a test generator"
#endif

#endif
