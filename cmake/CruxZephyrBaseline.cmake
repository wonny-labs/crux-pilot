# Crux states the Zephyr release it was verified on and refuses another one.
#
# The west manifest of a product can pick any Zephyr, and west accepts it without
# a warning. This gate makes the build say so. It compares the release numbers
# only: a fork that adds an EXTRAVERSION suffix still passes, because its
# KERNEL_VERSION_MAJOR, KERNEL_VERSION_MINOR and KERNEL_PATCHLEVEL are unchanged.
#
# Set -DCRUX_ALLOW_UNVERIFIED_ZEPHYR=ON to build on another release anyway. Crux
# then warns and its verification evidence does not apply to that build.
#
# Keep CRUX_ZEPHYR_BASELINE equal to the "# vX.Y.Z" comment on the zephyr revision
# in west.yml. tests/build/test_zephyr_baseline.py checks that.

set(CRUX_ZEPHYR_BASELINE "4.4.2")

# Zephyr's cmake/modules/version.cmake sets the three variables before module
# CMakeLists.txt files run.
function(crux_check_zephyr_baseline)
    if(NOT DEFINED KERNEL_VERSION_MAJOR OR NOT DEFINED KERNEL_VERSION_MINOR
            OR NOT DEFINED KERNEL_PATCHLEVEL)
        message(FATAL_ERROR
            "Crux cannot read the Zephyr version: KERNEL_VERSION_MAJOR, "
            "KERNEL_VERSION_MINOR or KERNEL_PATCHLEVEL is not set.")
    endif()
    set(used "${KERNEL_VERSION_MAJOR}.${KERNEL_VERSION_MINOR}.${KERNEL_PATCHLEVEL}")
    if(used STREQUAL CRUX_ZEPHYR_BASELINE)
        return()
    endif()
    if(CRUX_ALLOW_UNVERIFIED_ZEPHYR)
        message(WARNING
            "Crux was verified on Zephyr ${CRUX_ZEPHYR_BASELINE}, but this build uses "
            "Zephyr ${used}. Crux evidence does not apply to this build.")
    else()
        message(FATAL_ERROR
            "Crux was verified on Zephyr ${CRUX_ZEPHYR_BASELINE}, but this build uses "
            "Zephyr ${used}. Pin the Zephyr revision that Crux pins, or set "
            "-DCRUX_ALLOW_UNVERIFIED_ZEPHYR=ON to build on it without Crux evidence.")
    endif()
endfunction()
