add_executable(crux_spi_tests src/spi_test.c)
target_link_libraries(crux_spi_tests PRIVATE crux::spi)
target_compile_options(crux_spi_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Spi.HostContract COMMAND crux_spi_tests)

# The portable layer is checked against a test-only provider that can inject failures. It is
# built from the portable source alone so the shipped loopback provider is not linked.
add_executable(crux_spi_fault_tests src/spi_fault_test.c ../../components/spi/src/spi.c)
target_include_directories(crux_spi_fault_tests PRIVATE ../../components/spi/src)
target_link_libraries(crux_spi_fault_tests PRIVATE crux_spi_api)
target_compile_options(crux_spi_fault_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Spi.PortableFaults COMMAND crux_spi_fault_tests)

# The native-result mapping has no Zephyr dependency, so it is checked on the Host.
add_executable(crux_spi_status_tests src/spi_status_test.c
    ../../components/spi/ports/zephyr/spi_status.c)
target_include_directories(crux_spi_status_tests PRIVATE ../../components/spi/ports/zephyr)
target_link_libraries(crux_spi_status_tests PRIVATE crux::base)
target_compile_options(crux_spi_status_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Spi.NativeStatus COMMAND crux_spi_status_tests)
