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
    audio/FileDecoderTests.cpp
    audio/FileEncoderTests.cpp
    audio/TempoTests.cpp
    audio/MixerAudioTests.cpp
    audio/PluginHostTests.cpp
    audio/StemAudioTests.cpp
    core/AudioFormatTests.cpp
    core/AudioIoSetupTests.cpp
    core/BrowserFilterTests.cpp
    core/DecibelsTests.cpp
    core/DynamicsMeterTests.cpp
    core/EffectPreviewTests.cpp
    core/EqResponseTests.cpp
    core/PluginPresetsTests.cpp
    core/SpectrumAnalyzerTests.cpp
    core/DeviceRefTests.cpp
    core/DeviceWatchTests.cpp
    core/FrameStatsTests.cpp
    core/MidiFileTests.cpp
    core/MixerModelTests.cpp
    core/NoteEditsTests.cpp
    core/NoteHistoryTests.cpp
    core/NoteNamesTests.cpp
    core/ParameterChoicesTests.cpp
    core/ParameterScaleTests.cpp
    core/TimeEffectsTests.cpp
    core/PluginMetadataTests.cpp
    core/PluginScannerTests.cpp
    core/PositionFormatTests.cpp
    core/ProcessMemoryTests.cpp
    core/ProjectFileTests.cpp
    core/ProjectTests.cpp
    core/SampleFolderTests.cpp
    core/ScalesTests.cpp
    core/MdxSpectrogramTests.cpp
    core/StemChunksTests.cpp
    core/StemModelsTests.cpp
    core/StemSpectrogramTests.cpp
    core/TapTempoTests.cpp
    core/ThemePresetsTests.cpp
    core/TrackRoutingTests.cpp
    core/TransportLayoutTests.cpp
    core/UndoHistoryTests.cpp
    core/MinisignTests.cpp
    core/UpdateFeedTests.cpp
    core/VersionTests.cpp)

target_link_libraries(vloomdaw_tests PRIVATE vloomdaw_core Catch2::Catch2WithMain)
vloomdaw_copy_ffmpeg_dlls(vloomdaw_tests)
# Small media files the tests decode (tests/audio/data/README.md).
target_compile_definitions(vloomdaw_tests PRIVATE VLOOMDAW_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/audio/data")
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

add_library(vloomdaw_fake_vst2 MODULE plugins/FakeVst2.cpp)
target_compile_features(vloomdaw_fake_vst2 PRIVATE cxx_std_20)
vloom_set_warnings(vloomdaw_fake_vst2)
set_target_properties(vloomdaw_fake_vst2 PROPERTIES
    PREFIX "" OUTPUT_NAME "FakeVst2" CXX_VISIBILITY_PRESET hidden AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(APPLE)
    set_target_properties(vloomdaw_fake_vst2 PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "vst" SUFFIX "")
    set(probe_vst2 $<TARGET_BUNDLE_DIR:vloomdaw_fake_vst2>)
elseif(WIN32)
    set_target_properties(vloomdaw_fake_vst2 PROPERTIES SUFFIX ".dll")
    set(probe_vst2 $<TARGET_FILE:vloomdaw_fake_vst2>)
else()
    set_target_properties(vloomdaw_fake_vst2 PROPERTIES SUFFIX ".so")
    set(probe_vst2 $<TARGET_FILE:vloomdaw_fake_vst2>)
endif()
target_compile_definitions(vloomdaw_tests PRIVATE VLOOMDAW_FAKE_VST2_PATH="${probe_vst2}")
add_dependencies(vloomdaw_tests vloomdaw_fake_vst2)

add_test(NAME "Probe reads a VST2 module" COMMAND vloomdaw-plugin-probe --vst2 ${probe_vst2})
set_tests_properties("Probe reads a VST2 module" PROPERTIES
    PASS_REGULAR_EXPRESSION "@@vloom\tplugin\tFake VST2 Synth\tVloom Test\t200\tInstrument\\|Synth"
    FAIL_REGULAR_EXPRESSION "Forged VST2")
add_test(NAME "Probe fails cleanly on a file that is not a plugin"
    COMMAND vloomdaw-plugin-probe --vst3 ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
set_tests_properties("Probe fails cleanly on a file that is not a plugin" PROPERTIES
    WILL_FAIL TRUE)
add_test(NAME "Probe reports why it failed"
    COMMAND vloomdaw-plugin-probe --clap ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
set_tests_properties("Probe reports why it failed" PROPERTIES
    PASS_REGULAR_EXPRESSION "@@vloom\terror\t")
