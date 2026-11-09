# SPDX-License-Identifier: Apache-2.0
# How the RT1060-EVKB bench image gets its first account (row C4, D3 of docs/rt1060-management-image-feasibility.md).
#
# The password is in a file that is NOT in the repository. The file is named by the environment variable
# CRUX_BENCH_PASSWORD_FILE (the value is a path and is not recorded in CMake's cache), is checked by
# bench_first_account_file.py, and is embedded in the image by an assembler `.incbin` of that file. So the plaintext is
# in no generated source, header or configuration file: it is in the object that holds it, in the archive, in the
# ELF and in the binary. The Access API takes a plaintext password and derives its own record, so no pre-derived
# form exists to embed instead: the report says so, and says what a product does.
#
#   crux_bench_first_account_prepare()   refuses a missing, empty or too short file, embeds the bytes, and keeps the
#                                        file's SHA-256 for the manifest
#   crux_bench_first_account_sha256(var) the hash that prepare recorded
#
# This function is the only place that names the file. It runs only for the form CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE,
# which exists only under the bench declaration (Kconfig.bench), and crux_bench_guard() has already refused every build
# that is not the declared bench image, so a build without the declaration never reaches it and never holds the bytes.
cmake_policy(SET CMP0057 NEW)
set(CRUX_BENCH_FIRST_ACCOUNT_TOOL "${CMAKE_CURRENT_LIST_DIR}/bench_first_account_file.py")

# A Python 3 for the check. Zephyr names its own, and a script run with cmake -P (check_first_account.py) has neither.
macro(crux_bench_find_python out)
    set(${out} "")
    foreach(candidate IN ITEMS "${Python3_EXECUTABLE}" "${PYTHON_EXECUTABLE}" "${WEST_PYTHON}")
        if(NOT ${out} AND candidate AND EXISTS "${candidate}")
            set(${out} "${candidate}")
        endif()
    endforeach()
    if(NOT ${out})
        find_program(_crux_bench_python NAMES python3 python)
        set(${out} "${_crux_bench_python}")
    endif()
    if(NOT ${out})
        message(FATAL_ERROR "The bench first account needs Python 3 to check its password file")
    endif()
endmacro()

# Asm source of the embedded bytes. It names the file by path and holds no password. `%progbits` is the ARM spelling
# and the one the host assemblers accept too.
function(crux_bench_first_account_asm out path)
    set(text "")
    string(APPEND text "\t.section .rodata.crux_bench_first_account,\"a\",%progbits\n")
    string(APPEND text "\t.balign 4\n")
    string(APPEND text "\t.global crux_bench_first_account_password\n")
    string(APPEND text "\t.type crux_bench_first_account_password, %object\n")
    string(APPEND text "crux_bench_first_account_password:\n")
    string(APPEND text "\t.incbin \"${path}\"\n")
    string(APPEND text "crux_bench_first_account_password_end:\n")
    string(APPEND text "\t.size crux_bench_first_account_password, . - crux_bench_first_account_password\n")
    string(APPEND text "\t.balign 4\n")
    string(APPEND text "\t.global crux_bench_first_account_password_size\n")
    string(APPEND text "\t.type crux_bench_first_account_password_size, %object\n")
    string(APPEND text "crux_bench_first_account_password_size:\n")
    string(APPEND text "\t.long crux_bench_first_account_password_end - crux_bench_first_account_password\n")
    string(APPEND text "\t.size crux_bench_first_account_password_size, 4\n")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()

# Checks the file and returns its hash in `hash_var`, or stops the configuration. The reason says what is wrong with
# the file and never what is in it.
function(crux_bench_first_account_check hash_var)
    if("$ENV{CRUX_BENCH_PASSWORD_FILE}" STREQUAL "")
        message(FATAL_ERROR "The bench image's first account (D3) comes from a password file outside the repository: "
                            "set CRUX_BENCH_PASSWORD_FILE to its path. Make one with: "
                            "python3 ${CRUX_BENCH_FIRST_ACCOUNT_TOOL} generate --file <path outside any repository>")
    endif()
    crux_bench_find_python(python)
    execute_process(COMMAND "${python}" "${CRUX_BENCH_FIRST_ACCOUNT_TOOL}" check
                            --file "$ENV{CRUX_BENCH_PASSWORD_FILE}" --repo "${CRUX_ROOT}"
                    RESULT_VARIABLE status OUTPUT_VARIABLE found ERROR_VARIABLE reason
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "The bench image's first account: the password file is refused. ${reason}")
    endif()
    string(JSON hash GET "${found}" sha256)
    set(${hash_var} "${hash}" PARENT_SCOPE)
endfunction()

function(crux_bench_first_account_prepare)
    if(NOT CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE)
        message(FATAL_ERROR "The bench first account is prepared only for CONFIG_CRUX_BENCH_FIRST_ACCOUNT_FILE")
    endif()
    crux_bench_first_account_check(hash)
    set(file "$ENV{CRUX_BENCH_PASSWORD_FILE}")
    set(asm_file "${CMAKE_BINARY_DIR}/bench_first_account_password.S")
    crux_bench_first_account_asm(asm "${file}")
    # Written only when it changes, so that a rebuild does not touch the object.
    set(existing "")
    if(EXISTS "${asm_file}")
        file(READ "${asm_file}" existing)
    endif()
    if(NOT "${existing}" STREQUAL "${asm}")
        file(WRITE "${asm_file}" "${asm}")
    endif()
    target_sources(app PRIVATE "${asm_file}")
    # A changed file makes the object build again, and makes CMake run again, which checks the file again and writes the
    # manifest with the new hash.
    set_source_files_properties("${asm_file}" PROPERTIES OBJECT_DEPENDS "${file}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    set_property(GLOBAL PROPERTY CRUX_BENCH_FIRST_ACCOUNT_SHA256 "${hash}")
    message(STATUS "Bench first account: password file checked, sha256 ${hash}")
endfunction()

function(crux_bench_first_account_sha256 out)
    get_property(hash GLOBAL PROPERTY CRUX_BENCH_FIRST_ACCOUNT_SHA256)
    set(${out} "${hash}" PARENT_SCOPE)
endfunction()
