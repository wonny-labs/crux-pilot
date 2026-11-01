add_library(gpio_consumer_portable OBJECT consumer.c)
target_link_libraries(gpio_consumer_portable PRIVATE crux::gpio)
target_compile_options(gpio_consumer_portable PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(gpio_consumer_portable PRIVATE c_std_17)
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
    CALL crux_assert_portable_consumer gpio_consumer_portable)

add_executable(crux_gpio_host_tests host_test.c $<TARGET_OBJECTS:gpio_consumer_portable>)
target_include_directories(crux_gpio_host_tests PRIVATE .)
target_link_libraries(crux_gpio_host_tests PRIVATE crux::gpio)
target_compile_options(crux_gpio_host_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(crux_gpio_host_tests PRIVATE c_std_17)
add_test(NAME GPIO.Host COMMAND crux_gpio_host_tests)

# The native-result mapping has no Zephyr dependency, so it is checked on the Host.
add_executable(crux_gpio_status_tests status_test.c
    ../../components/gpio/ports/zephyr/gpio_status.c)
target_include_directories(crux_gpio_status_tests PRIVATE
    ../../components/gpio/ports/zephyr)
target_link_libraries(crux_gpio_status_tests PRIVATE crux::base)
target_compile_options(crux_gpio_status_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(crux_gpio_status_tests PRIVATE c_std_17)
add_test(NAME GPIO.NativeStatus COMMAND crux_gpio_status_tests)
