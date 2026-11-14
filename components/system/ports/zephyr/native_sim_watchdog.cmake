# The simulated System watchdog is a native_sim test setting. Kconfig declares no board
# dependency for it, because an unmet dependency only warns and a CMake check cannot see a
# hidden symbol, so this check refuses the symbol on every other board. system_binding.c
# repeats the refusal as an #error.
if(CONFIG_CRUX_SYSTEM_NATIVE_SIM_WATCHDOG AND NOT CONFIG_BOARD_NATIVE_SIM)
    message(FATAL_ERROR "CONFIG_CRUX_SYSTEM_NATIVE_SIM_WATCHDOG is a native_sim test setting "
        "and is refused on any other board; no simulated watchdog is bound there")
endif()
