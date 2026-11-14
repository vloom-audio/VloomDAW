# Every dependency of VloomDAW is declared here, in one place.
# Test-only dependencies live in tests/CMakeLists.txt.

# ---------------------------------------------------------------- vloom-engine
# Keep vloom-engine checked out next to this repo (../vloom-engine) to work on
# both at once — scripts/bootstrap.sh does that. Otherwise it is fetched from GitHub
# at the commit below, which CI also checks out: builds are reproducible, and a
# change on the engine's main reaches VloomDAW only through a PR that bumps it.
set(VLOOM_ENGINE_DIR "${PROJECT_SOURCE_DIR}/../vloom-engine"
    CACHE PATH "Local vloom-engine checkout, used when it exists")
set(VLOOM_ENGINE_GIT_TAG "7bb549a325d77c24df9b5cf85b4446e9907fc8f1"
    CACHE STRING "vloom-engine commit to fetch when no local checkout exists")

if(EXISTS "${VLOOM_ENGINE_DIR}/CMakeLists.txt")
    message(STATUS "VloomDAW: using local vloom-engine at ${VLOOM_ENGINE_DIR}")
    add_subdirectory("${VLOOM_ENGINE_DIR}" "${CMAKE_BINARY_DIR}/vloom-engine")
else()
    message(STATUS "VloomDAW: fetching vloom-engine@${VLOOM_ENGINE_GIT_TAG}")
    CPMAddPackage(
        NAME vloom-engine
        GITHUB_REPOSITORY vloom-audio/vloom-engine
        GIT_TAG ${VLOOM_ENGINE_GIT_TAG}
        GIT_SHALLOW FALSE
        OPTIONS "VLOOM_ENGINE_BUILD_TESTS OFF" "VLOOM_ENGINE_BUILD_TOOLS OFF")
endif()

# ---------------------------------------------------------------- Signalsmith Stretch
# Time-stretching for audio clips fitted to the song's tempo (audio/TimeStretch).
# MIT, header-only. Both pinned to the commits of their tags; Linear is added
# first so Stretch's own CMake does not fetch it by a (movable) tag.
CPMAddPackage(
    NAME signalsmith-linear
    GITHUB_REPOSITORY Signalsmith-Audio/linear
    GIT_TAG de55e6a50ffcf6f8f43f649692d94691c7025151 # 0.6.4
    GIT_SHALLOW FALSE)
CPMAddPackage(
    NAME signalsmith-stretch
    GITHUB_REPOSITORY Signalsmith-Audio/signalsmith-stretch
    GIT_TAG a670068d9aeb64913331d5cc29337b19a457a7df # 1.4.0
    GIT_SHALLOW FALSE)
# Their headers as system headers: their warnings are not ours to fail on.
set_target_properties(signalsmith-linear signalsmith-stretch PROPERTIES SYSTEM TRUE)

# ---------------------------------------------------------------- nlohmann/json
# Reads and writes the .vloom project file (core/ProjectFile). MIT,
# header-only, fuzzed in OSS-Fuzz. Pinned to the commit of the v3.12.0 tag.
CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    GIT_TAG 55f93686c01528224f448c19128836e7df245f72 # v3.12.0
    GIT_SHALLOW FALSE
    OPTIONS "JSON_BuildTests OFF" "JSON_Install OFF")
set_target_properties(nlohmann_json PROPERTIES SYSTEM TRUE)

# ---------------------------------------------------------------- FFmpeg
include(${CMAKE_CURRENT_LIST_DIR}/FFmpeg.cmake)

# ---------------------------------------------------------------- Qt 6
if(VLOOMDAW_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg)
    qt_standard_project_setup()
endif()
