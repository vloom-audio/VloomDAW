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
    core/DeviceRef.cpp
    core/PositionFormat.cpp
    core/TapTempo.cpp

    # audio — owns the engine and the hardware device
    audio/AudioController.cpp)
target_include_directories(vloomdaw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vloomdaw_core PUBLIC vloom::engine)
target_compile_definitions(vloomdaw_core PUBLIC VLOOMDAW_VERSION="${PROJECT_VERSION}")
vloom_set_warnings(vloomdaw_core)

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
    ui/browser/BrowserPanel.cpp
    ui/browser/BrowserPanel.hpp
    ui/dialogs/SettingsDialog/AudioSettingsPage.cpp
    ui/dialogs/SettingsDialog/AudioSettingsPage.hpp
    ui/dialogs/SettingsDialog/GeneralSettingsPage.cpp
    ui/dialogs/SettingsDialog/GeneralSettingsPage.hpp
    ui/dialogs/SettingsDialog/SettingsDialog.cpp
    ui/dialogs/SettingsDialog/SettingsDialog.hpp
    ui/widgets/ActivityBar/ActivityBar.cpp
    ui/widgets/ActivityBar/ActivityBar.hpp
    ui/widgets/DeviceSelector/DeviceSelector.cpp
    ui/widgets/DeviceSelector/DeviceSelector.hpp
    ui/widgets/LcdDisplay/LcdDisplay.cpp
    ui/widgets/LcdDisplay/LcdDisplay.hpp
    ui/widgets/PositionDisplay/PositionDisplay.cpp
    ui/widgets/PositionDisplay/PositionDisplay.hpp
    ui/widgets/TempoDisplay/TempoDisplay.cpp
    ui/widgets/TempoDisplay/TempoDisplay.hpp
    ui/widgets/TransportBar/TransportBar.cpp
    ui/widgets/TransportBar/TransportBar.hpp)
# Qt6::Svg provides the SVG icon engine the icons below need at run time.
target_link_libraries(vloomdaw_ui PUBLIC vloomdaw_core Qt6::Widgets Qt6::Svg)
if(APPLE)
    target_sources(vloomdaw_ui PRIVATE ui/MacWindow.hpp ui/MacWindow.mm)
    target_link_libraries(vloomdaw_ui PRIVATE "-framework AppKit")
endif()
qt_add_resources(vloomdaw_ui "icons"
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/resources/icons"
    FILES
        ${PROJECT_SOURCE_DIR}/resources/icons/browser-file.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/browser-folder.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/chevron-down.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/folder.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/metronome.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/minus.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/pause.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/play.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/plus.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/rewind.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/settings.svg
        ${PROJECT_SOURCE_DIR}/resources/icons/stop.svg)
vloom_set_warnings(vloomdaw_ui)

# ---------------------------------------------------------------- app
qt_add_executable(VloomDAW app/main.cpp)
target_link_libraries(VloomDAW PRIVATE vloomdaw_ui)
vloom_set_warnings(VloomDAW)

set_target_properties(VloomDAW PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "VloomDAW"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.vloom-audio.vloomdaw"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS VloomDAW
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
