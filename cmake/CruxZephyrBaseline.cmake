# Crux states the Zephyr release it was verified on and says so when a build uses another.
#
# The west manifest of a product can pick any Zephyr, and west accepts it without
# a warning. This gate makes the build say so. It compares the release numbers
# only: a fork that adds an EXTRAVERSION suffix stays quiet, because its
# KERNEL_VERSION_MAJOR, KERNEL_VERSION_MINOR and KERNEL_PATCHLEVEL are unchanged.
#
# Another release, or a version that cannot be read, gives a warning and the build
# continues. Crux evidence does not apply to that build. Set
# -DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON to stop such a build instead, as a release should.
#
# The Crux Zephyr module runs this while CONFIG_CRUX_PILOT_MODULE is on, so an
# application that turns the module off also turns the gate off.
#
# Keep CRUX_ZEPHYR_BASELINE equal to the "# vX.Y.Z" comment on the zephyr revision
# in west.yml. tests/build/test_zephyr_baseline.py checks that.

set(CRUX_ZEPHYR_BASELINE "4.4.2")

# Zephyr's cmake/modules/version.cmake sets the three variables before module
# CMakeLists.txt files run.
function(crux_check_zephyr_baseline)
    if(DEFINED KERNEL_VERSION_MAJOR AND DEFINED KERNEL_VERSION_MINOR AND DEFINED KERNEL_PATCHLEVEL)
        set(used "${KERNEL_VERSION_MAJOR}.${KERNEL_VERSION_MINOR}.${KERNEL_PATCHLEVEL}")
        if(used STREQUAL CRUX_ZEPHYR_BASELINE)
            return()
        endif()
        set(finding "Crux was verified on Zephyr ${CRUX_ZEPHYR_BASELINE}, but this build uses Zephyr ${used}.")
    else()
        string(CONCAT finding
            "Crux cannot read the Zephyr version: KERNEL_VERSION_MAJOR, KERNEL_VERSION_MINOR "
            "or KERNEL_PATCHLEVEL is not set. Crux was verified on Zephyr ${CRUX_ZEPHYR_BASELINE}.")
    endif()
    if(CRUX_REQUIRE_VERIFIED_ZEPHYR)
        message(FATAL_ERROR
            "${finding} CRUX_REQUIRE_VERIFIED_ZEPHYR is ON, so the build stops. Use the Zephyr "
            "revision that Crux pins, or clear the option to build without Crux evidence.")
    endif()
    message(WARNING
        "${finding} Crux evidence does not apply to this build. Set "
        "-DCRUX_REQUIRE_VERIFIED_ZEPHYR=ON to stop such a build.")
endfunction()
