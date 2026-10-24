get_filename_component(system_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(system_runtime STATIC "${CMAKE_CURRENT_LIST_DIR}/src/runtime.c" "${CMAKE_CURRENT_LIST_DIR}/src/consumer.cpp")
add_library(system_controller STATIC "${system_root}/samples/controller/system/controller_system.c"
    "${system_root}/samples/controller/core/health.c")
target_include_directories(system_controller PRIVATE "${system_root}/samples/controller/include")
foreach(consumer IN ITEMS system_runtime system_controller)
    target_link_libraries(${consumer} PRIVATE crux::system)
    target_compile_options(${consumer} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(${consumer} PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
    crux_assert_portable_consumer(${consumer})
endforeach()
