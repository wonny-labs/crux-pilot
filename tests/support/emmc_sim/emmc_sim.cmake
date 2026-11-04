# SPDX-License-Identifier: Apache-2.0
set(CRUX_EMMC_SIM_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Adds the simulator to a test application. Host file access runs in the native simulator runner,
# outside the Zephyr image, as in Zephyr's flash simulator.
function(crux_emmc_sim_add target)
  target_sources(${target} PRIVATE "${CRUX_EMMC_SIM_DIR}/emmc_sim.c")
  target_include_directories(${target} PRIVATE "${CRUX_EMMC_SIM_DIR}")
  target_sources(native_simulator INTERFACE "${CRUX_EMMC_SIM_DIR}/emmc_sim_bottom.c")
endfunction()
