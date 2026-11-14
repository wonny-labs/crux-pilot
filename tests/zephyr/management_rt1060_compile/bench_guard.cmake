# SPDX-License-Identifier: Apache-2.0
# The CMake level of the bench guards of the RT1060-EVKB bench image (D1 of
# docs/rt1060-management-image-feasibility.md). The other two levels are Kconfig.bench and src/bench_guard.h.
#
# The native guards (composition.cmake, ssh.cmake, ssh_test_host_key_guard.h, test_tls_credentials.h) are not
# touched: they still refuse every board. This file allows the test forms of the integrated composition on ONE
# board and ONE bench, named in bench_declaration.json, and refuses everything else. It is a function, so that
# check_bench_guard.py can run it in script mode with each setting broken in turn, with no Zephyr.
#
#   crux_bench_guard()      refuses a build that is not the declared bench image
#   crux_bench_manifest()   writes bench-manifest.json beside the build: the declaration, the resolved
#                           settings, the patched Zephyr copy and the sources with their SHA-256
# IN_LIST below. A project has it from cmake_minimum_required, and a script run with cmake -P may not.
cmake_policy(SET CMP0057 NEW)
set(CRUX_BENCH_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(CRUX_BENCH_DECLARATION_FILE "${CMAKE_CURRENT_LIST_DIR}/bench_declaration.json")
# A check script may name another declaration. A build never can: the override exists in script mode only.
if(CMAKE_SCRIPT_MODE_FILE AND DEFINED CRUX_BENCH_DECLARATION_OVERRIDE)
    set(CRUX_BENCH_DECLARATION_FILE "${CRUX_BENCH_DECLARATION_OVERRIDE}")
endif()

# Reads the declaration into BENCH_* variables of the caller.
macro(crux_bench_read_declaration)
    file(READ "${CRUX_BENCH_DECLARATION_FILE}" _bench_json)
    string(JSON BENCH_DECLARATION GET "${_bench_json}" declaration)
    string(JSON BENCH_IMAGE_ID GET "${_bench_json}" image_id)
    string(JSON BENCH_TARGET GET "${_bench_json}" board target)
    string(JSON BENCH_KCONFIG_TARGET GET "${_bench_json}" board kconfig_target)
    string(JSON BENCH_REVISION GET "${_bench_json}" board revision)
    string(JSON BENCH_ADDRESS GET "${_bench_json}" network address)
    string(JSON BENCH_PREFIX_LENGTH GET "${_bench_json}" network prefix_length)
    string(JSON BENCH_NETMASK GET "${_bench_json}" network netmask)
    string(JSON BENCH_PEER GET "${_bench_json}" network peer)
    string(JSON BENCH_HTTPS_PORT GET "${_bench_json}" listeners https_port)
    string(JSON BENCH_SSH_PORT GET "${_bench_json}" listeners ssh_port)
    # Row C6: the load that the network buffers and the heap are sized for.
    foreach(_bench_field IN ITEMS frame_bytes mss_bytes window_segments ring_spare_frames
            typical_segment_bytes segment_header_bytes sender_threads_beyond_slots packets_per_sender
            header_buffers_per_sender margin_percent)
        string(TOUPPER "${_bench_field}" _bench_upper)
        string(JSON BENCH_NB_${_bench_upper} GET "${_bench_json}" network_buffers ${_bench_field})
    endforeach()
    foreach(_bench_field IN ITEMS ssh_connection_floor_bytes ssh_window_extra_bytes tls_record_bytes
            tls_record_overhead_bytes tls_connection_object_bytes tls_handshake_bytes other_users_bytes
            paper_peak_limit_percent)
        string(TOUPPER "${_bench_field}" _bench_upper)
        string(JSON BENCH_HEAP_${_bench_upper} GET "${_bench_json}" heap ${_bench_field})
    endforeach()
    string(JSON BENCH_RSA_SYMBOL GET "${_bench_json}" profile wolfssl_rsa symbol)
    string(JSON BENCH_RSA_DECISION GET "${_bench_json}" profile wolfssl_rsa decision)
    string(JSON BENCH_FIRST_ACCOUNT_USER GET "${_bench_json}" first_account user)
    string(JSON BENCH_FIRST_ACCOUNT_ROLE GET "${_bench_json}" first_account role)
    string(JSON BENCH_FIRST_ACCOUNT_ENV GET "${_bench_json}" first_account password_file_env)
    # The build-local Zephyr copy (D8, row C11): schema 2 of docs/rt1060-bench-freeze-and-capture-design.md. An ordered list
    # of patches, every file they touch with its final hash, one marker, and the tree digest that is declared once it is derived.
    string(JSON BENCH_ZEPHYR_SCHEMA GET "${_bench_json}" patched_zephyr schema)
    string(JSON BENCH_ZEPHYR_DECISION GET "${_bench_json}" patched_zephyr decision)
    string(JSON BENCH_ZEPHYR_PIN GET "${_bench_json}" patched_zephyr zephyr_pin)
    string(JSON BENCH_PATCH_MARKER GET "${_bench_json}" patched_zephyr marker_file)
    # Each patch as "name|sha256", in the order of application.
    set(BENCH_PATCHES "")
    string(JSON _bench_count LENGTH "${_bench_json}" patched_zephyr patches)
    math(EXPR _bench_last "${_bench_count} - 1")
    foreach(_bench_i RANGE ${_bench_last})
        string(JSON _bench_name GET "${_bench_json}" patched_zephyr patches ${_bench_i} name)
        string(JSON _bench_sha GET "${_bench_json}" patched_zephyr patches ${_bench_i} sha256)
        list(APPEND BENCH_PATCHES "${_bench_name}|${_bench_sha}")
    endforeach()
    # Each patched file as "path|change|sha256|patch,patch" (an empty hash for a deleted file).
    set(BENCH_PATCHED_FILES "")
    string(JSON _bench_count LENGTH "${_bench_json}" patched_zephyr patched_files)
    math(EXPR _bench_last "${_bench_count} - 1")
    foreach(_bench_i RANGE ${_bench_last})
        string(JSON _bench_path GET "${_bench_json}" patched_zephyr patched_files ${_bench_i} path)
        string(JSON _bench_change GET "${_bench_json}" patched_zephyr patched_files ${_bench_i} change)
        string(JSON _bench_type TYPE "${_bench_json}" patched_zephyr patched_files ${_bench_i} sha256)
        set(_bench_sha "")
        if(_bench_type STREQUAL "STRING")
            string(JSON _bench_sha GET "${_bench_json}" patched_zephyr patched_files ${_bench_i} sha256)
        endif()
        set(_bench_patches "")
        string(JSON _bench_listed LENGTH "${_bench_json}" patched_zephyr patched_files ${_bench_i} patches)
        math(EXPR _bench_listed_last "${_bench_listed} - 1")
        foreach(_bench_j RANGE ${_bench_listed_last})
            string(JSON _bench_listed_name GET "${_bench_json}" patched_zephyr patched_files ${_bench_i} patches ${_bench_j})
            list(APPEND _bench_patches "${_bench_listed_name}")
        endforeach()
        string(REPLACE ";" "," _bench_patches "${_bench_patches}")
        list(APPEND BENCH_PATCHED_FILES "${_bench_path}|${_bench_change}|${_bench_sha}|${_bench_patches}")
    endforeach()
    # The tree digest, or "null" while it is not derived. The guard does not compute it: the freeze does, after the build.
    string(JSON BENCH_TREE_ALGORITHM GET "${_bench_json}" patched_zephyr tree algorithm)
    set(BENCH_TREE_SHA256 "null")
    set(BENCH_TREE_FILE_COUNT "null")
    string(JSON _bench_type TYPE "${_bench_json}" patched_zephyr tree sha256)
    if(_bench_type STREQUAL "STRING")
        string(JSON _bench_value GET "${_bench_json}" patched_zephyr tree sha256)
        set(BENCH_TREE_SHA256 "\"${_bench_value}\"")
    endif()
    string(JSON _bench_type TYPE "${_bench_json}" patched_zephyr tree file_count)
    if(_bench_type STREQUAL "NUMBER")
        string(JSON BENCH_TREE_FILE_COUNT GET "${_bench_json}" patched_zephyr tree file_count)
    endif()
    # The settings that a patch relies on, each as "SYMBOL|patch name|reason".
    set(BENCH_REQUIRED_SETTINGS "")
    string(JSON _bench_count LENGTH "${_bench_json}" patched_zephyr requires_settings)
    math(EXPR _bench_last "${_bench_count} - 1")
    foreach(_bench_i RANGE ${_bench_last})
        string(JSON _bench_symbol GET "${_bench_json}" patched_zephyr requires_settings ${_bench_i} symbol)
        string(JSON _bench_patch GET "${_bench_json}" patched_zephyr requires_settings ${_bench_i} patch)
        string(JSON _bench_reason GET "${_bench_json}" patched_zephyr requires_settings ${_bench_i} reason)
        list(APPEND BENCH_REQUIRED_SETTINGS "${_bench_symbol}|${_bench_patch}|${_bench_reason}")
    endforeach()
    # Each test form as "name|SYMBOL|state|decision".
    set(BENCH_FORMS "")
    string(JSON _bench_form_count LENGTH "${_bench_json}" test_forms)
    math(EXPR _bench_last "${_bench_form_count} - 1")
    foreach(_bench_i RANGE ${_bench_last})
        string(JSON _bench_name MEMBER "${_bench_json}" test_forms ${_bench_i})
        string(JSON _bench_symbol GET "${_bench_json}" test_forms ${_bench_name} symbol)
        string(JSON _bench_state GET "${_bench_json}" test_forms ${_bench_name} state)
        string(JSON _bench_decision GET "${_bench_json}" test_forms ${_bench_name} decision)
        list(APPEND BENCH_FORMS "${_bench_name}|${_bench_symbol}|${_bench_state}|${_bench_decision}")
    endforeach()
endmacro()

# Row C6: what the declared load needs of the network buffers and the heap, in BENCH_NEED_* of the caller. The load is
# the session sizes of D9 (SSH connections, HTTPS clients), the web listener's backlog and the declared window, with a margin, from the arithmetic
# of docs/rt1060-bench-network-buffers.md. check_bench_guard.py writes the same rule again in Python and compares.
function(crux_bench_buffer_needs)
    set(data ${CONFIG_NET_BUF_DATA_SIZE})
    set(ring ${CONFIG_ETH_NXP_ENET_RX_BUFFERS})
    set(ssh ${CONFIG_CRUX_SSH_MAX_CONNECTIONS})
    set(http ${CONFIG_HTTP_SERVER_MAX_CLIENTS})
    math(EXPR window "${BENCH_NB_WINDOW_SEGMENTS} * ${BENCH_NB_MSS_BYTES}")
    math(EXPR frame_bufs "(${BENCH_NB_FRAME_BYTES} + ${data} - 1) / ${data}")
    math(EXPR served "${ssh} + ${http}")
    math(EXPR connections "${served} + ${CONFIG_CRUX_WEB_BACKLOG}")
    math(EXPR typical_segments "(${window} + ${BENCH_NB_TYPICAL_SEGMENT_BYTES} - 1) / ${BENCH_NB_TYPICAL_SEGMENT_BYTES}")
    math(EXPR typical_bufs "(${BENCH_NB_TYPICAL_SEGMENT_BYTES} + ${BENCH_NB_SEGMENT_HEADER_BYTES} + ${data} - 1) / ${data}")
    math(EXPR full_bufs "${BENCH_NB_WINDOW_SEGMENTS} * ${frame_bufs}")
    math(EXPR typical_window_bufs "${typical_segments} * ${typical_bufs}")
    set(held_bufs ${full_bufs})
    if(typical_window_bufs GREATER held_bufs)
        set(held_bufs ${typical_window_bufs})
    endif()
    set(held_pkts ${BENCH_NB_WINDOW_SEGMENTS})
    if(typical_segments GREATER held_pkts)
        set(held_pkts ${typical_segments})
    endif()
    math(EXPR senders "${ssh} + 1 + ${BENCH_NB_SENDER_THREADS_BEYOND_SLOTS}")
    math(EXPR window_data_bufs "(${window} + ${data} - 1) / ${data}")
    set(margin ${BENCH_NB_MARGIN_PERCENT})
    math(EXPR rx_bufs "(${margin} * (${connections} * ${held_bufs} + ${ring} * ${frame_bufs}) + 99) / 100")
    math(EXPR tx_bufs "(${margin} * (${served} * ${window_data_bufs} + ${senders} * (${frame_bufs} + ${BENCH_NB_HEADER_BUFFERS_PER_SENDER})) + 99) / 100")
    math(EXPR rx_pkts "(${margin} * (${connections} * ${held_pkts} + ${ring}) + 99) / 100")
    math(EXPR tx_pkts "(${margin} * ${senders} * ${BENCH_NB_PACKETS_PER_SENDER} + 99) / 100")
    math(EXPR ring_min "${BENCH_NB_WINDOW_SEGMENTS} + ${BENCH_NB_RING_SPARE_FRAMES}")
    # The heap: the paper peak of the declared sessions, and an arena in which it stays under the declared share.
    math(EXPR ssh_each "2 * ${CONFIG_CRUX_SSH_WINDOW_BYTES} + ${BENCH_HEAP_SSH_WINDOW_EXTRA_BYTES}")
    if(BENCH_HEAP_SSH_CONNECTION_FLOOR_BYTES GREATER ssh_each)
        set(ssh_each ${BENCH_HEAP_SSH_CONNECTION_FLOOR_BYTES})
    endif()
    math(EXPR tls_each "2 * (${BENCH_HEAP_TLS_RECORD_BYTES} + ${BENCH_HEAP_TLS_RECORD_OVERHEAD_BYTES}) + ${BENCH_HEAP_TLS_CONNECTION_OBJECT_BYTES}")
    math(EXPR paper_peak "${ssh} * ${ssh_each} + ${http} * ${tls_each} + ${BENCH_HEAP_TLS_HANDSHAKE_BYTES} + ${BENCH_HEAP_OTHER_USERS_BYTES}")
    math(EXPR arena "(${paper_peak} * 100 + ${BENCH_HEAP_PAPER_PEAK_LIMIT_PERCENT} - 1) / ${BENCH_HEAP_PAPER_PEAK_LIMIT_PERCENT}")
    set(BENCH_NEED_WINDOW ${window} PARENT_SCOPE)
    set(BENCH_NEED_RING ${ring_min} PARENT_SCOPE)
    set(BENCH_NEED_RX_BUFS ${rx_bufs} PARENT_SCOPE)
    set(BENCH_NEED_TX_BUFS ${tx_bufs} PARENT_SCOPE)
    set(BENCH_NEED_RX_PKTS ${rx_pkts} PARENT_SCOPE)
    set(BENCH_NEED_TX_PKTS ${tx_pkts} PARENT_SCOPE)
    set(BENCH_NEED_PAPER_PEAK ${paper_peak} PARENT_SCOPE)
    set(BENCH_NEED_ARENA ${arena} PARENT_SCOPE)
endfunction()

function(crux_bench_guard)
    crux_bench_read_declaration()

    # 1. The declaration. Only the bench image's CMakeLists sets it, and no native image does.
    if(NOT CRUX_MANAGEMENT_BENCH_IMAGE)
        message(FATAL_ERROR "This build is not a declared RT1060-EVKB bench image: set "
                            "CRUX_MANAGEMENT_BENCH_IMAGE only in the bench image, "
                            "tests/zephyr/management_rt1060_compile")
    endif()
    # 2. The board: exactly the declared target, and never native_sim.
    if(CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI OR
       NOT "${CONFIG_BOARD_TARGET}" STREQUAL "${BENCH_KCONFIG_TARGET}" OR
       NOT "${CONFIG_BOARD_REVISION}" STREQUAL "${BENCH_REVISION}")
        message(FATAL_ERROR "The bench image is restricted to ${BENCH_TARGET}: the test forms are accepted "
                            "on this board and no other")
    endif()
    # 3. The Kconfig level of the declaration, which already needs the board and the TRNG.
    if(NOT CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE)
        message(FATAL_ERROR "The Kconfig declaration CONFIG_CRUX_MANAGEMENT_BENCH_IMAGE is not set")
    endif()
    # 4. Entropy.
    if(CONFIG_TEST_RANDOM_GENERATOR OR CONFIG_FAKE_ENTROPY_NATIVE_SIM OR NOT CONFIG_ENTROPY_MCUX_TRNG)
        message(FATAL_ERROR "The bench image draws its random values from the PSA provider over the TRNG "
                            "driver: CONFIG_ENTROPY_MCUX_TRNG is required, and a test random generator and "
                            "fake entropy are refused")
    endif()
    # 5. Profile and services.
    if(NOT CONFIG_WOLFPSA OR CONFIG_MBEDTLS OR CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
        message(FATAL_ERROR "The bench image needs the Wolf profile: CONFIG_WOLFPSA, and no Mbed TLS")
    endif()
    if(NOT CONFIG_CRUX_SSH OR NOT CONFIG_CRUX_SSH_CLI OR NOT CONFIG_CRUX_SSH_SFTP OR
       NOT CONFIG_CRUX_WEB OR CONFIG_CRUX_WEB_PLAIN_HTTP_FOR_TESTS)
        message(FATAL_ERROR "The bench image is HTTPS only, and SSH with CLI and SFTP")
    endif()
    if(NOT CONFIG_CRUX_LOGGING)
        message(FATAL_ERROR "The security events write to Crux Logging: CONFIG_CRUX_LOGGING is required")
    endif()
    # 5b. wolfCrypt without RSA (D11, row C9): the declared profile uses none, and one module option drops it from
    # wolfCrypt, wolfSSL, the single-precision math and wolfPSA. A wolfSSL settings file would take the options out of
    # the build (they all depend on there being none) and hide whether RSA is built.
    if(CONFIG_${BENCH_RSA_SYMBOL})
        message(FATAL_ERROR "The bench image has no RSA (${BENCH_RSA_DECISION}): CONFIG_${BENCH_RSA_SYMBOL} is "
                            "refused, so that wolfCrypt is built with NO_RSA")
    endif()
    if(CONFIG_WOLFSSL_HAS_SETTINGS_FILE)
        message(FATAL_ERROR "The bench image takes wolfSSL's settings from the module's Kconfig options only: "
                            "a settings file would hide whether RSA is built")
    endif()
    # 6. Network: the static bench address, and listeners on that address only (D10).
    if(CONFIG_NET_DHCPV4 OR CONFIG_NET_IPV6 OR CONFIG_NET_CONFIG_SETTINGS)
        message(FATAL_ERROR "The bench image has the static bench address ${BENCH_ADDRESS} only: DHCP, IPv6 "
                            "and automatic address configuration are refused")
    endif()
    if(NOT "${CONFIG_CRUX_WEB_BIND_ADDRESS}" STREQUAL "${BENCH_ADDRESS}")
        message(FATAL_ERROR "The HTTPS listener must listen on the bench address only: "
                            "CONFIG_CRUX_WEB_BIND_ADDRESS=\"${BENCH_ADDRESS}\"")
    endif()
    if(NOT "${CONFIG_CRUX_SSH_BIND_ADDRESS}" STREQUAL "${BENCH_ADDRESS}")
        message(FATAL_ERROR "The SSH listener must listen on the bench address only: "
                            "CONFIG_CRUX_SSH_BIND_ADDRESS=\"${BENCH_ADDRESS}\"")
    endif()
    if(NOT "${CONFIG_CRUX_WEB_PORT}" STREQUAL "${BENCH_HTTPS_PORT}" OR
       NOT "${CONFIG_CRUX_SSH_PORT}" STREQUAL "${BENCH_SSH_PORT}")
        message(FATAL_ERROR "The listener ports are the declared ones: HTTPS ${BENCH_HTTPS_PORT}, "
                            "SSH ${BENCH_SSH_PORT}")
    endif()
    # 7. Time and the card: no time source (D7), and a mount never formats a card.
    if(CONFIG_RTC)
        message(FATAL_ERROR "The bench image has no time source (D7): CONFIG_RTC is refused")
    endif()
    if(CONFIG_FS_FATFS_MOUNT_MKFS)
        message(FATAL_ERROR "A mount never formats a card: CONFIG_FS_FATFS_MOUNT_MKFS is refused")
    endif()
    # 8. The test forms: each one is declared, and its state in the declaration is the setting.
    set(declared_symbols "")
    foreach(form IN LISTS BENCH_FORMS)
        string(REPLACE "|" ";" parts "${form}")
        list(GET parts 0 name)
        list(GET parts 1 symbol)
        list(GET parts 2 state)
        list(GET parts 3 decision)
        list(APPEND declared_symbols "CONFIG_${symbol}")
        if(state STREQUAL "selected" AND NOT CONFIG_${symbol})
            message(FATAL_ERROR "The test form ${name} (${decision}) is declared selected, and "
                                "CONFIG_${symbol} is not set")
        endif()
        if(NOT state STREQUAL "selected" AND CONFIG_${symbol})
            message(FATAL_ERROR "The test form ${name} (${decision}) is reserved and nothing implements it: "
                                "CONFIG_${symbol} is refused until its row changes the declaration")
        endif()
    endforeach()
    get_cmake_property(all_variables VARIABLES)
    foreach(variable IN LISTS all_variables)
        if(variable MATCHES "^CONFIG_CRUX_BENCH_" AND NOT variable IN_LIST declared_symbols AND
           ${variable})
            message(FATAL_ERROR "${variable} is a bench test form that bench_declaration.json does not list")
        endif()
    endforeach()
    # 8b. The file backend (D2, row C3): exactly one, and the provider, the disk drivers and the form agree. The
    # RAM disk image has no card driver at all, so no card is there to mount or format. The state of each form was
    # checked above, so a microSD build is possible only after the declaration says so.
    if(CONFIG_CRUX_BENCH_FILE_RAM_DISK AND CONFIG_CRUX_BENCH_FILE_MICROSD)
        message(FATAL_ERROR "The bench image has exactly one file backend: the RAM disk and the microSD card "
                            "forms are both set")
    endif()
    if(NOT CONFIG_CRUX_BENCH_FILE_RAM_DISK AND NOT CONFIG_CRUX_BENCH_FILE_MICROSD)
        message(FATAL_ERROR "The bench image needs a file backend: exactly one of the RAM disk and the microSD "
                            "card forms")
    endif()
    if(CONFIG_CRUX_BENCH_FILE_RAM_DISK)
        if(NOT CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK OR NOT CONFIG_DISK_DRIVER_RAM)
            message(FATAL_ERROR "The RAM disk form needs the RAM disk File provider and the RAM disk driver: "
                                "CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK and CONFIG_DISK_DRIVER_RAM")
        endif()
        if(CONFIG_CRUX_FILE_FATFS_RT1060 OR CONFIG_DISK_DRIVER_SDMMC OR CONFIG_IMX_USDHC OR
           CONFIG_DISK_DRIVER_FLASH)
            message(FATAL_ERROR "The RAM disk image has no card: the microSD provider, the SD card driver, the "
                                "USDHC host and the flash disk driver are refused")
        endif()
    else()
        if(NOT CONFIG_CRUX_FILE_FATFS_RT1060 OR NOT CONFIG_DISK_DRIVER_SDMMC)
            message(FATAL_ERROR "The microSD form needs the microSD File provider and the SD card driver: "
                                "CONFIG_CRUX_FILE_FATFS_RT1060 and CONFIG_DISK_DRIVER_SDMMC")
        endif()
        if(CONFIG_CRUX_FILE_FATFS_RT1060_RAM_DISK OR CONFIG_DISK_DRIVER_RAM)
            message(FATAL_ERROR "The microSD image has no RAM disk: the RAM disk provider and driver are refused")
        endif()
    endif()
    # 9. The build-local Zephyr copy (D8, row C11): the declared patches, applied in the declared order, and nothing else. The
    # copy has ONE marker, ".crux-wolf-tls", which holds the pin and the hashes of the patches in order of application:
    # scripts/prepare-wolf-tls-zephyr.sh writes "<pin> <hash 1>" and scripts/prepare-isn-fail-closed.py extends it. A missing,
    # an extra, a changed or a reordered patch is another marker text. Every patched file has its declared final hash.
    set(expected_marker "${BENCH_ZEPHYR_PIN}")
    set(patch_names "")
    foreach(entry IN LISTS BENCH_PATCHES)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 patch_name)
        list(GET parts 1 patch_hash)
        string(APPEND expected_marker " ${patch_hash}")
        list(APPEND patch_names "${patch_name}")
    endforeach()
    string(REPLACE ";" ", " patch_list "${patch_names}")
    set(marker "${ZEPHYR_BASE}/${BENCH_PATCH_MARKER}")
    if(NOT EXISTS "${marker}")
        message(FATAL_ERROR "ZEPHYR_BASE is not the build-local Zephyr copy with the declared patches (${patch_list}): the marker "
                            "${BENCH_PATCH_MARKER} is missing (scripts/prepare-wolf-tls-zephyr.sh, then scripts/prepare-isn-fail-closed.py)")
    endif()
    file(READ "${marker}" marker_text)
    string(STRIP "${marker_text}" marker_text)
    if(NOT "${marker_text}" STREQUAL "${expected_marker}")
        message(FATAL_ERROR "ZEPHYR_BASE is not the build-local Zephyr copy with the declared patches (${patch_list} on "
                            "${BENCH_ZEPHYR_PIN}): the marker ${BENCH_PATCH_MARKER} differs from the pin and the hashes of the "
                            "patches in the declared order")
    endif()
    # No other marker: a patch that the declaration does not name, or a copy made with an earlier version of the scripts.
    file(GLOB stray_markers LIST_DIRECTORIES false RELATIVE "${ZEPHYR_BASE}" "${ZEPHYR_BASE}/.crux-*")
    foreach(stray IN LISTS stray_markers)
        if(NOT "${stray}" STREQUAL "${BENCH_PATCH_MARKER}")
            message(FATAL_ERROR "ZEPHYR_BASE is not the build-local Zephyr copy with the declared patches (${patch_list}): "
                                "${stray} is a marker that the declaration does not name")
        endif()
    endforeach()
    foreach(entry IN LISTS BENCH_PATCHED_FILES)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 file_path)
        list(GET parts 1 file_change)
        list(GET parts 2 file_hash)
        set(patched "${ZEPHYR_BASE}/${file_path}")
        if("${file_change}" STREQUAL "deleted")
            if(EXISTS "${patched}")
                message(FATAL_ERROR "ZEPHYR_BASE is not the build-local Zephyr copy with the declared patches (${patch_list}): "
                                    "${file_path} must be deleted by them and is there")
            endif()
        else()
            if(NOT EXISTS "${patched}")
                message(FATAL_ERROR "ZEPHYR_BASE is not the build-local Zephyr copy with the declared patches (${patch_list}): "
                                    "${file_path} is missing")
            endif()
            file(SHA256 "${patched}" patched_hash)
            if(NOT "${patched_hash}" STREQUAL "${file_hash}")
                message(FATAL_ERROR "ZEPHYR_BASE is not the build-local Zephyr copy with the declared patches (${patch_list} on "
                                    "${BENCH_ZEPHYR_PIN}): ${file_path} differs from its declared final hash")
            endif()
        endif()
    endforeach()
    # 9b. The settings that a patch relies on. The fail-closed initial sequence number protects the RFC 6528 one only.
    foreach(entry IN LISTS BENCH_REQUIRED_SETTINGS)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 required_symbol)
        list(GET parts 1 required_patch)
        list(GET parts 2 required_reason)
        if(NOT CONFIG_${required_symbol})
            message(FATAL_ERROR "The patch ${required_patch} relies on CONFIG_${required_symbol}: CONFIG_${required_symbol} is "
                                "required, because ${required_reason}")
        endif()
    endforeach()
    if(CONFIG_NET_TEST OR CONFIG_NET_TEST_PROTOCOL)
        message(FATAL_ERROR "The bench image makes its initial sequence numbers: CONFIG_NET_TEST and "
                            "CONFIG_NET_TEST_PROTOCOL skip tcp_init_isn() and are refused")
    endif()
    # 10. The network buffers, the TCP windows and the arena (row C6): sized for the declared load. A pool below its
    # need, a window that is not the declared one, and a session size raised without the pools are refused.
    foreach(setting IN ITEMS NET_BUF_DATA_SIZE NET_BUF_RX_COUNT NET_BUF_TX_COUNT NET_PKT_RX_COUNT NET_PKT_TX_COUNT
            NET_TCP_MAX_RECV_WINDOW_SIZE NET_TCP_MAX_SEND_WINDOW_SIZE ETH_NXP_ENET_RX_BUFFERS
            CRUX_SSH_MAX_CONNECTIONS HTTP_SERVER_MAX_CLIENTS CRUX_WEB_BACKLOG CRUX_SSH_WINDOW_BYTES
            COMMON_LIBC_MALLOC_ARENA_SIZE)
        if(NOT "${CONFIG_${setting}}" MATCHES "^[0-9]+$")
            message(FATAL_ERROR "CONFIG_${setting} is not set to a number: the buffer and heap rule of row C6 "
                                "reads it")
        endif()
    endforeach()
    crux_bench_buffer_needs()
    if(NOT "${CONFIG_NET_TCP_MAX_RECV_WINDOW_SIZE}" STREQUAL "${BENCH_NEED_WINDOW}")
        message(FATAL_ERROR "The TCP receive window must be the declared ${BENCH_NEED_WINDOW} B "
                            "(${BENCH_NB_WINDOW_SEGMENTS} segments of ${BENCH_NB_MSS_BYTES} B), and not the value Zephyr "
                            "derives from the buffer counts: CONFIG_NET_TCP_MAX_RECV_WINDOW_SIZE is "
                            "${CONFIG_NET_TCP_MAX_RECV_WINDOW_SIZE}")
    endif()
    if(NOT "${CONFIG_NET_TCP_MAX_SEND_WINDOW_SIZE}" STREQUAL "${BENCH_NEED_WINDOW}")
        message(FATAL_ERROR "The TCP send window must be the declared ${BENCH_NEED_WINDOW} B "
                            "(${BENCH_NB_WINDOW_SEGMENTS} segments of ${BENCH_NB_MSS_BYTES} B), and not the value Zephyr "
                            "derives from the buffer counts: CONFIG_NET_TCP_MAX_SEND_WINDOW_SIZE is "
                            "${CONFIG_NET_TCP_MAX_SEND_WINDOW_SIZE}")
    endif()
    if(CONFIG_ETH_NXP_ENET_RX_BUFFERS LESS BENCH_NEED_RING)
        message(FATAL_ERROR "The Ethernet driver's receive ring of ${CONFIG_ETH_NXP_ENET_RX_BUFFERS} frames must "
                            "hold the declared window (${BENCH_NB_WINDOW_SEGMENTS} segments) and "
                            "${BENCH_NB_RING_SPARE_FRAMES} spare frames: at least ${BENCH_NEED_RING}")
    endif()
    if(CONFIG_NET_BUF_RX_COUNT LESS BENCH_NEED_RX_BUFS)
        message(FATAL_ERROR "CONFIG_NET_BUF_RX_COUNT ${CONFIG_NET_BUF_RX_COUNT} is under the ${BENCH_NEED_RX_BUFS} "
                            "receive data buffers that the declared load needs")
    endif()
    if(CONFIG_NET_BUF_TX_COUNT LESS BENCH_NEED_TX_BUFS)
        message(FATAL_ERROR "CONFIG_NET_BUF_TX_COUNT ${CONFIG_NET_BUF_TX_COUNT} is under the ${BENCH_NEED_TX_BUFS} "
                            "transmit data buffers that the declared load needs")
    endif()
    if(CONFIG_NET_PKT_RX_COUNT LESS BENCH_NEED_RX_PKTS)
        message(FATAL_ERROR "CONFIG_NET_PKT_RX_COUNT ${CONFIG_NET_PKT_RX_COUNT} is under the ${BENCH_NEED_RX_PKTS} "
                            "receive packets that the declared load needs")
    endif()
    if(CONFIG_NET_PKT_TX_COUNT LESS BENCH_NEED_TX_PKTS)
        message(FATAL_ERROR "CONFIG_NET_PKT_TX_COUNT ${CONFIG_NET_PKT_TX_COUNT} is under the ${BENCH_NEED_TX_PKTS} "
                            "transmit packets that the declared load needs")
    endif()
    if(CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE LESS BENCH_NEED_ARENA)
        message(FATAL_ERROR "CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE ${CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE} is under "
                            "the ${BENCH_NEED_ARENA} B in which the declared sessions' paper peak of "
                            "${BENCH_NEED_PAPER_PEAK} B stays under ${BENCH_HEAP_PAPER_PEAK_LIMIT_PERCENT} percent")
    endif()
    message(STATUS "Bench network buffers meet the declared load: rx buffers ${CONFIG_NET_BUF_RX_COUNT} (need "
                   "${BENCH_NEED_RX_BUFS}), tx buffers ${CONFIG_NET_BUF_TX_COUNT} (${BENCH_NEED_TX_BUFS}), rx packets "
                   "${CONFIG_NET_PKT_RX_COUNT} (${BENCH_NEED_RX_PKTS}), tx packets ${CONFIG_NET_PKT_TX_COUNT} "
                   "(${BENCH_NEED_TX_PKTS}), window ${BENCH_NEED_WINDOW}, arena ${CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE} "
                   "(${BENCH_NEED_ARENA})")
endfunction()

# The definitions that carry the declaration into C. src/bench_guard.h refuses a source without them.
function(crux_bench_definitions target)
    crux_bench_read_declaration()
    target_compile_definitions(${target} PRIVATE
        CRUX_MANAGEMENT_BENCH_IMAGE=1
        "CRUX_BENCH_IMAGE_ID=\"${BENCH_IMAGE_ID}\""
        "CRUX_BENCH_ADDRESS=\"${BENCH_ADDRESS}\""
        "CRUX_BENCH_NETMASK=\"${BENCH_NETMASK}\""
        CRUX_BENCH_PREFIX_LENGTH=${BENCH_PREFIX_LENGTH}
        "CRUX_BENCH_FIRST_ACCOUNT_USER=\"${BENCH_FIRST_ACCOUNT_USER}\""
        "CRUX_BENCH_FIRST_ACCOUNT_ROLE=\"${BENCH_FIRST_ACCOUNT_ROLE}\"")
endfunction()

# Writes ${CMAKE_BINARY_DIR}/bench-manifest.json. Call it last, after every source is added. It records what
# the guards accepted: it is a record and not a check.
function(crux_bench_manifest)
    crux_bench_read_declaration()
    set(manifest "{}")
    string(JSON manifest SET "${manifest}" schema 1)
    string(JSON manifest SET "${manifest}" declaration "\"${BENCH_DECLARATION}\"")
    string(JSON manifest SET "${manifest}" image_id "\"${BENCH_IMAGE_ID}\"")
    string(JSON manifest SET "${manifest}" board "\"${BENCH_TARGET}\"")
    string(JSON manifest SET "${manifest}" address "\"${BENCH_ADDRESS}/${BENCH_PREFIX_LENGTH}\"")
    string(JSON manifest SET "${manifest}" peer "\"${BENCH_PEER}\"")
    # The refusal in main() is a separate switch: it stays until a reviewed change removes it.
    if(CRUX_BENCH_RUN_REFUSED)
        string(JSON manifest SET "${manifest}" run_refused true)
    else()
        string(JSON manifest SET "${manifest}" run_refused false)
    endif()

    set(forms "{}")
    foreach(form IN LISTS BENCH_FORMS)
        string(REPLACE "|" ";" parts "${form}")
        list(GET parts 0 name)
        list(GET parts 1 symbol)
        list(GET parts 2 state)
        list(GET parts 3 decision)
        if(CONFIG_${symbol})
            set(on true)
        else()
            set(on false)
        endif()
        string(JSON forms SET "${forms}" ${name}
               "{\"symbol\": \"${symbol}\", \"decision\": \"${decision}\", \"state\": \"${state}\", \"set\": ${on}}")
    endforeach()
    string(JSON manifest SET "${manifest}" test_forms "${forms}")

    # The wolfSSL profile (D11, row C9): RSA is trimmed. CONFIG_${BENCH_RSA_SYMBOL} is off, which CMake cannot show as a
    # variable, so the manifest says what the guard accepted.
    set(profile "{}")
    string(JSON profile SET "${profile}" decision "\"${BENCH_RSA_DECISION}\"")
    string(JSON profile SET "${profile}" rsa "\"trimmed: CONFIG_${BENCH_RSA_SYMBOL} is off, so wolfCrypt is built with NO_RSA\"")
    string(JSON manifest SET "${manifest}" wolfssl_profile "${profile}")

    # The first account (D3, row C4): where its password comes from, and the hash of that file. Never the password.
    # The plaintext is in the image, as the report says, and so the manifest says it too.
    if(CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE)
        get_property(first_account_hash GLOBAL PROPERTY CRUX_BENCH_FIRST_ACCOUNT_SHA256)
        if("${first_account_hash}" STREQUAL "")
            message(FATAL_ERROR "The first account's password file was not checked before the manifest: "
                                "crux_bench_first_account_prepare() must run first")
        endif()
        set(first "{}")
        string(JSON first SET "${first}" decision "\"D3\"")
        string(JSON first SET "${first}" user "\"${BENCH_FIRST_ACCOUNT_USER}\"")
        string(JSON first SET "${first}" role "\"${BENCH_FIRST_ACCOUNT_ROLE}\"")
        string(JSON first SET "${first}" source
               "\"a file outside the repository, named by the environment variable ${BENCH_FIRST_ACCOUNT_ENV}\"")
        string(JSON first SET "${first}" file_sha256 "\"${first_account_hash}\"")
        string(JSON first SET "${first}" plaintext_in_image true)
        string(JSON first SET "${first}" plaintext_elsewhere
               "\"none: not in this manifest, the configuration, a generated source, a log or the map\"")
        string(JSON manifest SET "${manifest}" first_account "${first}")
    endif()

    set(settings "{}")
    get_cmake_property(all_variables VARIABLES)
    list(SORT all_variables)
    foreach(variable IN LISTS all_variables)
        if(variable MATCHES "^CONFIG_(CRUX_|HTTP_SERVER_STACK_SIZE|HTTP_SERVER_MAX_CLIENTS|MAIN_STACK_SIZE|COMMON_LIBC_MALLOC_ARENA_SIZE|NET_MAX_|NET_PKT_|NET_BUF_|NET_TCP_MAX_|NET_TCP_ISN_RFC6528|ETH_NXP_ENET_RX_BUFFERS|NET_SOCKETS_TLS_MAX_CONTEXTS|ENTROPY_MCUX_TRNG|RTC|BOARD_TARGET)")
            set(value "${${variable}}")
            string(REPLACE "\\" "\\\\" value "${value}")
            string(REPLACE "\"" "\\\"" value "${value}")
            string(JSON settings SET "${settings}" "${variable}" "\"${value}\"")
        endif()
    endforeach()
    string(JSON manifest SET "${manifest}" settings "${settings}")

    # The file backend (row C3, D2): what persists and what does not, which the owner reads before a run.
    set(backend "{}")
    if(CONFIG_CRUX_BENCH_FILE_RAM_DISK)
        set(ram_overlay "${CRUX_BENCH_DIR}/ram_disk.overlay")
        file(READ "${ram_overlay}" ram_overlay_text)
        string(REGEX MATCH "sector-size = <([0-9]+)>" _unused "${ram_overlay_text}")
        set(ram_sector_size "${CMAKE_MATCH_1}")
        string(REGEX MATCH "sector-count = <([0-9]+)>" _unused "${ram_overlay_text}")
        set(ram_sector_count "${CMAKE_MATCH_1}")
        math(EXPR ram_bytes "${ram_sector_size} * ${ram_sector_count}")
        string(JSON backend SET "${backend}" kind "\"ram_disk\"")
        string(JSON backend SET "${backend}" disk_name "\"RAM\"")
        string(JSON backend SET "${backend}" size_bytes "${ram_bytes}")
        string(JSON backend SET "${backend}" sector_size "${ram_sector_size}")
        string(JSON backend SET "${backend}" sector_count "${ram_sector_count}")
        string(JSON backend SET "${backend}" memory "\"SDRAM, the .bss of the RAM disk driver\"")
        string(JSON backend SET "${backend}" persists_across_reset false)
        string(JSON backend SET "${backend}" formatted_by_the_image "\"the RAM disk only, at every boot\"")
        string(JSON backend SET "${backend}" card_in_image false)
        string(JSON backend SET "${backend}" lost_at_reset "[\"accounts\", \"keys\", \"certificate\", \"events\", \"files\"]")
        string(JSON backend SET "${backend}" note "\"Every boot is a first boot. The host key and the certificate are generated again, so a client sees new fingerprints after a reset. That shows that the entropy source varies, and is not a statement on its quality.\"")
    elseif(CONFIG_CRUX_BENCH_FILE_MICROSD)
        string(JSON backend SET "${backend}" kind "\"microsd_card\"")
        string(JSON backend SET "${backend}" persists_across_reset true)
        string(JSON backend SET "${backend}" formatted_by_the_image "\"never\"")
        string(JSON backend SET "${backend}" card_in_image true)
    endif()
    string(JSON manifest SET "${manifest}" file_backend "${backend}")

    # The patched Zephyr copy: the block of the design (schema 2). It mirrors the declaration's keys of the same names, with the
    # pin as "pin", and it is what the freeze and the image check read (tests/target/patched_zephyr.py).
    set(zephyr "{}")
    string(JSON zephyr SET "${zephyr}" schema 2)
    string(JSON zephyr SET "${zephyr}" decision "\"${BENCH_ZEPHYR_DECISION}\"")
    string(JSON zephyr SET "${zephyr}" marker_file "\"${BENCH_PATCH_MARKER}\"")
    string(JSON zephyr SET "${zephyr}" pin "\"${BENCH_ZEPHYR_PIN}\"")
    set(listed_patches "[]")
    set(count 0)
    foreach(entry IN LISTS BENCH_PATCHES)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 patch_name)
        list(GET parts 1 patch_hash)
        string(JSON listed_patches SET "${listed_patches}" ${count} "{\"name\": \"${patch_name}\", \"sha256\": \"${patch_hash}\"}")
        math(EXPR count "${count} + 1")
    endforeach()
    string(JSON zephyr SET "${zephyr}" patches "${listed_patches}")
    set(listed_files "[]")
    set(count 0)
    foreach(entry IN LISTS BENCH_PATCHED_FILES)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 file_path)
        list(GET parts 1 file_change)
        list(GET parts 2 file_hash)
        list(GET parts 3 file_patches)
        if("${file_hash}" STREQUAL "")
            set(shown_hash "null")
        else()
            set(shown_hash "\"${file_hash}\"")
        endif()
        set(names "[]")
        set(name_count 0)
        string(REPLACE "," ";" file_patches "${file_patches}")
        foreach(listed_name IN LISTS file_patches)
            string(JSON names SET "${names}" ${name_count} "\"${listed_name}\"")
            math(EXPR name_count "${name_count} + 1")
        endforeach()
        string(JSON listed_files SET "${listed_files}" ${count}
               "{\"path\": \"${file_path}\", \"change\": \"${file_change}\", \"sha256\": ${shown_hash}, \"patches\": ${names}}")
        math(EXPR count "${count} + 1")
    endforeach()
    string(JSON zephyr SET "${zephyr}" patched_files "${listed_files}")
    string(JSON zephyr SET "${zephyr}" tree
           "{\"algorithm\": \"${BENCH_TREE_ALGORITHM}\", \"sha256\": ${BENCH_TREE_SHA256}, \"file_count\": ${BENCH_TREE_FILE_COUNT}}")
    string(JSON manifest SET "${manifest}" patched_zephyr "${zephyr}")

    # The source revision, when git can say it. A missing answer is recorded as such.
    set(commit "unavailable")
    set(dirty "unavailable")
    execute_process(COMMAND git -C "${CRUX_ROOT}" rev-parse HEAD OUTPUT_VARIABLE head_out
                    RESULT_VARIABLE head_status OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(head_status EQUAL 0)
        set(commit "${head_out}")
        execute_process(COMMAND git -C "${CRUX_ROOT}" status --porcelain --untracked-files=no
                        OUTPUT_VARIABLE status_out RESULT_VARIABLE status_status ERROR_QUIET)
        if(status_status EQUAL 0)
            if(status_out STREQUAL "")
                set(dirty "false")
            else()
                set(dirty "true")
            endif()
        endif()
    endif()
    string(JSON manifest SET "${manifest}" source_commit "\"${commit}\"")
    string(JSON manifest SET "${manifest}" source_tree_modified "\"${dirty}\"")

    # The sources of the image's own targets with their hashes. A generated file has no hash here.
    set(sources "[]")
    set(count 0)
    foreach(target app crux_ssh_wolf_provider crux_ssh_entries)
        if(NOT TARGET ${target})
            continue()
        endif()
        get_target_property(target_sources ${target} SOURCES)
        get_target_property(target_dir ${target} SOURCE_DIR)
        foreach(source IN LISTS target_sources)
            if(source MATCHES "\\$<")
                continue()
            endif()
            if(IS_ABSOLUTE "${source}")
                set(path "${source}")
            else()
                set(path "${target_dir}/${source}")
            endif()
            string(FIND "${path}" "${CMAKE_BINARY_DIR}/" in_build)
            if(in_build EQUAL 0)
                # Generated by the build: named from the build directory, with no hash at configuration.
                file(RELATIVE_PATH shown "${CMAKE_BINARY_DIR}" "${path}")
                set(shown "build/${shown}")
                set(hash "generated in the build directory")
            else()
                file(RELATIVE_PATH shown "${CRUX_ROOT}" "${path}")
                if(EXISTS "${path}")
                    file(SHA256 "${path}" hash)
                else()
                    set(hash "missing at configuration")
                endif()
            endif()
            string(JSON sources SET "${sources}" ${count}
                   "{\"path\": \"${shown}\", \"sha256\": \"${hash}\"}")
            math(EXPR count "${count} + 1")
        endforeach()
    endforeach()
    string(JSON manifest SET "${manifest}" sources "${sources}")

    # The files that configure the image, which are not sources: the declaration, the guards, the settings. The
    # profile files that CMakeLists.txt merges are listed by their repository path.
    set(configuration "[]")
    set(count 0)
    set(configuration_files
        "${CRUX_BENCH_DIR}/bench_declaration.json" "${CRUX_BENCH_DIR}/bench_guard.cmake"
        "${CRUX_BENCH_DIR}/management_target.cmake" "${CRUX_BENCH_DIR}/CMakeLists.txt"
        "${CRUX_BENCH_DIR}/bench_first_account.cmake" "${CRUX_BENCH_DIR}/bench_first_account_file.py"
        "${CRUX_BENCH_DIR}/Kconfig" "${CRUX_BENCH_DIR}/Kconfig.bench" "${CRUX_BENCH_DIR}/prj.conf"
        "${CRUX_BENCH_DIR}/target.conf" "${CRUX_BENCH_DIR}/target.overlay")
    foreach(file IN LISTS configuration_files EXTRA_CONF_FILE DTC_OVERLAY_FILE)
        if(NOT EXISTS "${file}")
            continue()
        endif()
        file(RELATIVE_PATH shown "${CRUX_ROOT}" "${file}")
        file(SHA256 "${file}" hash)
        string(JSON configuration SET "${configuration}" ${count} "{\"path\": \"${shown}\", \"sha256\": \"${hash}\"}")
        math(EXPR count "${count} + 1")
    endforeach()
    string(JSON manifest SET "${manifest}" configuration_files "${configuration}")
    file(WRITE "${CMAKE_BINARY_DIR}/bench-manifest.json" "${manifest}\n")
    message(STATUS "Bench manifest: ${CMAKE_BINARY_DIR}/bench-manifest.json (${count} sources)")
endfunction()
