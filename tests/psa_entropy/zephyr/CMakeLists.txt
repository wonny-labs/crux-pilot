cmake_minimum_required(VERSION 3.20.0)
# A native_sim application whose entropy device can be made to fail, to show what the Mbed TLS glue
# does with a failed read. The provider is TF-PSA-Crypto through Zephyr's Mbed TLS module, a project
# of the crux-secure-management west group. See docs/psa-crypto-qualification.md, finding 3.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_psa_entropy C)
target_sources(app PRIVATE src/main.c src/test_entropy.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
