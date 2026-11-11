# Changelog

All notable changes to VloomDAW are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Main window with a transport bar: play / pause (Space), stop and return to start
  (Shift+Space), go to start (Home), metronome (Ctrl/Cmd+M)
- LCD tempo display: drag (Shift: fine), scroll or double-click to type the BPM.
  The time signature is shown next to it.
- LCD song position: bar · beat · tick, or click to switch to min : sec . ms
- Tap tempo (T)
- Sidebar: an icon strip opens a resizable side panel; first panel is the
  Browser for audio files in your sample folders (add / remove folders, search)
- Settings window (Ctrl/Cmd+,) with pages: General (start-up position format,
  metronome) and Audio (output device, sample rate, buffer size, latency);
  settings are saved and the output device is remembered by name
- Dark theme with a central palette (`ui/Theme`) and SVG icons
- macOS: tool bar in the title bar row; full screen keeps the window buttons
- Window size and position are remembered between sessions
