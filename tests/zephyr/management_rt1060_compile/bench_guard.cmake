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
    string(JSON BENCH_ZEPHYR_PIN GET "${_bench_json}" patched_zephyr zephyr_pin)
    string(JSON BENCH_PATCH_NAME GET "${_bench_json}" patched_zephyr patch_name)
    string(JSON BENCH_PATCH_SHA256 GET "${_bench_json}" patched_zephyr patch_sha256)
    string(JSON BENCH_PATCH_MARKER GET "${_bench_json}" patched_zephyr marker_file)
    string(JSON BENCH_PATCHED_FILE GET "${_bench_json}" patched_zephyr patched_file)
    string(JSON BENCH_PATCHED_FILE_SHA256 GET "${_bench_json}" patched_zephyr patched_file_sha256)
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
    # 9. The build-local Zephyr copy with the wolfSSL TLS socket patch (D8).
    set(marker "${ZEPHYR_BASE}/${BENCH_PATCH_MARKER}")
    set(patched "${ZEPHYR_BASE}/${BENCH_PATCHED_FILE}")
    if(NOT EXISTS "${marker}" OR NOT EXISTS "${patched}")
        message(FATAL_ERROR "ZEPHYR_BASE is not the build-local Zephyr copy with the wolfSSL TLS socket "
                            "patch (scripts/prepare-wolf-tls-zephyr.sh): ${BENCH_PATCH_MARKER} is missing")
    endif()
    file(READ "${marker}" marker_text)
    string(STRIP "${marker_text}" marker_text)
    file(SHA256 "${patched}" patched_hash)
    if(NOT "${marker_text}" STREQUAL "${BENCH_ZEPHYR_PIN} ${BENCH_PATCH_SHA256}" OR
       NOT "${patched_hash}" STREQUAL "${BENCH_PATCHED_FILE_SHA256}")
        message(FATAL_ERROR "ZEPHYR_BASE is not the build-local Zephyr copy with the wolfSSL TLS socket "
                            "patch that the declaration names (${BENCH_PATCH_NAME} on ${BENCH_ZEPHYR_PIN})")
    endif()
endfunction()

# The definitions that carry the declaration into C. src/bench_guard.h refuses a source without them.
function(crux_bench_definitions target)
    crux_bench_read_declaration()
    target_compile_definitions(${target} PRIVATE
        CRUX_MANAGEMENT_BENCH_IMAGE=1
        "CRUX_BENCH_IMAGE_ID=\"${BENCH_IMAGE_ID}\""
        "CRUX_BENCH_ADDRESS=\"${BENCH_ADDRESS}\""
        "CRUX_BENCH_NETMASK=\"${BENCH_NETMASK}\""
        CRUX_BENCH_PREFIX_LENGTH=${BENCH_PREFIX_LENGTH})
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

    set(settings "{}")
    get_cmake_property(all_variables VARIABLES)
    list(SORT all_variables)
    foreach(variable IN LISTS all_variables)
        if(variable MATCHES "^CONFIG_(CRUX_|HTTP_SERVER_STACK_SIZE|MAIN_STACK_SIZE|COMMON_LIBC_MALLOC_ARENA_SIZE|NET_MAX_|NET_SOCKETS_TLS_MAX_CONTEXTS|ENTROPY_MCUX_TRNG|RTC|BOARD_TARGET)")
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

    set(zephyr "{}")
    string(JSON zephyr SET "${zephyr}" decision "\"D8\"")
    string(JSON zephyr SET "${zephyr}" pin "\"${BENCH_ZEPHYR_PIN}\"")
    string(JSON zephyr SET "${zephyr}" patch "\"${BENCH_PATCH_NAME}\"")
    string(JSON zephyr SET "${zephyr}" patch_sha256 "\"${BENCH_PATCH_SHA256}\"")
    string(JSON zephyr SET "${zephyr}" patched_file "\"${BENCH_PATCHED_FILE}\"")
    string(JSON zephyr SET "${zephyr}" patched_file_sha256 "\"${BENCH_PATCHED_FILE_SHA256}\"")
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
