# The fake sources, which an adapter's tests can link too. They exist only when Crux builds its own
# tests, and no public target or provider setting can select them.
add_library(crux_mgmt_status_fake_sources OBJECT fake_sources.c)
target_include_directories(crux_mgmt_status_fake_sources
    PUBLIC . ../../components/mgmt_status/src)
target_link_libraries(crux_mgmt_status_fake_sources PUBLIC crux_mgmt_status_api)
target_compile_options(crux_mgmt_status_fake_sources PRIVATE -Wall -Wextra -Wpedantic -Werror)

function(crux_mgmt_status_suite suite)
    add_executable(mgmt_status_${suite} ${suite}.c status_test.c)
    target_include_directories(mgmt_status_${suite} PRIVATE ../configuration)
    target_link_libraries(mgmt_status_${suite} PRIVATE crux_mgmt_status_core
        crux_mgmt_status_fake_sources crux_access_test_composition Threads::Threads)
    target_compile_definitions(mgmt_status_${suite} PRIVATE _POSIX_C_SOURCE=200809L)
    target_compile_options(mgmt_status_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
endfunction()

foreach(suite IN ITEMS items snapshot json)
    crux_mgmt_status_suite(${suite})
    add_test(NAME MgmtStatus.${suite} COMMAND mgmt_status_${suite})
    set_tests_properties(MgmtStatus.${suite} PROPERTIES TIMEOUT 60)
endforeach()

# Holds the service's output to the merged specification.
crux_mgmt_status_suite(json_fixture)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME MgmtStatus.openapi_schema
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check_openapi_status.py
        --fixture $<TARGET_FILE:mgmt_status_json_fixture>
        --spec ${PROJECT_SOURCE_DIR}/web/api/openapi.yaml)
set_tests_properties(MgmtStatus.openapi_schema PROPERTIES TIMEOUT 60)

# The thin providers over the real System, Time and Netif, with no Configuration provider.
if(CRUX_BUILD_TIME AND CRUX_TIME_PROVIDER STREQUAL "memory")
    add_executable(mgmt_status_real_sources real_sources.c status_test.c)
    target_include_directories(mgmt_status_real_sources PRIVATE . ../../components/mgmt_status/src)
    target_link_libraries(mgmt_status_real_sources PRIVATE crux_mgmt_status_core
        crux_mgmt_status_sources crux_mgmt_status_no_security crux_access_test_composition
        Threads::Threads)
    target_compile_definitions(mgmt_status_real_sources PRIVATE _POSIX_C_SOURCE=200809L)
    target_compile_options(mgmt_status_real_sources PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME MgmtStatus.real_sources COMMAND mgmt_status_real_sources)
    set_tests_properties(MgmtStatus.real_sources PROPERTIES TIMEOUT 60)
endif()

# Each thin provider over a stand-in for the contract it calls.
add_executable(mgmt_status_source_mapping source_mapping.c
    ../../components/mgmt_status/src/source_system.c
    ../../components/mgmt_status/src/source_time.c
    ../../components/mgmt_status/src/source_netif.c
    ../../components/mgmt_status/src/source_no_security.c)
target_include_directories(mgmt_status_source_mapping PRIVATE ../../components/mgmt_status/src
    ../../components/time/include ../../components/netif/include)
target_link_libraries(mgmt_status_source_mapping PRIVATE crux_mgmt_status_api)
target_compile_options(mgmt_status_source_mapping PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME MgmtStatus.source_mapping COMMAND mgmt_status_source_mapping)
set_tests_properties(MgmtStatus.source_mapping PROPERTIES TIMEOUT 60)

add_executable(mgmt_status_lock_faults lock_faults.c ../config_service/access_double.c)
target_link_libraries(mgmt_status_lock_faults PRIVATE crux_mgmt_status_core
    crux_mgmt_status_fake_sources crux::sync)
target_link_options(mgmt_status_lock_faults PRIVATE -Wl,--wrap=crux_mutex_lock
    -Wl,--wrap=crux_mutex_create)
target_compile_options(mgmt_status_lock_faults PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME MgmtStatus.lock_faults COMMAND mgmt_status_lock_faults)
set_tests_properties(MgmtStatus.lock_faults PROPERTIES TIMEOUT 60)

# Every public declaration must also compile for a C++ consumer.
add_library(mgmt_status_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(mgmt_status_cpp_headers PRIVATE crux_mgmt_status_api)
target_compile_options(mgmt_status_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)
