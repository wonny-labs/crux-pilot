message(STATUS "Crux Zephyr integration module discovered")

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
   CONFIG_CRUX_FILE_FATFS_RT1060 OR CONFIG_CRUX_FILE_FATFS_RT1060_COMPILE_CHECK OR
   CONFIG_CRUX_FILE_RELIANCE_EDGE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/file/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/file")
    if(CONFIG_CRUX_FILE_SIMULATOR OR CONFIG_CRUX_FILE_FATFS_SIMULATOR OR
       CONFIG_CRUX_FILE_FATFS_RT1060 OR CONFIG_CRUX_FILE_RELIANCE_EDGE)
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
