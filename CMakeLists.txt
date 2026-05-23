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

add_executable(pdnkit
    src/main.cpp
    src/MainWindow.cpp
    src/PcbCanvas.cpp
)

target_link_libraries(pdnkit PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Eigen3::Eigen
    spdlog::spdlog
    CLI11::CLI11
)

target_compile_options(pdnkit PRIVATE
    -Wall -Wextra -Wpedantic
)
