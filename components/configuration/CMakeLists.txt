add_library(crux_configuration STATIC src/configuration.c)
add_library(crux::configuration ALIAS crux_configuration)
target_include_directories(crux_configuration PUBLIC include PRIVATE src)
target_compile_options(crux_configuration PRIVATE -Wall -Wextra -Wpedantic -Werror)
