cmake_minimum_required(VERSION 3.28)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_system_tests LANGUAGES C)
target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE ../../../components/system/src ../../../components/system/ports/zephyr)
target_link_libraries(app PRIVATE crux::system)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
foreach(symbol crux_watchdog_zephyr_device crux_watchdog_zephyr_running
        z_impl_hwinfo_get_reset_cause z_impl_hwinfo_get_supported_reset_cause
        z_impl_hwinfo_clear_reset_cause sys_reboot)
    target_link_options(app PUBLIC -Wl,--wrap=${symbol})
endforeach()
