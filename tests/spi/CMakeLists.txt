add_executable(crux_spi_tests src/spi_test.c)
target_link_libraries(crux_spi_tests PRIVATE crux::spi)
target_compile_options(crux_spi_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Spi.HostContract COMMAND crux_spi_tests)
