# Every dependency of VloomDAW is declared here, in one place.
# Test-only dependencies live in tests/CMakeLists.txt.

# ---------------------------------------------------------------- vloom-engine
# Keep vloom-engine checked out next to this repo (../vloom-engine) to work on
# both at once — scripts/bootstrap.sh does that. Otherwise it is fetched from GitHub.
set(VLOOM_ENGINE_DIR "${PROJECT_SOURCE_DIR}/../vloom-engine"
    CACHE PATH "Local vloom-engine checkout, used when it exists")
set(VLOOM_ENGINE_GIT_TAG "main"
    CACHE STRING "vloom-engine revision to fetch when no local checkout exists")

if(EXISTS "${VLOOM_ENGINE_DIR}/CMakeLists.txt")
    message(STATUS "VloomDAW: using local vloom-engine at ${VLOOM_ENGINE_DIR}")
    add_subdirectory("${VLOOM_ENGINE_DIR}" "${CMAKE_BINARY_DIR}/vloom-engine")
else()
    message(STATUS "VloomDAW: fetching vloom-engine@${VLOOM_ENGINE_GIT_TAG}")
    CPMAddPackage(
        NAME vloom-engine
        GITHUB_REPOSITORY vloom-audio/vloom-engine
        GIT_TAG ${VLOOM_ENGINE_GIT_TAG}
        OPTIONS "VLOOM_ENGINE_BUILD_TESTS OFF" "VLOOM_ENGINE_BUILD_TOOLS OFF")
endif()

# ---------------------------------------------------------------- Qt 6
if(VLOOMDAW_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg)
    qt_standard_project_setup()
endif()
