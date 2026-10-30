add_executable(crux_i2c_tests src/i2c_test.c)
target_link_libraries(crux_i2c_tests PRIVATE crux::i2c)
target_compile_options(crux_i2c_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME I2c.HostContract COMMAND crux_i2c_tests)
