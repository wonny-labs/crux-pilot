# PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
# Exists so the team can review the proposed image-state contract in running code.
#
# A standalone project. The root CMakeLists.txt does not add this directory, and this file refuses
# to be added from another project, so a product cannot pick it up by accident. It defines no
# library target, no usage target, no install rule and no export. Run it with:
#
#   cmake -S tests/prototypes/m9_image_state -B build/m9-image-state-prototype
#   cmake --build build/m9-image-state-prototype
#   ctest --test-dir build/m9-image-state-prototype --output-on-failure
cmake_minimum_required(VERSION 3.28)

project(crux_image_state_prototype LANGUAGES C)

if(NOT PROJECT_IS_TOP_LEVEL)
    message(FATAL_ERROR "tests/prototypes/m9_image_state is a standalone prototype and must not be "
                        "added to another project (not adopted, not for merge)")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)

add_executable(image_state_proto_tests
    test_main.c
    test_pure.c
    test_provider.c
    test_scenarios.c
    test_codec.c
    image_state_core.c
    image_state_provider.c
    image_state_fake.c
    attempt_codec.c
)
target_compile_options(image_state_proto_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME ImageStatePrototype.Host COMMAND image_state_proto_tests)
set_tests_properties(ImageStatePrototype.Host PROPERTIES TIMEOUT 30)
