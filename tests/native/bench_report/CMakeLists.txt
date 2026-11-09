# SPDX-License-Identifier: Apache-2.0
# The kernel side of the RT1060-EVKB bench report (row C2), built on native_sim: the thread table, the heap line, the
# thread that watches an activity counter, and the lines it prints through the same deferred log path as the bench
# image. The numbers a thread's stack gives on native_sim mean nothing (a thread runs on a host stack), so this
# test checks the shape, the bounds and the timing of the report, and the bench image's numbers come from a board.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_native_bench_report LANGUAGES C)
get_filename_component(report_dir "${CMAKE_CURRENT_LIST_DIR}/../../zephyr/management_rt1060_compile/report" ABSOLUTE)
target_sources(app PRIVATE src/main.c ${report_dir}/bench_report_core.c ${report_dir}/bench_report_format.c
    ${report_dir}/bench_report_policy.c)
target_include_directories(app PRIVATE ${report_dir})
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
