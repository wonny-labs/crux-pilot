# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(product_x)

# The domain is plain C in ../domain. The shell includes its headers by relative path, so the
# build adds no include directory.
target_sources(app PRIVATE src/main.c ../domain/product_domain.c)
target_link_libraries(app PRIVATE crux::time)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
