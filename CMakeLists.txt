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

# Optional: CHOLMOD (SuiteSparse) for the supernodal Cholesky solver. Typical
# 5-20x speedup over Eigen::SimplicialLLT for boards with thousands of nodes.
# Falls back to pure-Eigen if missing.
find_library(CHOLMOD_LIBRARY NAMES cholmod)
find_path(CHOLMOD_INCLUDE_DIR NAMES cholmod.h
          PATH_SUFFIXES suitesparse)
if(CHOLMOD_LIBRARY AND CHOLMOD_INCLUDE_DIR)
    message(STATUS "Found CHOLMOD: ${CHOLMOD_LIBRARY}")
    set(PDNKIT_HAVE_CHOLMOD ON)
else()
    message(STATUS "CHOLMOD not found; using Eigen::SimplicialLLT fallback")
    set(PDNKIT_HAVE_CHOLMOD OFF)
endif()

# Core static library: parsing, model, mesher, solver.
# Linked by both the GUI binary and the test binary.
add_library(pdnkit_core STATIC
    src/sexpr/SExpr.cpp
    src/parser/KicadPcbParser.cpp
    src/model/HitTest.cpp
    src/pi/IrMesher.cpp
    src/pi/IrSolver.cpp
    src/pi/CavityModel.cpp
    src/pi/DecapOptimizer.cpp
    src/render/Camera2D.cpp
    src/render/ZoneMesher.cpp
    src/render/IrResultMesh.cpp
    src/render/SegmentMesher.cpp
    src/render/ViaMesher.cpp
)
target_include_directories(pdnkit_core PUBLIC src)
target_include_directories(pdnkit_core PUBLIC third_party)
target_link_libraries(pdnkit_core PUBLIC
    Eigen3::Eigen
    spdlog::spdlog
)
if(PDNKIT_HAVE_CHOLMOD)
    target_compile_definitions(pdnkit_core PUBLIC PDNKIT_HAVE_CHOLMOD)
    target_include_directories(pdnkit_core PRIVATE ${CHOLMOD_INCLUDE_DIR})
    target_link_libraries(pdnkit_core PUBLIC ${CHOLMOD_LIBRARY})
endif()
target_compile_options(pdnkit_core PRIVATE -Wall -Wextra -Wpedantic)

# GUI executable.
add_executable(pdnkit
    src/main.cpp
    src/MainWindow.cpp
    src/PcbCanvas.cpp
    src/LayerPanel.cpp
    src/AnalysisPanel.cpp
    src/NetStatsPanel.cpp
    src/CavityPanel.cpp
    src/ZfPlotWidget.cpp
    src/ColorLegend.cpp
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
