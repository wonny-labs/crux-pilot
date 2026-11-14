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
/* D11, row C9: wolfCrypt is built without RSA. The declared profile uses none, one module option
 * drops it, and a settings file for wolfSSL would hide whether it is built. */
#if defined(CONFIG_WOLFSSL_RSA)
#error "The bench image has no RSA (D11): CONFIG_WOLFSSL_RSA is refused"
#endif
#if defined(CONFIG_WOLFSSL_HAS_SETTINGS_FILE)
#error "The bench image takes wolfSSL's settings from the module's Kconfig options only"
#endif
/* D-c, row C11: the TCP initial sequence number is the keyed one (RFC 6528), which the second patch
 * of the build-local Zephyr copy makes fail closed. Without the setting Zephyr takes the number
 * from sys_rand32_get(), and a test configuration skips tcp_init_isn() altogether. */
#if !defined(CONFIG_NET_TCP_ISN_RFC6528)
#error                                                                                             \
    "The bench image needs the RFC 6528 initial sequence number: CONFIG_NET_TCP_ISN_RFC6528 is required"
#endif
#if defined(CONFIG_NET_TEST) || defined(CONFIG_NET_TEST_PROTOCOL)
#error                                                                                             \
    "The bench image makes its initial sequence numbers: CONFIG_NET_TEST and CONFIG_NET_TEST_PROTOCOL are refused"
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

/* The file backend (D2, row C3): the glue and the decision of src/bench_medium*.c rely on these.
 * Each form is checked when it is set, so a source that does not use the medium compiles without
 * one. */
#if defined(CONFIG_FS_FATFS_MOUNT_MKFS)
#error "A mount never formats a card: CONFIG_FS_FATFS_MOUNT_MKFS is refused in the bench image"
#endif
#if defined(CONFIG_CRUX_BENCH_FILE_RAM_DISK) && defined(CONFIG_CRUX_BENCH_FILE_MICROSD)
#error "The bench image has exactly one file backend, and both of its forms are set"
#endif
#if defined(CONFIG_CRUX_BENCH_FILE_RAM_DISK)
#if !defined(CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK) || !defined(CONFIG_DISK_DRIVER_RAM)
#error "The RAM disk form needs the RAM disk File provider and the RAM disk driver"
#endif
#if defined(CONFIG_CRUX_FILE_FATFS_RT1060) || defined(CONFIG_DISK_DRIVER_SDMMC) ||                 \
    defined(CONFIG_IMX_USDHC) || defined(CONFIG_DISK_DRIVER_FLASH)
#error                                                                                             \
    "The RAM disk image has no card: the microSD provider, the SD card driver, the USDHC host and the flash disk are refused"
#endif
#endif
#if defined(CONFIG_CRUX_BENCH_FILE_MICROSD)
#if !defined(CONFIG_CRUX_FILE_FATFS_RT1060) || !defined(CONFIG_DISK_DRIVER_SDMMC)
#error "The microSD form needs the microSD File provider and the SD card driver"
#endif
#if defined(CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK) || defined(CONFIG_DISK_DRIVER_RAM)
#error "The microSD image has no RAM disk: the RAM disk provider and driver are refused"
#endif
#endif

/* A test form's own source adds its line, as ssh_target.c does:
 *   #ifndef CONFIG_CRUX_BENCH_TEST_HOST_KEY
 *   #error "..."
 *   #endif
 */

#endif
