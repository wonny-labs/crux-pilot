add_library(crux_configuration STATIC src/configuration.c src/configuration_text.c)
add_library(crux::configuration ALIAS crux_configuration)
target_include_directories(crux_configuration PUBLIC include PRIVATE src)
target_compile_options(crux_configuration PRIVATE -Wall -Wextra -Wpedantic -Werror)

# A build that changes the longest string sets this, and every user of the header then agrees.
set(CRUX_CONFIG_STRING_MAX "" CACHE STRING "Longest Configuration string in bytes; empty keeps the default of 128")
if(NOT CRUX_CONFIG_STRING_MAX STREQUAL "")
    target_compile_definitions(crux_configuration PUBLIC CRUX_CONFIG_STRING_MAX=${CRUX_CONFIG_STRING_MAX})
endif()

# The port a composition selects while it has no provider, as #16 has not decided one for a target.
# Load and save then return CRUX_CONFIG_ERR_NOT_SUPPORTED, instead of the image failing to link. It
# is objects, so that a final link always takes it, and a second provider is a duplicate symbol.
add_library(crux_configuration_no_provider OBJECT src/configuration_no_provider.c)
target_include_directories(crux_configuration_no_provider PRIVATE src)
target_link_libraries(crux_configuration_no_provider PUBLIC crux_configuration)
target_compile_options(crux_configuration_no_provider PRIVATE -Wall -Wextra -Wpedantic -Werror)
