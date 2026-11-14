cmake_minimum_required(VERSION 3.20.0)
# A native_sim application for the fail-closed TCP initial sequence number (row C11, docs/rt1060-isn-fail-closed.md).
# It is built on a Zephyr tree that either has the patch (patches/zephyr/zephyr-isn-fail-closed.patch) or does not, and
# the same source gives the red run on the pinned tree and the green run on the patched one. The provider of the hash
# is TF-PSA-Crypto through Zephyr's Mbed TLS module, as in tests/psa_entropy/zephyr.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_isn_fail_closed C)
target_sources(app PRIVATE src/main.c src/test_entropy.c)
# The test builds and reads TCP packets with the stack's own helpers, which are internal headers of subsys/net/ip,
# as Zephyr's tests/net/tcp does.
target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/net/ip)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
