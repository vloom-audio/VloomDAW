# Pinned to the commit of the v3.7.1 tag (tags can be moved).
CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    VERSION 3.7.1
    GIT_TAG fa43b77429ba76c462b1898d6cd2f2d7a9416b14
    GIT_SHALLOW FALSE)

# Tests cover the Qt-free core only, so they build and run without Qt or a display.
# Test files mirror src/: tests/<module>/<Name>Tests.cpp
add_executable(vloomdaw_tests
    core/AudioFormatTests.cpp
    core/BrowserFilterTests.cpp
    core/DecibelsTests.cpp
    core/DeviceRefTests.cpp
    core/FrameStatsTests.cpp
    core/MixerModelTests.cpp
    core/NoteNamesTests.cpp
    core/PluginScannerTests.cpp
    core/PositionFormatTests.cpp
    core/ProcessMemoryTests.cpp
    core/ProjectTests.cpp
    core/ScalesTests.cpp
    core/TapTempoTests.cpp
    core/ThemePresetsTests.cpp)

target_link_libraries(vloomdaw_tests PRIVATE vloomdaw_core Catch2::Catch2WithMain)
vloom_set_warnings(vloomdaw_tests)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(vloomdaw_tests)
