set(CRUX_I2C_PROVIDER host CACHE STRING "I2C provider for this composition")
if(NOT CRUX_I2C_PROVIDER STREQUAL "host")
    message(FATAL_ERROR "Crux I2C unsupported provider: ${CRUX_I2C_PROVIDER}; expected host")
endif()

add_library(crux_i2c_api INTERFACE)
target_include_directories(crux_i2c_api INTERFACE include)
target_link_libraries(crux_i2c_api INTERFACE crux::base)

add_library(crux_i2c_host STATIC src/i2c.c host/i2c_port_host.c)
target_link_libraries(crux_i2c_host PRIVATE crux_i2c_api)
target_include_directories(crux_i2c_host PRIVATE src)
target_compile_options(crux_i2c_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_property(TARGET crux_i2c_host PROPERTY INTERFACE_CRUX_I2C_PROVIDER host)
set_property(TARGET crux_i2c_host APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_I2C_PROVIDER)

add_library(crux_i2c INTERFACE)
add_library(crux::i2c ALIAS crux_i2c)
target_link_libraries(crux_i2c INTERFACE crux_i2c_api crux_i2c_host)
