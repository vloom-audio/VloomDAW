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
    audio/ChannelRackAudioTests.cpp
    audio/MixerAudioTests.cpp
    core/AudioFormatTests.cpp
    core/BrowserFilterTests.cpp
    core/DecibelsTests.cpp
    core/DeviceRefTests.cpp
    core/DeviceWatchTests.cpp
    core/FrameStatsTests.cpp
    core/MixerModelTests.cpp
    core/NoteNamesTests.cpp
    core/PluginMetadataTests.cpp
    core/PluginScannerTests.cpp
    core/PositionFormatTests.cpp
    core/ProcessMemoryTests.cpp
    core/ProjectTests.cpp
    core/SampleFolderTests.cpp
    core/ScalesTests.cpp
    core/TapTempoTests.cpp
    core/ThemePresetsTests.cpp
    core/TransportLayoutTests.cpp)

target_link_libraries(vloomdaw_tests PRIVATE vloomdaw_core Catch2::Catch2WithMain)
vloom_set_warnings(vloomdaw_tests)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(vloomdaw_tests)

# ---------------------------------------------------------------- plugin probe
# Fake VST3 and CLAP modules, built like real ones (bundles on macOS), let the
# real vloomdaw-plugin-probe load a plugin on every OS without any vendor's code.
foreach(format IN ITEMS Vst3 Clap)
    string(TOLOWER ${format} extension)
    add_library(vloomdaw_fake_${extension} MODULE plugins/Fake${format}.cpp)
    target_include_directories(vloomdaw_fake_${extension} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(vloomdaw_fake_${extension} PRIVATE cxx_std_20)
    vloom_set_warnings(vloomdaw_fake_${extension})
    set_target_properties(vloomdaw_fake_${extension} PROPERTIES
        PREFIX "" OUTPUT_NAME "Fake${format}" SUFFIX ".${extension}"
        CXX_VISIBILITY_PRESET hidden AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    if(APPLE)
        set_target_properties(vloomdaw_fake_${extension} PROPERTIES
            BUNDLE TRUE BUNDLE_EXTENSION ${extension} SUFFIX "")
        set(module $<TARGET_BUNDLE_DIR:vloomdaw_fake_${extension}>)
    else()
        set(module $<TARGET_FILE:vloomdaw_fake_${extension}>)
    endif()
    set(probe_${extension} ${module})
endforeach()

add_test(NAME "Probe reads a VST3 factory" COMMAND vloomdaw-plugin-probe --vst3 ${probe_vst3})
set_tests_properties("Probe reads a VST3 factory" PROPERTIES
    PASS_REGULAR_EXPRESSION "@@vloom\tplugin\tFake Reverb\tVloom Test\t1\\.2\\.3\tFx\\|Reverb"
    FAIL_REGULAR_EXPRESSION "Fake Controller|init without newline")
add_test(NAME "Probe reads a CLAP factory" COMMAND vloomdaw-plugin-probe --clap ${probe_clap})
set_tests_properties("Probe reads a CLAP factory" PROPERTIES
    PASS_REGULAR_EXPRESSION "Fake CLAP Reverb\tVloom Test\t0\\.9\taudio-effect\\|reverb\\|stereo.*Fake CLAP Synth"
    FAIL_REGULAR_EXPRESSION "Forged|licence ok")
add_test(NAME "Probe fails cleanly on a file that is not a plugin"
    COMMAND vloomdaw-plugin-probe --vst3 ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
set_tests_properties("Probe fails cleanly on a file that is not a plugin" PROPERTIES
    WILL_FAIL TRUE)
add_test(NAME "Probe reports why it failed"
    COMMAND vloomdaw-plugin-probe --clap ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
set_tests_properties("Probe reports why it failed" PROPERTIES
    PASS_REGULAR_EXPRESSION "@@vloom\terror\t")
