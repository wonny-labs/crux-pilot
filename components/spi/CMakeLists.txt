set(CRUX_SPI_PROVIDER host CACHE STRING "SPI provider for this composition")
if(NOT CRUX_SPI_PROVIDER STREQUAL "host")
    message(FATAL_ERROR "Crux SPI unsupported provider: ${CRUX_SPI_PROVIDER}; expected host")
endif()

add_library(crux_spi_api INTERFACE)
target_include_directories(crux_spi_api INTERFACE include)
target_link_libraries(crux_spi_api INTERFACE crux::base)

add_library(crux_spi_host STATIC src/spi.c host/spi_port_host.c)
target_link_libraries(crux_spi_host PRIVATE crux_spi_api)
target_include_directories(crux_spi_host PRIVATE src)
target_compile_options(crux_spi_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_property(TARGET crux_spi_host PROPERTY INTERFACE_CRUX_SPI_PROVIDER host)
set_property(TARGET crux_spi_host APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_SPI_PROVIDER)

add_library(crux_spi INTERFACE)
add_library(crux::spi ALIAS crux_spi)
target_link_libraries(crux_spi INTERFACE crux_spi_api crux_spi_host)
