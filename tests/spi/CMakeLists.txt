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

# A portable consumer (a serial NOR flash identification read) uses only the public header. Its
# tests run against a test-only provider that models the flash command and injects failures.
add_library(spi_flash_id_consumer_portable OBJECT consumer/spi_flash_id.c)
target_include_directories(spi_flash_id_consumer_portable PUBLIC consumer)
target_link_libraries(spi_flash_id_consumer_portable PRIVATE crux::spi)
target_compile_options(spi_flash_id_consumer_portable PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(spi_flash_id_consumer_portable PRIVATE c_std_17)
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
    CALL crux_assert_portable_consumer spi_flash_id_consumer_portable)

add_executable(crux_spi_flash_id_tests consumer/spi_flash_id_test.c ../../components/spi/src/spi.c
    $<TARGET_OBJECTS:spi_flash_id_consumer_portable>)
target_include_directories(crux_spi_flash_id_tests PRIVATE consumer ../../components/spi/src)
target_link_libraries(crux_spi_flash_id_tests PRIVATE crux_spi_api)
target_compile_options(crux_spi_flash_id_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Spi.FlashIdConsumer COMMAND crux_spi_flash_id_tests)
