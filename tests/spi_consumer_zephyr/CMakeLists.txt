cmake_minimum_required(VERSION 3.20.0)
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_spi_flash_id_zephyr_test LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/flash_emulator.c
    ../spi/consumer/spi_flash_id.c
)
target_include_directories(app PRIVATE ../spi/consumer)
target_link_libraries(app PRIVATE crux::spi)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
