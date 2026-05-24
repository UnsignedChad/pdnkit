find_package(Catch2 3 REQUIRED)
include(Catch)

add_executable(pdnkit_tests
    smoke_test.cpp
    sexpr_test.cpp
    model_test.cpp
    parser_test.cpp
    camera_test.cpp
)

target_link_libraries(pdnkit_tests PRIVATE
    pdnkit_core
    Catch2::Catch2WithMain
)

target_compile_options(pdnkit_tests PRIVATE
    -Wall -Wextra -Wpedantic
)

catch_discover_tests(pdnkit_tests)
