# SPDX-License-Identifier: Apache-2.0
set(CRUX_EMMC_SIM_DIR "${CMAKE_CURRENT_LIST_DIR}")

# CMake warns "No SOURCES given to Zephyr library: drivers__sdhc", because the only host driver
# lives in the application. ALLOW_EMPTY cannot silence it from a test's CMake: Zephyr creates and
# checks that target inside find_package(Zephyr), so the target does not exist before that call and
# the warning has been given after it. The warning is harmless.

# Adds the simulator to a test application. Host file access runs in the native simulator runner,
# outside the Zephyr image, as in Zephyr's flash simulator.
function(crux_emmc_sim_add target)
  target_sources(${target} PRIVATE "${CRUX_EMMC_SIM_DIR}/emmc_sim.c")
  target_include_directories(${target} PRIVATE "${CRUX_EMMC_SIM_DIR}")
  target_sources(native_simulator INTERFACE "${CRUX_EMMC_SIM_DIR}/emmc_sim_bottom.c")
endfunction()
