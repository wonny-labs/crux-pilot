add_library(crux_base INTERFACE)
add_library(crux::base ALIAS crux_base)
target_include_directories(crux_base INTERFACE include)
