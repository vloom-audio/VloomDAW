cmake_minimum_required(VERSION 3.22)

project(VloomDAW
    VERSION 0.1.0
    DESCRIPTION "Open-source pattern-based digital audio workstation"
    HOMEPAGE_URL "https://github.com/vloom-audio/VloomDAW"
    LANGUAGES C CXX)

# Objective-C++ is used only for small macOS window tweaks in src/ui/*.mm.
if(APPLE)
    enable_language(OBJCXX)
endif()

option(VLOOMDAW_BUILD_APP   "Build the Qt desktop application" ON)
option(VLOOMDAW_BUILD_TESTS "Build unit tests for the Qt-free core" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows and Linux: the programs (and Windows DLLs) the build makes land in
# build/<preset>/bin, apart from CMake's own files, so the app runs from there
# with its helpers next to it. Set before Qt's qt_standard_project_setup(),
# which would otherwise put them in build/<preset> itself on Windows.
# macOS keeps its VloomDAW.app bundle where it was.
if(NOT APPLE AND NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/CPM.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/Dependencies.cmake)

add_subdirectory(src)

if(VLOOMDAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
