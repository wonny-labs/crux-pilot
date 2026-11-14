# The static half of the per-service priority checks (ADR-0002 P3). The kernel half, which reads the
# priorities back on native_sim, is tests/service_priority/zephyr.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ServicePriority.Placement
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_placement.py --root ${PROJECT_SOURCE_DIR})
add_test(NAME ServicePriority.PlacementCheckCatchesDefects
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_check_placement.py)
set_tests_properties(ServicePriority.Placement ServicePriority.PlacementCheckCatchesDefects
    PROPERTIES TIMEOUT 30)
