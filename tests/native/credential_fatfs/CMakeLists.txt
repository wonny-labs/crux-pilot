cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_credential_fatfs C)
# The test composition, wired directly: the store has no Zephyr composition yet, and these sources
# are portable. The protection is still the test-only fake.
target_sources(app PRIVATE
    main.c
    ${CRUX_ROOT}/tests/credential/conformance.c
    ${CRUX_ROOT}/tests/credential/credential_test.c
    ${CRUX_ROOT}/tests/credential/fake_ports.c
    ${CRUX_ROOT}/components/credential/src/credential_image.c
    ${CRUX_ROOT}/components/credential/src/credential_store.c)
target_link_libraries(app PRIVATE crux::file crux::sync)
target_include_directories(app PRIVATE
    ${CRUX_ROOT}/components/credential/include
    ${CRUX_ROOT}/components/credential/adapter
    ${CRUX_ROOT}/components/credential/src
    ${CRUX_ROOT}/components/file/ports/zephyr
    ${CRUX_ROOT}/tests/credential)
target_compile_definitions(app PRIVATE
    CRUX_CREDENTIAL_TESTING=1 CRUX_CREDENTIAL_TEST_COMPOSITION=1)
