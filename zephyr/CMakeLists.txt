message(STATUS "Crux Zephyr integration module discovered")

# Crux's own targets are not product code. This directory definition reaches the targets that
# the module creates and no usage requirement, so the application does not inherit it. It keeps
# the opt-in frozen header warning (CRUX_WARN_FROZEN_HEADERS) out of Crux's own sources.
add_compile_definitions(CRUX_INTERNAL_BUILD)

# Rule L5 of the command line contract applies to every image, including one with no management.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/CruxManagementShell.cmake")
crux_check_management_shell()

if(CONFIG_CRUX_PILOT_MODULE)
    include("${CMAKE_CURRENT_LIST_DIR}/../cmake/CruxZephyrBaseline.cmake")
    crux_check_zephyr_baseline()

    add_library(crux_base INTERFACE)
    target_include_directories(crux_base INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/../components/base/include"
    )
    add_library(crux::base ALIAS crux_base)
endif()

# The per-service priority placement of the services' workers (ADR-0002 P3). It exposes nothing by
# itself: a composition links crux_service_priority_placement to the target that compiles a worker.
if(CONFIG_CRUX_PILOT_MODULE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/service_priority"
        "${CMAKE_CURRENT_BINARY_DIR}/service_priority")
endif()

if(CONFIG_CRUX_TIME OR CONFIG_CRUX_THREAD OR CONFIG_CRUX_SYNC OR CONFIG_CRUX_QUEUE OR CONFIG_CRUX_SYSTEM)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/interrupt"
        "${CMAKE_CURRENT_BINARY_DIR}/interrupt")
endif()

if(CONFIG_CRUX_THREAD)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/thread/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/thread")
    cmake_language(DEFER CALL crux_assert_portable_usage crux::thread)
endif()
if(CONFIG_CRUX_SYNC)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/sync/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/sync")
    cmake_language(DEFER CALL crux_assert_portable_usage crux::sync)
endif()


if(CONFIG_CRUX_TIME)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/time/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/time")
endif()


include("${CMAKE_CURRENT_LIST_DIR}/../cmake/CruxConsumerBoundary.cmake")
if(CONFIG_CRUX_PILOT_MODULE)
    cmake_language(DEFER CALL crux_assert_portable_usage crux::base)
endif()
if(CONFIG_CRUX_TIME)
    cmake_language(DEFER CALL crux_assert_portable_usage crux::time)
endif()

if(CONFIG_CRUX_SOCKETS)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/sockets/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/sockets")
    cmake_language(DEFER CALL crux_assert_portable_usage crux::sockets)
endif()
if(CONFIG_CRUX_NETIF)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/netif/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/netif")
    cmake_language(DEFER CALL crux_assert_portable_usage crux::netif)
endif()

if(CONFIG_CRUX_QUEUE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/queue/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/queue")
endif()


if(CONFIG_CRUX_SERIAL)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/serial/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/serial")
endif()

if(CONFIG_CRUX_CLI)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/cli/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/cli")
endif()

if(CONFIG_CRUX_I2C)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/i2c/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/i2c")
endif()

if(CONFIG_CRUX_SPI)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/spi/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/spi")
endif()

if(CONFIG_CRUX_LOGGING)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/logging/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/logging")
endif()

if(CONFIG_CRUX_GPIO)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/gpio/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/gpio")
endif()

if(CONFIG_CRUX_SYSTEM)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/system/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/system")
endif()

if(CONFIG_CRUX_FILE_SIMULATOR OR CONFIG_CRUX_FILE_FATFS_SIMULATOR OR
   CONFIG_CRUX_FILE_FATFS_RT1060 OR CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK OR
   CONFIG_CRUX_FILE_FATFS_RT1060_COMPILE_CHECK OR CONFIG_CRUX_FILE_RELIANCE_EDGE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/file/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/file")
    if(CONFIG_CRUX_FILE_SIMULATOR OR CONFIG_CRUX_FILE_FATFS_SIMULATOR OR
       CONFIG_CRUX_FILE_FATFS_RT1060 OR CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK OR
       CONFIG_CRUX_FILE_RELIANCE_EDGE)
        cmake_language(DEFER CALL crux_assert_portable_usage crux::file)
    endif()
endif()

if(CONFIG_CRUX_MMC_CACHE_FLUSH)
    include("${CMAKE_CURRENT_LIST_DIR}/mmc_cache_flush.cmake")
endif()

if(CONFIG_CRUX_FS_RELIANCE_EDGE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/fs/reliance_edge"
        "${CMAKE_CURRENT_BINARY_DIR}/fs_reliance_edge")
endif()

if(CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/configuration/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/configuration")
    # The Settings File backend of the pinned Zephyr hides I/O faults. The provider is built with
    # the corrected build-local copies of its sources, which the fixture of
    # tests/native/configuration_settings_file also uses, so a product build reaches a file under
    # tests/. CRUX_SETTINGS_FILE_CONTROL keeps the unpatched backend, for the fixture's negative
    # control only.
    if(CRUX_SETTINGS_FILE_CONTROL)
        if(NOT CONFIG_BOARD_NATIVE_SIM)
            message(FATAL_ERROR "CRUX_SETTINGS_FILE_CONTROL is a native_sim test fixture setting "
                "and is refused on any other board")
        endif()
    else()
        include("${CMAKE_CURRENT_LIST_DIR}/../tests/native/configuration_settings_file/candidate.cmake")
    endif()
endif()

if(CONFIG_CRUX_WEB)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/web/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/web")
    cmake_language(DEFER CALL crux_assert_portable_usage crux::web)
endif()

# ADR-0004 phase 0: Access, the credential store and the security events, which are wired to each
# other, so one symbol builds the three. Off by default, and an image without the symbol is unchanged.
# Each library is registered with the Zephyr link. No crux:: target and no public header is added: see
# components/access/ports/zephyr/CMakeLists.txt. What the three need from the rest of the image is
# refused here, at configuration, with the missing piece named.
if(CONFIG_CRUX_ACCESS)
    if(NOT CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
        message(FATAL_ERROR "CONFIG_CRUX_ACCESS needs the Mbed profile of PSA Crypto "
                            "(CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS): its ports are the PSA ports")
    endif()
    if(NOT CONFIG_TF_PSA_CRYPTO_USER_CONFIG)
        message(FATAL_ERROR "CONFIG_CRUX_ACCESS needs the locks that the Mbed profile's core calls "
                            "(CONFIG_TF_PSA_CRYPTO_USER_CONFIG): see "
                            "tests/native/management_composition/mbed-threading.conf")
    endif()
    if(NOT TARGET crux::file)
        message(FATAL_ERROR "CONFIG_CRUX_ACCESS needs a Crux File provider for the credential store's "
                            "two roots: set one of the CONFIG_CRUX_FILE_* selections")
    endif()
    if(NOT CONFIG_CRUX_LOGGING)
        message(FATAL_ERROR "CONFIG_CRUX_ACCESS needs CONFIG_CRUX_LOGGING: the security events "
                            "write their records through it")
    endif()
    if(NOT CONFIG_HWINFO)
        message(FATAL_ERROR "CONFIG_CRUX_ACCESS needs CONFIG_HWINFO: the credential store's device ID "
                            "comes from it")
    endif()
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/access/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/access")
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/credential/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/credential")
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/security_event/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/security_event")
endif()

# ADR-0004 phase 0, slice 2: the Configuration core, the Configuration service and the status service.
# Each is off by default and added only under its symbol. The core defers to the Settings File selection
# when that is on (components/configuration/ports/zephyr/core/CMakeLists.txt).
if(CONFIG_CRUX_CONFIGURATION_CORE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/configuration/ports/zephyr/core"
        "${CMAKE_CURRENT_BINARY_DIR}/configuration_core")
endif()
if(CONFIG_CRUX_CONFIG_SERVICE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/config_service/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/config_service")
endif()
if(CONFIG_CRUX_MGMT_STATUS)
    if(NOT TARGET crux::system)
        message(FATAL_ERROR "CONFIG_CRUX_MGMT_STATUS needs CONFIG_CRUX_SYSTEM: the status service reads "
                            "the System contract")
    endif()
    if(NOT TARGET crux::netif)
        message(FATAL_ERROR "CONFIG_CRUX_MGMT_STATUS needs CONFIG_CRUX_NETIF: the status service reads "
                            "the network interface")
    endif()
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/mgmt_status/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/mgmt_status")
endif()

# SSH has no crux:: target and no public header, so the module builds nothing for it. It refuses what no
# composition could make work, at configuration, and a composition then calls crux_ssh_wolf_settings() from
# components/ssh/ports/zephyr/wolf_ssh_settings.cmake, which reads CONFIG_CRUX_SSH_CLI, CONFIG_CRUX_SSH_SFTP
# and the sizes of the provider from the Kconfig symbols.
if(CONFIG_CRUX_SSH)
    # The design gives the Mbed profile no SSH provider, and two TLS stacks are not a supported image.
    if(CONFIG_MBEDTLS OR CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
        message(FATAL_ERROR "CONFIG_CRUX_SSH needs the Wolf profile. The Mbed profile has no SSH provider: "
                            "see docs/plans/secure-management-design.md#provider-profiles")
    endif()
    if(NOT CONFIG_WOLFSSL OR NOT CONFIG_WOLFSSH)
        message(FATAL_ERROR "CONFIG_CRUX_SSH needs CONFIG_WOLFSSL and CONFIG_WOLFSSH, from the "
                            "crux-secure-management west group")
    endif()
    if(NOT CONFIG_WOLFSSH_SETTINGS_FILE STREQUAL "crux_wolfssh_settings.h")
        message(FATAL_ERROR "CONFIG_CRUX_SSH needs CONFIG_WOLFSSH_SETTINGS_FILE=\"crux_wolfssh_settings.h\": "
                            "add components/ssh/ports/zephyr/wolf-ssh.conf to EXTRA_CONF_FILE")
    endif()
endif()
