add_executable(crux-controller-host main.c)
target_link_libraries(crux-controller-host PRIVATE crux_controller_core crux::time)
target_compile_definitions(crux-controller-host PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux-controller-host PRIVATE -Wall -Wextra -Wpedantic -Werror)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME Controller.HostScenario
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check-controller.py
                $<TARGET_FILE:crux-controller-host>
                --log ${CMAKE_CURRENT_BINARY_DIR}/controller.log)
    set_tests_properties(Controller.HostScenario PROPERTIES TIMEOUT 15)
endif()
