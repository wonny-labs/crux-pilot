set(CRUX_FILE_SERVICE_TEST_SOURCES service_test.c paths_permissions.c capacity_conflicts.c
    transfers_interrupted.c)
set(CRUX_FILE_SERVICE_SOURCE ../../components/file_service/src/file_service.c)

# The second run uses a name table too small for three uploads and a rename, which is the only way
# to fill it while the transfer pool leaves a File handle free.
foreach(variant IN ITEMS default small_table)
    add_executable(file_service_${variant} ${CRUX_FILE_SERVICE_TEST_SOURCES}
        ${CRUX_FILE_SERVICE_SOURCE})
    target_link_libraries(file_service_${variant} PRIVATE crux_file_service_api crux_file_fake
        crux::sync)
    target_include_directories(file_service_${variant} PRIVATE .
        ../../components/file_service/src)
    target_compile_definitions(file_service_${variant} PRIVATE _DEFAULT_SOURCE)
    target_compile_options(file_service_${variant} PRIVATE -Wall -Wextra -Wpedantic -Werror
        -UNDEBUG)
    add_test(NAME FileService.${variant} COMMAND file_service_${variant})
    set_tests_properties(FileService.${variant} PROPERTIES TIMEOUT 60)
endforeach()
target_compile_definitions(file_service_small_table PRIVATE CRUX_FILE_SERVICE_RESERVATIONS=4)

# The service on the real Access core, with its fake ports, instead of the double above.
add_executable(file_service_with_access with_access.c ${CRUX_FILE_SERVICE_SOURCE})
target_link_libraries(file_service_with_access PRIVATE crux_file_service_api
    crux_access_test_composition crux_file_fake)
target_include_directories(file_service_with_access PRIVATE ../../components/file_service/src)
target_compile_options(file_service_with_access PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME FileService.with_access COMMAND file_service_with_access)
set_tests_properties(FileService.with_access PROPERTIES TIMEOUT 60)
