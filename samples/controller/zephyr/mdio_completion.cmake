# Reference-only correction; never modify the pinned dependency checkout.
set(crux_mdio_repo "${CMAKE_CURRENT_LIST_DIR}/../../..")
get_filename_component(crux_mdio_repo "${crux_mdio_repo}" ABSOLUTE)
set(crux_mdio_original "${ZEPHYR_BASE}/drivers/ethernet/mdio/mdio_nxp_enet.c")
set(crux_mdio_generated "${CMAKE_CURRENT_BINARY_DIR}/crux-mdio/mdio_nxp_enet.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${crux_mdio_original}"
    "${crux_mdio_repo}/patches/zephyr/nxp-enet-mdio-completion.patch"
    "${crux_mdio_repo}/scripts/prepare-nxp-mdio.py")
execute_process(COMMAND ${PYTHON_EXECUTABLE}
    "${crux_mdio_repo}/scripts/prepare-nxp-mdio.py"
    "${ZEPHYR_BASE}" "${crux_mdio_generated}"
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT TARGET drivers__ethernet)
    message(FATAL_ERROR "Pinned Ethernet library is unavailable")
endif()
get_target_property(crux_mdio_sources drivers__ethernet SOURCES)
get_target_property(crux_mdio_source_dir drivers__ethernet SOURCE_DIR)
set(crux_mdio_remaining "")
set(crux_mdio_replaced 0)
foreach(crux_mdio_source IN LISTS crux_mdio_sources)
    get_filename_component(crux_mdio_absolute "${crux_mdio_source}" ABSOLUTE
        BASE_DIR "${crux_mdio_source_dir}")
    if(crux_mdio_absolute STREQUAL crux_mdio_original)
        math(EXPR crux_mdio_replaced "${crux_mdio_replaced} + 1")
    else()
        list(APPEND crux_mdio_remaining "${crux_mdio_source}")
    endif()
endforeach()
if(NOT crux_mdio_replaced EQUAL 1)
    message(FATAL_ERROR "Expected exactly one pinned MDIO source, found ${crux_mdio_replaced}")
endif()
set_property(TARGET drivers__ethernet PROPERTY SOURCES "${crux_mdio_remaining}")
target_sources(drivers__ethernet PRIVATE "${crux_mdio_generated}")

set_source_files_properties("${crux_mdio_generated}" TARGET_DIRECTORY drivers__ethernet
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
if(CONFIG_CRUX_M3_MDIO_FAILURE_TRACE)
    target_include_directories(drivers__ethernet PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
endif()
