set(CRUX_GPIO_PROVIDER host CACHE STRING "GPIO provider for this composition")
if(NOT CRUX_GPIO_PROVIDER STREQUAL "host")
    message(FATAL_ERROR "Crux GPIO unsupported provider: ${CRUX_GPIO_PROVIDER}; expected host")
endif()

add_library(crux_gpio_api INTERFACE)
target_include_directories(crux_gpio_api INTERFACE include)
target_link_libraries(crux_gpio_api INTERFACE crux::base)

add_library(crux_gpio_host STATIC src/gpio.c ports/host/gpio_host.c)
target_include_directories(crux_gpio_host PRIVATE src)
target_link_libraries(crux_gpio_host PRIVATE crux_gpio_api)
target_compile_options(crux_gpio_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_property(TARGET crux_gpio_host PROPERTY INTERFACE_CRUX_GPIO_PROVIDER host)
set_property(TARGET crux_gpio_host APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_GPIO_PROVIDER)

add_library(crux_gpio INTERFACE)
add_library(crux::gpio ALIAS crux_gpio)
target_link_libraries(crux_gpio INTERFACE crux_gpio_api crux_gpio_host)
