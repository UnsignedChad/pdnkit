find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(pdnkit_tests
    smoke_test.cpp
    sexpr_test.cpp
    model_test.cpp
    parser_test.cpp
    camera_test.cpp
    zone_mesher_test.cpp
    segment_mesher_test.cpp
    via_mesher_test.cpp
    hit_test.cpp
    ir_mesher_test.cpp
    ir_solver_test.cpp
    ir_result_mesh_test.cpp
    e2e_test.cpp
)

target_compile_definitions(pdnkit_tests PRIVATE
    PDNKIT_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_link_libraries(pdnkit_tests PRIVATE
    pdnkit_core
    Catch2::Catch2WithMain
)

target_compile_options(pdnkit_tests PRIVATE
    -Wall -Wextra -Wpedantic
)

catch_discover_tests(pdnkit_tests)
