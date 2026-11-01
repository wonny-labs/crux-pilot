add_executable(crux_i2c_tests src/i2c_test.c)
target_link_libraries(crux_i2c_tests PRIVATE crux::i2c)
target_compile_options(crux_i2c_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME I2c.HostContract COMMAND crux_i2c_tests)

# The portable layer is checked against a test-only provider that can inject failures. It is
# built from the portable source alone so the shipped loopback provider is not linked.
add_executable(crux_i2c_fault_tests src/i2c_fault_test.c ../../components/i2c/src/i2c.c)
target_include_directories(crux_i2c_fault_tests PRIVATE ../../components/i2c/src)
target_link_libraries(crux_i2c_fault_tests PRIVATE crux_i2c_api)
target_compile_options(crux_i2c_fault_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME I2c.PortableFaults COMMAND crux_i2c_fault_tests)

# The native-result mapping has no Zephyr dependency, so it is checked on the Host.
add_executable(crux_i2c_status_tests src/i2c_status_test.c
    ../../components/i2c/ports/zephyr/i2c_status.c)
target_include_directories(crux_i2c_status_tests PRIVATE ../../components/i2c/ports/zephyr)
target_link_libraries(crux_i2c_status_tests PRIVATE crux::base)
target_compile_options(crux_i2c_status_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME I2c.NativeStatus COMMAND crux_i2c_status_tests)

# A portable consumer (a TMP117 temperature sensor) uses only the public header. Its tests run
# against a test-only provider that models the sensor and injects failures.
add_library(i2c_tmp117_consumer_portable OBJECT consumer/tmp117.c)
target_include_directories(i2c_tmp117_consumer_portable PUBLIC consumer)
target_link_libraries(i2c_tmp117_consumer_portable PRIVATE crux::i2c)
target_compile_options(i2c_tmp117_consumer_portable PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(i2c_tmp117_consumer_portable PRIVATE c_std_17)
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
    CALL crux_assert_portable_consumer i2c_tmp117_consumer_portable)

add_executable(crux_i2c_tmp117_tests consumer/tmp117_test.c ../../components/i2c/src/i2c.c
    $<TARGET_OBJECTS:i2c_tmp117_consumer_portable>)
target_include_directories(crux_i2c_tmp117_tests PRIVATE consumer ../../components/i2c/src)
target_link_libraries(crux_i2c_tmp117_tests PRIVATE crux_i2c_api)
target_compile_options(crux_i2c_tmp117_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME I2c.Tmp117Consumer COMMAND crux_i2c_tmp117_tests)
