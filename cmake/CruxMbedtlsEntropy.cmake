# Fail-closed entropy for Zephyr's Mbed TLS glue. Include this after find_package(Zephyr) and call
# crux_mbedtls_fail_closed_entropy() in an image whose PSA random source is the legacy RNG over an
# entropy driver (CONFIG_MBEDTLS_PSA_DRIVER_GET_ENTROPY), as on RT1060-EVKB.
#
# Zephyr's modules/mbedtls/zephyr_entropy.c answers a failed entropy read with sys_rand_get() and still
# reports full entropy, and on RT1060-EVKB sys_rand_get() is not cryptographically secure. This
# replaces that one source with a build-local copy from scripts/prepare-mbedtls-entropy.py, which
# checks the original's hash, so that a failed read fails PSA. It never modifies the Zephyr checkout,
# and a Zephyr change to the file fails configuration. See patches/zephyr/README.md.
get_filename_component(CRUX_MBEDTLS_ENTROPY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(crux_mbedtls_fail_closed_entropy)
    if(NOT CONFIG_MBEDTLS_PSA_DRIVER_GET_ENTROPY)
        message(FATAL_ERROR "The correction is for the entropy driver route, "
                            "CONFIG_MBEDTLS_PSA_DRIVER_GET_ENTROPY, which this image does not use")
    endif()
    set(root "${CRUX_MBEDTLS_ENTROPY_ROOT}")
    set(original "${ZEPHYR_BASE}/modules/mbedtls/zephyr_entropy.c")
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/crux-mbedtls/zephyr_entropy.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${original}"
        "${root}/patches/zephyr/mbedtls-entropy-fail-closed.patch"
        "${root}/scripts/prepare-mbedtls-entropy.py")
    execute_process(COMMAND ${PYTHON_EXECUTABLE}
        "${root}/scripts/prepare-mbedtls-entropy.py" "${ZEPHYR_BASE}" "${generated}"
        COMMAND_ERROR_IS_FATAL ANY)

    # The one library that compiles the original source.
    get_property(targets DIRECTORY "${ZEPHYR_BASE}/modules/mbedtls" PROPERTY BUILDSYSTEM_TARGETS)
    set(owner "")
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()
        get_target_property(sources ${target} SOURCES)
        get_target_property(source_dir ${target} SOURCE_DIR)
        set(remaining "")
        set(found 0)
        foreach(source IN LISTS sources)
            get_filename_component(absolute "${source}" ABSOLUTE BASE_DIR "${source_dir}")
            if(absolute STREQUAL original)
                math(EXPR found "${found} + 1")
            else()
                list(APPEND remaining "${source}")
            endif()
        endforeach()
        if(found)
            if(NOT found EQUAL 1 OR owner)
                message(FATAL_ERROR "Expected exactly one library compiling ${original}")
            endif()
            set(owner ${target})
            set(owner_sources "${remaining}")
        endif()
    endforeach()
    if(NOT owner)
        message(FATAL_ERROR "No library compiles ${original}, so the correction cannot apply")
    endif()
    set_property(TARGET ${owner} PROPERTY SOURCES "${owner_sources}")
    target_sources(${owner} PRIVATE "${generated}")
endfunction()
