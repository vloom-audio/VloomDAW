# VloomDAW is split into three layers. Dependencies only point downwards:
#
#   app/   VloomDAW executable: main(), wiring
#     │
#   ui/    vloomdaw_ui   (Qt Widgets) windows and widgets, no audio logic
#     │
#   core/, audio/        vloomdaw_core (no Qt) logic and engine glue, unit-tested

# ---------------------------------------------------------------- core (no Qt)
add_library(vloomdaw_core STATIC)
target_sources(vloomdaw_core PRIVATE
    # core — pure logic: formatting, (later) project model, commands, undo
    core/AudioFormat.cpp
    core/AudioIoSetup.cpp
    core/BrowserFilter.cpp
    core/Decibels.cpp
    core/DynamicsMeter.cpp
    core/EqResponse.cpp
    core/PluginPresets.cpp
    core/SpectrumAnalyzer.cpp
    core/DeviceRef.cpp
    core/DeviceWatch.cpp
    core/FrameStats.cpp
    core/MidiFile.cpp
    core/MixerModel.cpp
    core/Names.cpp
    core/NoteEdits.cpp
    core/NoteHistory.cpp
    core/NoteNames.cpp
    core/ParameterChoices.cpp
    core/ParameterScale.cpp
    core/PluginMetadata.cpp
    core/PluginScanner.cpp
    core/PositionFormat.cpp
    core/ProcessMemory.cpp
    core/Project.cpp
    core/ProjectFile.cpp
    core/SampleFolder.cpp
    core/Scales.cpp
    core/MdxSpectrogram.cpp
    core/StemChunks.cpp
    core/StemModels.cpp
    core/StemSpectrogram.cpp
    core/TapTempo.cpp
    core/ThemePresets.cpp
    core/TimeEffects.cpp
    core/TrackRouting.cpp
    core/TransportLayout.cpp
    core/UndoHistory.cpp
    core/Minisign.cpp
    core/UpdateFeed.cpp
    core/Version.cpp

    # audio — owns the engine and the hardware device
    audio/AudioController.cpp
    audio/ChannelRackAudio.cpp
    audio/FileDecoder.cpp
    audio/FileEncoder.cpp
    audio/TempoDetector.cpp
    audio/TimeStretch.cpp
    audio/MixerAudio.cpp
    audio/PluginHost.cpp
    audio/StemAudio.cpp)
target_include_directories(vloomdaw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vloomdaw_core PUBLIC vloom::engine vloom::dsp PRIVATE vloomdaw::ffmpeg signalsmith-stretch nlohmann_json::nlohmann_json monocypher)
# The version the app reports (About, the updater, project files): the release
# workflow passes the tag's full version, e.g. 0.1.0-rc.1, so a release
# candidate knows it is older than 0.1.0 and is offered it. Its numbers must be
# the project's VERSION; without it, the app reports PROJECT_VERSION.
set(VLOOMDAW_VERSION_LABEL "${PROJECT_VERSION}" CACHE STRING "Version the app reports, e.g. 0.1.0-rc.1")
if(NOT VLOOMDAW_VERSION_LABEL MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "VLOOMDAW_VERSION_LABEL '${VLOOMDAW_VERSION_LABEL}' is not a semantic version")
endif()
string(REGEX REPLACE "-.*$" "" vloomdaw_label_numbers "${VLOOMDAW_VERSION_LABEL}")
if(NOT vloomdaw_label_numbers STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "VLOOMDAW_VERSION_LABEL ${VLOOMDAW_VERSION_LABEL} does not match VERSION ${PROJECT_VERSION}")
endif()
message(STATUS "VloomDAW: reports version ${VLOOMDAW_VERSION_LABEL}")
target_compile_definitions(vloomdaw_core PUBLIC VLOOMDAW_VERSION="${VLOOMDAW_VERSION_LABEL}")
if(VLOOMDAW_STEMS_ONNX)
    # The model store offers the ONNX models only where the helper can run them.
    target_compile_definitions(vloomdaw_core PUBLIC VLOOMDAW_STEMS_ONNX)
endif()
vloom_set_warnings(vloomdaw_core)

# ---------------------------------------------------------------- plugin probe (no Qt)
# Loads one plugin per run to read its factory; the app runs it as a child
# process so a crashing plugin cannot take VloomDAW down. See probe/main.cpp.
add_executable(vloomdaw-plugin-probe probe/main.cpp)
target_include_directories(vloomdaw-plugin-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vloomdaw-plugin-probe PRIVATE cxx_std_20)
set_target_properties(vloomdaw-plugin-probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
vloom_set_warnings(vloomdaw-plugin-probe)
target_link_libraries(vloomdaw-plugin-probe PRIVATE vloom::engine)
if(WIN32)
    target_link_libraries(vloomdaw-plugin-probe PRIVATE shell32)
elseif(APPLE)
    target_link_libraries(vloomdaw-plugin-probe PRIVATE "-framework CoreFoundation")
elseif(UNIX)
    target_link_libraries(vloomdaw-plugin-probe PRIVATE ${CMAKE_DL_LIBS})
endif()

# Windows out-of-process VST 2 bridge (vloom-engine ADR 0011).
if(WIN32 AND NOT TARGET vloom-plugin-bridge AND EXISTS "${VLOOM_ENGINE_DIR}/tools/vloom-plugin-bridge/CMakeLists.txt")
    add_subdirectory("${VLOOM_ENGINE_DIR}/tools/vloom-plugin-bridge" "${CMAKE_BINARY_DIR}/vloom-plugin-bridge")
elseif(WIN32 AND NOT TARGET vloom-plugin-bridge AND DEFINED vloom-engine_SOURCE_DIR
       AND EXISTS "${vloom-engine_SOURCE_DIR}/tools/vloom-plugin-bridge/CMakeLists.txt")
    add_subdirectory("${vloom-engine_SOURCE_DIR}/tools/vloom-plugin-bridge" "${CMAKE_BINARY_DIR}/vloom-plugin-bridge")
endif()

if(NOT VLOOMDAW_BUILD_APP)
    return()
endif()

# ---------------------------------------------------------------- stem separator (no Qt)
# Separates audio into stems with a Demucs model; the app runs it as a child
# process (it takes minutes, and ending the process cancels it). See stems/main.cpp.
add_subdirectory(stems)

# ---------------------------------------------------------------- ui (Qt)
qt_add_library(vloomdaw_ui STATIC
    ui/MainWindow.cpp
    ui/MainWindow.hpp
    ui/Theme.cpp
    ui/Theme.hpp
    ui/AppSettings.cpp
    ui/AppSettings.hpp
    ui/DeviceScanner.cpp
    ui/DeviceScanner.hpp
    ui/FilePaths.cpp
    ui/FilePaths.hpp
    ui/RecentProjects.cpp
    ui/RecentProjects.hpp
    ui/launcher/LauncherDialog.cpp
    ui/launcher/LauncherDialog.hpp
    ui/browser/BrowserPanel.cpp
    ui/browser/BrowserPanel.hpp
    ui/AudioDecoder.cpp
    ui/AudioDecoder.hpp
    ui/DroppedFiles.cpp
    ui/DroppedFiles.hpp
    ui/browser/BrowserRoles.hpp
    ui/browser/SamplePreviewer.cpp
    ui/browser/SampleWaveform.cpp
    ui/browser/SampleWaveform.hpp
    ui/browser/SamplePreviewer.hpp
    ui/browser/SampleTreeModel.cpp
    ui/browser/SampleTreeModel.hpp
    ui/browser/SearchResultsModel.cpp
    ui/browser/SearchResultsModel.hpp
    ui/channelrack/ChannelRackView.cpp
    ui/channelrack/ChannelRackView.hpp
    ui/dialogs/AboutDialog/AboutDialog.cpp
    ui/dialogs/AboutDialog/AboutDialog.hpp
    ui/dialogs/AddTracksDialog/AddTracksDialog.cpp
    ui/dialogs/AddTracksDialog/AddTracksDialog.hpp
    ui/dialogs/AudioIoDialog/AudioIoDialog.cpp
    ui/dialogs/AudioIoDialog/AudioIoDialog.hpp
    ui/dialogs/ExportDialog.cpp
    ui/dialogs/ExportDialog.hpp
    ui/dialogs/ProjectLoadDialog/ProjectLoadDialog.cpp
    ui/dialogs/ProjectLoadDialog/ProjectLoadDialog.hpp
    ui/dialogs/SettingsDialog/AudioSettingsPage.cpp
    ui/dialogs/SettingsDialog/AudioSettingsPage.hpp
    ui/dialogs/SettingsDialog/GeneralSettingsPage.cpp
    ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp
    ui/dialogs/SettingsDialog/SettingsCard.cpp
    ui/dialogs/SettingsDialog/SettingsCard.hpp
    ui/dialogs/SettingsDialog/SettingsDialog.cpp
    ui/dialogs/SettingsDialog/SettingsDialog.hpp
    ui/dialogs/SettingsDialog/StemSettingsPage.cpp
    ui/dialogs/SettingsDialog/StemSettingsPage.hpp
    ui/dialogs/SettingsDialog/ThemeSettingsPage.cpp
    ui/dialogs/SettingsDialog/ThemeSettingsPage.hpp
    ui/mixer/MixerPanel.cpp
    ui/mixer/MixerPanel.hpp
    ui/mixer/MixerSlotRack.cpp
    ui/mixer/MixerSlotRack.hpp
    ui/mixer/MixerStrip.cpp
    ui/mixer/MixerStrip.hpp
    ui/pianoroll/PianoRollView.cpp
    ui/pianoroll/PianoRollView.hpp
    ui/playlist/PlaylistView.cpp
    ui/playlist/PlaylistView.hpp
    ui/plugins/builtin/ArcKnob.cpp
    ui/plugins/builtin/ArcKnob.hpp
    ui/plugins/builtin/BuiltinEditorWindow.cpp
    ui/plugins/builtin/BuiltinEditorWindow.hpp
    ui/plugins/builtin/BuiltinEditors.cpp
    ui/plugins/builtin/BuiltinEditors.hpp
    ui/plugins/builtin/ChorusEditor.cpp
    ui/plugins/builtin/ChorusEditor.hpp
    ui/plugins/builtin/CompressorEditor.cpp
    ui/plugins/builtin/CompressorEditor.hpp
    ui/plugins/builtin/ControlPanel.cpp
    ui/plugins/builtin/ControlPanel.hpp
    ui/plugins/builtin/DelayEditor.cpp
    ui/plugins/builtin/DelayEditor.hpp
    ui/plugins/builtin/EditorStyle.cpp
    ui/plugins/builtin/EditorStyle.hpp
    ui/plugins/builtin/FlangerEditor.cpp
    ui/plugins/builtin/FlangerEditor.hpp
    ui/plugins/builtin/GateEditor.cpp
    ui/plugins/builtin/GateEditor.hpp
    ui/plugins/builtin/LevelHistoryView.cpp
    ui/plugins/builtin/LevelHistoryView.hpp
    ui/plugins/builtin/LimiterEditor.cpp
    ui/plugins/builtin/LimiterEditor.hpp
    ui/plugins/builtin/MeterBars.cpp
    ui/plugins/builtin/MeterBars.hpp
    ui/plugins/builtin/ParameterControl.cpp
    ui/plugins/builtin/ParameterControl.hpp
    ui/plugins/builtin/PhaserEditor.cpp
    ui/plugins/builtin/PhaserEditor.hpp
    ui/plugins/builtin/PillToggle.cpp
    ui/plugins/builtin/PillToggle.hpp
    ui/plugins/builtin/Readout.cpp
    ui/plugins/builtin/Readout.hpp
    ui/plugins/builtin/ReverbEditor.cpp
    ui/plugins/builtin/ReverbEditor.hpp
    ui/plugins/builtin/TimeEffectParts.cpp
    ui/plugins/builtin/TimeEffectParts.hpp
    ui/plugins/builtin/TransferCurve.cpp
    ui/plugins/builtin/TransferCurve.hpp
    ui/plugins/builtin/TremoloEditor.cpp
    ui/plugins/builtin/TremoloEditor.hpp
    ui/plugins/PresetBar.cpp
    ui/plugins/PresetBar.hpp
    ui/plugins/PluginDrag.cpp
    ui/plugins/PluginDrag.hpp
    ui/plugins/EqEditorWindow.cpp
    ui/plugins/EqEditorWindow.hpp
    ui/plugins/ParameterEditorWindow.cpp
    ui/plugins/ParameterEditorWindow.hpp
    ui/plugins/ParameterText.cpp
    ui/plugins/ParameterText.hpp
    ui/plugins/SynthEditorWindow.cpp
    ui/plugins/SynthEditorWindow.hpp
    ui/plugins/PluginEditorWindow.cpp
    ui/plugins/PluginEditorWindow.hpp
    ui/plugins/PluginManagerDialog.cpp
    ui/plugins/PluginManagerDialog.hpp
    ui/plugins/PluginPickerPopup.cpp
    ui/plugins/PluginPickerPopup.hpp
    ui/plugins/PluginProber.cpp
    ui/plugins/PluginProber.hpp
    ui/stems/StemModelStore.cpp
    ui/stems/StemModelStore.hpp
    ui/stems/StemSeparator.cpp
    ui/stems/StemSeparator.hpp
    ui/update/UpdateDialog.cpp
    ui/update/UpdateDialog.hpp
    ui/update/UpdateInstaller.cpp
    ui/update/UpdateInstaller.hpp
    ui/update/UpdateService.cpp
    ui/update/UpdateService.hpp
    ui/plugins/PluginsPanel.cpp
    ui/plugins/PluginsPanel.hpp
    ui/plugins/PluginStore.cpp
    ui/plugins/PluginStore.hpp
    ui/plugins/PluginThumbnails.cpp
    ui/plugins/PluginThumbnails.hpp
    ui/widgets/ActivityBar/ActivityBar.cpp
    ui/widgets/ActivityBar/ActivityBar.hpp
    ui/widgets/ColorMenu/ColorMenu.cpp
    ui/widgets/ColorMenu/ColorMenu.hpp
    ui/widgets/DetachButton/DetachButton.cpp
    ui/widgets/DetachButton/DetachButton.hpp
    ui/widgets/DeviceSelector/DeviceSelector.cpp
    ui/widgets/DeviceSelector/DeviceSelector.hpp
    ui/widgets/Fader/Fader.cpp
    ui/widgets/Fader/Fader.hpp
    ui/widgets/FloatingPanel/FloatingPanel.cpp
    ui/widgets/FloatingPanel/FloatingPanel.hpp
    ui/widgets/KeyDisplay/KeyDisplay.cpp
    ui/widgets/KeyDisplay/KeyDisplay.hpp
    ui/widgets/Knob/Knob.cpp
    ui/widgets/Knob/Knob.hpp
    ui/widgets/LcdDisplay/LcdDisplay.cpp
    ui/widgets/LcdDisplay/LcdDisplay.hpp
    ui/widgets/LevelMeter/LevelMeter.cpp
    ui/widgets/LevelMeter/LevelMeter.hpp
    ui/widgets/PanelFrame/PanelFrame.cpp
    ui/widgets/PanelFrame/PanelFrame.hpp
    ui/widgets/PatternSelector/PatternSelector.cpp
    ui/widgets/PatternSelector/PatternSelector.hpp
    ui/widgets/PerformanceMeter/PerformanceMeter.cpp
    ui/widgets/PerformanceMeter/PerformanceMeter.hpp
    ui/widgets/PerformanceOverlay/PerformanceOverlay.cpp
    ui/widgets/PerformanceOverlay/PerformanceOverlay.hpp
    ui/widgets/PositionDisplay/PositionDisplay.cpp
    ui/widgets/PositionDisplay/PositionDisplay.hpp
    ui/widgets/TempoDisplay/TempoDisplay.cpp
    ui/widgets/TempoDisplay/TempoDisplay.hpp
    ui/widgets/Timeline/Timeline.cpp
    ui/widgets/Timeline/Timeline.hpp
    ui/widgets/ToggleSwitch/ToggleSwitch.cpp
    ui/widgets/ToggleSwitch/ToggleSwitch.hpp
    ui/widgets/ToolGroup/ToolGroup.cpp
    ui/widgets/ToolGroup/ToolGroup.hpp
    ui/widgets/TransportBar/TransportBar.cpp
    ui/widgets/TransportBar/TransportBar.hpp
    ui/widgets/ViewButtons/ViewButtons.cpp
    ui/widgets/ViewButtons/ViewButtons.hpp)
# Qt6::Svg provides the SVG icon engine the icons below need at run time.
target_link_libraries(vloomdaw_ui PUBLIC vloomdaw_core Qt6::Widgets Qt6::Svg PRIVATE Qt6::Network)

# The minisign public key updates must be signed with (ui/update/UpdateService,
# docs/releasing.md). Without the file the app still tells of new versions,
# but only points to their page: it installs nothing it cannot check.
set(VLOOMDAW_UPDATE_KEY_FILE "${PROJECT_SOURCE_DIR}/packaging/update/minisign.pub"
    CACHE FILEPATH "minisign public key for signed updates")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VLOOMDAW_UPDATE_KEY_FILE}")
if(EXISTS "${VLOOMDAW_UPDATE_KEY_FILE}")
    file(STRINGS "${VLOOMDAW_UPDATE_KEY_FILE}" update_key REGEX "^RW[A-Za-z0-9+/]+=*$")
    list(LENGTH update_key update_key_count)
    if(NOT update_key_count EQUAL 1)
        message(FATAL_ERROR "${VLOOMDAW_UPDATE_KEY_FILE} is not a minisign public key")
    endif()
    message(STATUS "VloomDAW: updates are checked with the key in ${VLOOMDAW_UPDATE_KEY_FILE}")
    set_property(SOURCE ui/update/UpdateService.cpp APPEND PROPERTY
        COMPILE_DEFINITIONS "VLOOMDAW_UPDATE_PUBLIC_KEY=\"${update_key}\"")
else()
    message(STATUS "VloomDAW: no update signing key; the app will not install updates itself")
endif()
if(APPLE)
    target_sources(vloomdaw_ui PRIVATE ui/MacWindow.hpp ui/MacWindow.mm)
    target_link_libraries(vloomdaw_ui PRIVATE "-framework AppKit")
elseif(WIN32)
    target_sources(vloomdaw_ui PRIVATE
        ui/WinWindow.hpp
        ui/WinWindow.cpp
        ui/widgets/WindowButtons/WindowButtons.cpp
        ui/widgets/WindowButtons/WindowButtons.hpp)
    target_link_libraries(vloomdaw_ui PRIVATE dwmapi shell32 advapi32)
endif()
qt_add_resources(vloomdaw_ui "icons"
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/resources/icons"
    FILES
        ${PROJECT_SOURCE_DIR}/resources/icons/audio-io.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/browser-file.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/browser-folder.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/channel-rack.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/chevron-down.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/chevron-left.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/chevron-right.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/chevrons-right.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/detach.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/dock.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/follow-playhead.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/folder.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/home.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/input-monitor.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/input-monitor-active.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/loop.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/metronome.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/minus.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/mixer.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/palette.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/pause.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/piano-roll.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/play.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/playlist.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/plug.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/plus.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/power.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/record.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/refresh.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/rewind.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/settings.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/skip-end.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/sliders.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/speaker.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/speaker-mute.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/step-back.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/step-forward.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/stems.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/stop.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/thumbnail.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-delete.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-draw.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-mute.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-paint.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-playback.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-select.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-slice.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-zoom.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/track-arm.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/track-arm-active.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/track-audio.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/track-instrument.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/track-mute.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/track-solo.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/volume-down.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/volume-up.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/window-close.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/window-maximize.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/window-minimize.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/window-restore.svg)
# The app icon, set as the window icon in main() (resources/app/README.md).
qt_add_resources(vloomdaw_ui "fonts"
    PREFIX "/fonts"
    BASE "${PROJECT_SOURCE_DIR}/resources/fonts"
    FILES
        ${PROJECT_SOURCE_DIR}/resources/fonts/NotoSans-Regular.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/NotoSans-SemiBold.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/NotoSans-Bold.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/NotoSansThai-Regular.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/NotoSansThai-SemiBold.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/NotoSansThai-Bold.ttf)
qt_add_resources(vloomdaw_ui "app_icon"
    PREFIX "/app"
    BASE "${PROJECT_SOURCE_DIR}/resources/app"
    FILES
        ${PROJECT_SOURCE_DIR}/resources/app/vloomdaw.png
        ${PROJECT_SOURCE_DIR}/resources/app/vloom-project.png)
vloom_set_warnings(vloomdaw_ui)

# ---------------------------------------------------------------- app
qt_add_executable(VloomDAW app/main.cpp)
# The icon the OS shows for the executable / bundle (generated, see resources/app/README.md).
if(WIN32)
    target_sources(VloomDAW PRIVATE app/VloomDAW.rc)
elseif(APPLE)
    set(VLOOMDAW_ICNS ${PROJECT_SOURCE_DIR}/resources/app/vloomdaw.icns)
    set_source_files_properties(${VLOOMDAW_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(VloomDAW PRIVATE ${VLOOMDAW_ICNS})
endif()
target_link_libraries(VloomDAW PRIVATE vloomdaw_ui)
vloomdaw_copy_ffmpeg_dlls(VloomDAW)
# The app finds the probe next to its own executable (inside the bundle on macOS).
add_dependencies(VloomDAW vloomdaw-plugin-probe vloomdaw-stems)
if(TARGET vloom-plugin-bridge)
    add_dependencies(VloomDAW vloom-plugin-bridge)
    set_target_properties(vloom-plugin-bridge PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:VloomDAW>)
endif()
if(APPLE)
    add_custom_command(TARGET VloomDAW POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vloomdaw-plugin-probe> $<TARGET_FILE:vloomdaw-stems> $<TARGET_FILE_DIR:VloomDAW>/
        VERBATIM)
    # Recording and input monitoring open the microphone (CoreAudio input):
    # without a usage description macOS ends the app the moment it does.
    # Added to whichever Info.plist the bundle was given, before signing.
    add_custom_command(TARGET VloomDAW POST_BUILD
        COMMAND plutil -replace NSMicrophoneUsageDescription -string
            "VloomDAW records and monitors audio from the inputs you choose."
            $<TARGET_BUNDLE_CONTENT_DIR:VloomDAW>/Info.plist
        VERBATIM)
else()
    set_target_properties(vloomdaw-plugin-probe vloomdaw-stems PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:VloomDAW>)
endif()
vloom_set_warnings(VloomDAW)

set_target_properties(VloomDAW PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "VloomDAW"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.vloom-audio.vloomdaw"
    MACOSX_BUNDLE_ICON_FILE "vloomdaw.icns"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS VloomDAW
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(NOT APPLE)
    install(TARGETS vloomdaw-plugin-probe vloomdaw-stems RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(TARGET vloom-plugin-bridge)
        install(TARGETS vloom-plugin-bridge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
# Windows: FFmpeg's DLLs (LGPL) go next to the app, with FFmpeg's licence.
# macOS and Linux release builds link a static FFmpeg (scripts/build-ffmpeg.sh).
# The stem separator's ONNX Runtime and DirectML, with their licences.
# Both are Windows-only: their licences go to licenses/, beside bin/
# (packaging/windows/VloomDAW.iss installs both folders).
set(VLOOMDAW_INSTALL_LICENSEDIR licenses)
if(VLOOMDAW_STEMS_ONNX)
    install(FILES ${VLOOMDAW_ONNX_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
    list(GET VLOOMDAW_ONNX_LICENSES 0 _ort_license)
    list(GET VLOOMDAW_ONNX_LICENSES 1 _ort_notices)
    list(GET VLOOMDAW_ONNX_LICENSES 2 _dml_license)
    install(FILES "${_ort_license}" DESTINATION ${VLOOMDAW_INSTALL_LICENSEDIR} RENAME OnnxRuntime-LICENSE.txt)
    install(FILES "${_ort_notices}" DESTINATION ${VLOOMDAW_INSTALL_LICENSEDIR}
        RENAME OnnxRuntime-ThirdPartyNotices.txt)
    install(FILES "${_dml_license}" DESTINATION ${VLOOMDAW_INSTALL_LICENSEDIR} RENAME DirectML-LICENSE.txt)
endif()
if(WIN32 AND VLOOMDAW_FFMPEG_DLLS)
    install(FILES ${VLOOMDAW_FFMPEG_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(EXISTS "${FFMPEG_ROOT}/LICENSE.txt")
        install(FILES "${FFMPEG_ROOT}/LICENSE.txt" DESTINATION ${VLOOMDAW_INSTALL_LICENSEDIR}
            RENAME FFmpeg-LICENSE.txt)
    endif()
endif()
