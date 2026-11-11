CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    VERSION 3.7.1)

# Tests cover the Qt-free core only, so they build and run without Qt or a display.
# Test files mirror src/: tests/<module>/<Name>Tests.cpp
add_executable(vloomdaw_tests
    core/AudioFormatTests.cpp
    core/BrowserFilterTests.cpp
    core/DeviceRefTests.cpp
    core/PositionFormatTests.cpp
    core/TapTempoTests.cpp)

target_link_libraries(vloomdaw_tests PRIVATE vloomdaw_core Catch2::Catch2WithMain)
vloom_set_warnings(vloomdaw_tests)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(vloomdaw_tests)
