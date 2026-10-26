cmake_minimum_required(VERSION 3.28)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_messaging_resources C)
target_sources(app PRIVATE src/main.c)
# Retain the selected callable Queue and typed clock/sleep surface for static-size accounting.
# No application behaviour is executed or qualified by this fixture.
if(CONFIG_CRUX_QUEUE)
    target_link_options(app PUBLIC -Wl,--undefined=crux_queue_create -Wl,--undefined=crux_queue_send
        -Wl,--undefined=crux_queue_receive -Wl,--undefined=crux_queue_destroy)
endif()
if(CONFIG_CRUX_THREAD)
    target_link_options(app PUBLIC -Wl,--undefined=crux_time_get_monotonic
        -Wl,--undefined=crux_thread_sleep_until)
endif()
