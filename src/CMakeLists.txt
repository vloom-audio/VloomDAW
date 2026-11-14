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
    core/BrowserFilter.cpp
    core/Decibels.cpp
    core/DeviceRef.cpp
    core/DeviceWatch.cpp
    core/FrameStats.cpp
    core/MixerModel.cpp
    core/Names.cpp
    core/NoteNames.cpp
    core/PluginMetadata.cpp
    core/PluginScanner.cpp
    core/PositionFormat.cpp
    core/ProcessMemory.cpp
    core/Project.cpp
    core/SampleFolder.cpp
    core/Scales.cpp
    core/TapTempo.cpp
    core/ThemePresets.cpp
    core/TransportLayout.cpp

    # audio — owns the engine and the hardware device
    audio/AudioController.cpp
    audio/ChannelRackAudio.cpp
    audio/FileDecoder.cpp
    audio/TempoDetector.cpp
    audio/TimeStretch.cpp
    audio/MixerAudio.cpp
    audio/PluginHost.cpp)
target_include_directories(vloomdaw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vloomdaw_core PUBLIC vloom::engine PRIVATE vloomdaw::ffmpeg signalsmith-stretch)
target_compile_definitions(vloomdaw_core PUBLIC VLOOMDAW_VERSION="${PROJECT_VERSION}")
vloom_set_warnings(vloomdaw_core)

# ---------------------------------------------------------------- plugin probe (no Qt)
# Loads one plugin per run to read its factory; the app runs it as a child
# process so a crashing plugin cannot take VloomDAW down. See probe/main.cpp.
add_executable(vloomdaw-plugin-probe probe/main.cpp)
target_include_directories(vloomdaw-plugin-probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(vloomdaw-plugin-probe PRIVATE cxx_std_20)
set_target_properties(vloomdaw-plugin-probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
vloom_set_warnings(vloomdaw-plugin-probe)
if(WIN32)
    target_link_libraries(vloomdaw-plugin-probe PRIVATE shell32)
elseif(APPLE)
    target_link_libraries(vloomdaw-plugin-probe PRIVATE "-framework CoreFoundation")
elseif(UNIX)
    target_link_libraries(vloomdaw-plugin-probe PRIVATE ${CMAKE_DL_LIBS})
endif()

if(NOT VLOOMDAW_BUILD_APP)
    return()
endif()

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
    ui/dialogs/SettingsDialog/AudioSettingsPage.cpp
    ui/dialogs/SettingsDialog/AudioSettingsPage.hpp
    ui/dialogs/SettingsDialog/GeneralSettingsPage.cpp
    ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp
    ui/dialogs/SettingsDialog/SettingsCard.cpp
    ui/dialogs/SettingsDialog/SettingsCard.hpp
    ui/dialogs/SettingsDialog/SettingsDialog.cpp
    ui/dialogs/SettingsDialog/SettingsDialog.hpp
    ui/dialogs/SettingsDialog/ThemeSettingsPage.cpp
    ui/dialogs/SettingsDialog/ThemeSettingsPage.hpp
    ui/mixer/MixerPanel.cpp
    ui/mixer/MixerPanel.hpp
    ui/mixer/MixerStrip.cpp
    ui/mixer/MixerStrip.hpp
    ui/pianoroll/PianoRollView.cpp
    ui/pianoroll/PianoRollView.hpp
    ui/playlist/PlaylistView.cpp
    ui/playlist/PlaylistView.hpp
    ui/plugins/PluginDrag.cpp
    ui/plugins/PluginDrag.hpp
    ui/plugins/PluginEditorWindow.cpp
    ui/plugins/PluginEditorWindow.hpp
    ui/plugins/PluginManagerDialog.cpp
    ui/plugins/PluginManagerDialog.hpp
    ui/plugins/PluginProber.cpp
    ui/plugins/PluginProber.hpp
    ui/plugins/PluginsPanel.cpp
    ui/plugins/PluginsPanel.hpp
    ui/plugins/PluginStore.cpp
    ui/plugins/PluginStore.hpp
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
target_link_libraries(vloomdaw_ui PUBLIC vloomdaw_core Qt6::Widgets Qt6::Svg)
if(APPLE)
    target_sources(vloomdaw_ui PRIVATE ui/MacWindow.hpp ui/MacWindow.mm)
    target_link_libraries(vloomdaw_ui PRIVATE "-framework AppKit")
elseif(WIN32)
    target_sources(vloomdaw_ui PRIVATE
        ui/WinWindow.hpp
        ui/WinWindow.cpp
        ui/widgets/WindowButtons/WindowButtons.cpp
        ui/widgets/WindowButtons/WindowButtons.hpp)
    target_link_libraries(vloomdaw_ui PRIVATE dwmapi)
endif()
qt_add_resources(vloomdaw_ui "icons"
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/resources/icons"
    FILES
        ${PROJECT_SOURCE_DIR}/resources/icons/browser-file.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/browser-folder.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/channel-rack.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/chevron-down.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/chevrons-right.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/detach.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/dock.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/folder.svg
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
        ${PROJECT_SOURCE_DIR}/resources/icons/record.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/refresh.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/rewind.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/settings.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/sliders.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/speaker.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/stop.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-delete.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-draw.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-mute.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-paint.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-playback.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-select.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-slice.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/tool-zoom.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/track-mute.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/track-solo.svg
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
    FILES ${PROJECT_SOURCE_DIR}/resources/app/vloomdaw.png)
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
add_dependencies(VloomDAW vloomdaw-plugin-probe)
if(APPLE)
    add_custom_command(TARGET VloomDAW POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vloomdaw-plugin-probe> $<TARGET_FILE_DIR:VloomDAW>/
        VERBATIM)
else()
    set_target_properties(vloomdaw-plugin-probe PROPERTIES
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
    install(TARGETS vloomdaw-plugin-probe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
# Windows: FFmpeg's DLLs (LGPL) go next to the app, with FFmpeg's licence.
# macOS and Linux release builds link a static FFmpeg (scripts/build-ffmpeg.sh).
if(WIN32 AND VLOOMDAW_FFMPEG_DLLS)
    install(FILES ${VLOOMDAW_FFMPEG_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(EXISTS "${FFMPEG_ROOT}/LICENSE.txt")
        install(FILES "${FFMPEG_ROOT}/LICENSE.txt" DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME FFmpeg-LICENSE.txt)
    endif()
endif()
