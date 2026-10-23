cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_execution_runtime_firmware LANGUAGES C)

add_library(crux_runtime_fixture STATIC ../src/runtime_fixture.c)
target_include_directories(crux_runtime_fixture PUBLIC ../include)
target_link_libraries(crux_runtime_fixture PRIVATE crux::execution)
target_compile_features(crux_runtime_fixture PRIVATE c_std_17)
set_target_properties(crux_runtime_fixture PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(crux_runtime_fixture PRIVATE -Wall -Wextra -Werror)

target_sources(app PRIVATE main.c)
target_link_libraries(app PRIVATE crux_runtime_fixture)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
