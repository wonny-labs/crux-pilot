add_executable(crux-networking-host main.c ../client.c)
target_include_directories(crux-networking-host PRIVATE ..)
target_link_libraries(crux-networking-host PRIVATE crux::sockets crux::netif)
target_compile_options(crux-networking-host PRIVATE -Wall -Wextra -Wpedantic -Werror)
