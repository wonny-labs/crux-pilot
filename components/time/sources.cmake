# Shared source list for the host, test, and Zephyr compositions.
set(CRUX_TIME_COMMON_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/src/time.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/time_types.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/time_clock.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/time_calendar.c"
)
