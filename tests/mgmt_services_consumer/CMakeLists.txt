cmake_minimum_required(VERSION 3.28)
project(crux_mgmt_services_independent LANGUAGES C CXX)
if(NOT CRUX_SOURCE_DIR)
    get_filename_component(CRUX_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
foreach(capability IN ITEMS SYNC SYSTEM CONFIGURATION ACCESS CONFIG_SERVICE MGMT_STATUS)
    set(CRUX_BUILD_${capability} ON CACHE BOOL "Required external capability")
endforeach()
add_subdirectory("${CRUX_SOURCE_DIR}" crux)
include("${CRUX_SOURCE_DIR}/cmake/CruxConsumerBoundary.cmake")
include("${CRUX_SOURCE_DIR}/cmake/CruxAccessBoundary.cmake")
crux_assert_portable_usage(crux::config_service crux::mgmt_status)
crux_assert_access_adapter_unreachable(crux::config_service crux::mgmt_status)

# A fake must not exist in a build that a product could use.
foreach(fake IN ITEMS crux_mgmt_status_fake_sources crux_config_service_fake_events
        crux_configuration_fake_provider crux_access_test_composition)
    if(TARGET ${fake})
        message(FATAL_ERROR "${fake} must not exist outside Crux's own tests")
    endif()
endforeach()

add_library(mgmt_services_consumer_c OBJECT consumer.c)
add_library(mgmt_services_consumer_cpp OBJECT consumer.cpp)
foreach(consumer IN ITEMS mgmt_services_consumer_c mgmt_services_consumer_cpp)
    target_link_libraries(${consumer} PRIVATE crux::config_service crux::mgmt_status)
    target_compile_options(${consumer} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    crux_assert_portable_consumer(${consumer})
    crux_assert_access_consumer(${consumer})
endforeach()
set_target_properties(mgmt_services_consumer_c PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
set_target_properties(mgmt_services_consumer_cpp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
