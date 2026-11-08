# The native socket offload reports a peer's FIN as POLLIN alone, where Zephyr's own TCP adds POLLHUP
# (sock_is_eof in sockets_inet.c). On the Mbed TLS profile that FIN never reaches poll, and a client that
# closes without close_notify leaves the server thread spinning inside zsock_poll. This test image builds a
# copy of the driver's host side that also asks epoll for EPOLLRDHUP and reports it as POLLHUP, which is
# what a target's stack does. The West checkout is not edited. --no-rdhup turns it off, as a control.
# See docs/https-composition.md.
set(nsos_source "${ZEPHYR_BASE}/drivers/net/nsos_adapt.c")
# The driver at Zephyr v4.4.2. Another hash means the base moved, so review the edit again.
set(nsos_sha256 c4fe7d2f83f151ab2598dcd3c86d6e5e3646347bcd52c1673622954c28017365)
file(SHA256 "${nsos_source}" nsos_actual)
if(NOT nsos_actual STREQUAL nsos_sha256)
  message(FATAL_ERROR "${nsos_source} is not the reviewed v4.4.2 file: ${nsos_actual}")
endif()
file(READ "${nsos_source}" nsos_text)
# Each anchor must appear exactly once. The texts hold semicolons, so they are not a CMake list.
function(crux_nsos_edit anchor replacement)
  string(FIND "${nsos_text}" "${anchor}" first)
  string(FIND "${nsos_text}" "${anchor}" last REVERSE)
  if(first EQUAL -1 OR NOT first EQUAL last)
    message(FATAL_ERROR "nsos_adapt.c: the anchor must appear once: ${anchor}")
  endif()
  string(REPLACE "${anchor}" "${replacement}" edited "${nsos_text}")
  set(nsos_text "${edited}" PARENT_SCOPE)
endfunction()
crux_nsos_edit("#include \"nsi_tasks.h\"\n"
  "#include \"nsi_tasks.h\"\n\nstatic int crux_nsos_rdhup = 1;\nvoid crux_nsos_set_rdhup(int on);\nvoid crux_nsos_set_rdhup(int on)\n{\n\tcrux_nsos_rdhup = on;\n}\n")
crux_nsos_edit("\tMAP_POLL_EPOLL(POLLHUP, EPOLLHUP);\n"
  "\tMAP_POLL_EPOLL(POLLHUP, EPOLLHUP);\n\tif (crux_nsos_rdhup && (events_to & EPOLLIN)) {\n\t\tevents_to |= EPOLLRDHUP;\n\t}\n")
crux_nsos_edit("\tMAP_POLL_EPOLL(EPOLLHUP, POLLHUP);\n"
  "\tMAP_POLL_EPOLL(EPOLLHUP, POLLHUP);\n\tMAP_POLL_EPOLL(EPOLLRDHUP, POLLHUP);\n")
set(nsos_copy "${CMAKE_CURRENT_BINARY_DIR}/nsos_adapt_rdhup.c")
file(WRITE "${nsos_copy}" "${nsos_text}")

get_target_property(nsos_sources native_simulator INTERFACE_SOURCES)
list(FIND nsos_sources "${ZEPHYR_BASE}/drivers/net/nsos_adapt.c" nsos_index)
if(nsos_index EQUAL -1)
  message(FATAL_ERROR "native_simulator does not list nsos_adapt.c: ${nsos_sources}")
endif()
list(REMOVE_AT nsos_sources ${nsos_index})
list(APPEND nsos_sources "${nsos_copy}")
set_property(TARGET native_simulator PROPERTY INTERFACE_SOURCES "${nsos_sources}")
# The copy includes the driver's own headers, which sit beside the original.
target_compile_options(native_simulator INTERFACE "-I${ZEPHYR_BASE}/drivers/net")
