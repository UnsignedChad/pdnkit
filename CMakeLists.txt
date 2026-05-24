cmake_minimum_required(VERSION 3.25)
project(pdnkit
    VERSION 0.0.1
    DESCRIPTION "Open-source Power Integrity analysis for KiCad PCBs"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(Eigen3 REQUIRED)
find_package(spdlog REQUIRED)
find_package(CLI11 REQUIRED)

# Core static library: parsing, model, mesher, solver.
# Linked by both the GUI binary and the test binary.
add_library(pdnkit_core STATIC
    src/sexpr/SExpr.cpp
    src/parser/KicadPcbParser.cpp
    src/model/HitTest.cpp
    src/pi/IrMesher.cpp
    src/render/Camera2D.cpp
    src/render/ZoneMesher.cpp
    src/render/SegmentMesher.cpp
    src/render/ViaMesher.cpp
)
target_include_directories(pdnkit_core PUBLIC src)
target_include_directories(pdnkit_core PUBLIC third_party)
target_link_libraries(pdnkit_core PUBLIC
    Eigen3::Eigen
    spdlog::spdlog
)
target_compile_options(pdnkit_core PRIVATE -Wall -Wextra -Wpedantic)

# GUI executable.
add_executable(pdnkit
    src/main.cpp
    src/MainWindow.cpp
    src/PcbCanvas.cpp
    src/LayerPanel.cpp
)
target_link_libraries(pdnkit PRIVATE
    pdnkit_core
    Qt6::Widgets
    Qt6::OpenGLWidgets
    CLI11::CLI11
)
target_compile_options(pdnkit PRIVATE -Wall -Wextra -Wpedantic)

option(PDNKIT_BUILD_TESTS "Build pdnkit unit tests" ON)
if(PDNKIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
