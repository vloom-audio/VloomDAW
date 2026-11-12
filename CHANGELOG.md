# Changelog

All notable changes to VloomDAW are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Channel rack volume and routing: each channel has a volume knob (drag, Shift
  for fine, scroll, double-click for 0 dB) and an insert box; click it to send
  the channel to one of the mixer's 16 inserts ("-" plays straight into the
  master). Insert faders, pans, mute and solo now shape the sound, and insert
  meters show what plays through them.
- Losing the audio output (an unplugged interface or headphones) no longer leaves
  the app playing in silence: it carries on through the system default right
  away, says so in the status bar, and switches back to your device when it is
  plugged in again. Your chosen device stays saved.
- Plugin vendor and category: with "Read vendor and category" on, a scan loads each
  new or changed plugin in a separate process (`vloomdaw-plugin-probe`) and asks its
  factory for name, vendor, version and VST3 sub-categories / CLAP features. A plugin
  that crashes or hangs (20 s) is skipped and marked; unchanged plugins reuse the last
  scan. The sidebar groups by category (instruments split into Synth, Sampler, Drums,
  Keys...), vendor or format, and searches names and vendors. Right-click a plugin to
  set its category yourself (kept across scans); Rescan All reads every plugin again.
- The channel rack plays: drop a WAV file from the browser or the desktop on a
  channel (below the last one for a new channel) and its steps trigger the sample
  through vloom-engine's step sequencer. Edits apply while playing without cutting
  notes. Channels without a sample are dimmed.
- Plugins side panel (second sidebar page): lists the VST3 and CLAP plugins found
  by the last scan, grouped by format, with search. Plugins are only available after
  a scan, which the user starts in the Plugin Manager (Options > Plugin Manager…);
  nothing scans automatically. The scan runs in the background, reads file names only
  (no plugin code is loaded), covers the default folders plus folders the user adds,
  and its results are remembered across restarts.
- Main window with a transport bar: play / pause (Space), stop and return to start
  (Shift+Space), go to start (Home), metronome (Ctrl/Cmd+M)
- LCD tempo display: drag (Shift: fine), scroll or double-click to type the BPM.
  The time signature is shown next to it.
- LCD song position: bar · beat · tick, or click to switch to min : sec . ms
- File menu: New Project (Ctrl/Cmd+N: stop, rewind, reset tempo), and Open,
  Open Recent, Save, Save As, Import, Export with their shortcuts. Those report
  that they arrive with projects in milestone M4.
- Tap tempo (T)
- The mixer, piano roll and playlist can be detached into their own windows
  (button in their header) and docked back. Detached windows have minimise /
  maximise / close in the panel header (no native title bar on Windows: drag
  the header to move them) and a taskbar button, close with the main window,
  and are remembered with their position. Transport and view shortcuts work in any
  window.
- Performance overlay (View > Show Performance Overlay, Ctrl/Cmd+Shift+D):
  frames per second, paint time, refresh tick spacing, UI work per frame
  with the frame rate it could reach, and audio load
  (`core/FrameStats`)
- Smoother UI: the display refreshes at the screen's rate (30-240 Hz, was a
  fixed 30), the playhead is drawn anti-aliased at sub-pixel positions,
  editors repaint only the strip a moving playhead covers, LCDs and meters
  paint opaquely, and the refresh timer is precise (it fired every 31-47 ms
  on Windows). Qt's own repaint delay is off (`QT_QPA_UPDATE_IDLE_TIME=0`),
  which removed 15 ms frame-time spikes at 144 Hz. `VLOOM_REFRESH_HZ` forces
  a refresh rate for testing.
- Song editors in the workspace (editing only, nothing plays them yet), backed
  by a tested project model (`core/Project`: channels, patterns, steps, notes,
  playlist clips):
  - Channel rack (F6): mute light, name and 16 step pads per channel; click
    or drag to paint steps, right-click to clear, double-click a name to open
    its piano roll; add channels
  - Piano roll (F7): keyboard, ruler and grid with the key's scale shaded;
    click to add a note, drag to move, drag the right edge to resize,
    right-click to delete (Alt: no snap, Ctrl+wheel: zoom)
  - Playlist (F5): 16 tracks; click to place the current pattern, drag to
    move (Shift: snap to beats), right-click to delete, double-click to make a
    clip's pattern current
  - Playheads follow the transport in all three
- Mixer panel (F9) under the workspace: master and 16 insert tracks with pan,
  fader (cubic taper, unity at 80 %), dB readout, stereo peak meter with hold
  and clip light, mute and solo. The master track drives the engine (gain,
  pan, mute) and meters the real output; inserts get audio with channels in M1.
- Key LCD on the transport bar: root note (C .. B) and scale (major, natural /
  harmonic / melodic minor, the modes, pentatonics, blues, chromatic). Drag or
  scroll a field to step, click to pick. Scale maths in `core/Scales` for the
  piano roll's scale highlighting in M2.
- Click the audio device in the status bar to switch the output device
- FL Studio-style transport bar, in one row, with the tempo and position
  displays centred in the window: pattern / song mode (L), pattern selector
  (add, pick, delete), CPU and RAM meter, master volume knob (drives the engine), pitch
  knob, and buttons for Playlist (F5), Channel rack (F6), Piano roll (F7),
  Mixer (F9) and Browser (Alt+F8). A narrow window moves what does not fit
  behind a » button. Only Browser opens a panel so far; the
  others, song mode, patterns and pitch report the milestone they arrive in.
- Record-arm button on the transport bar (R); it only keeps the armed state
  until the engine can capture audio input
- Sidebar: an icon strip opens a resizable side panel; first panel is the
  Browser for audio files in your sample folders (add / remove folders, search)
- Settings window (Ctrl/Cmd+,) with pages General (start-up position format,
  metronome), Audio (output device and status, sample rate, buffer size,
  latency) and Theme; each page groups its settings in cards with a short
  explanation per setting. Settings are saved and the output device is
  remembered by name.
- Theme editor (Options > Theme…), applied to the whole app as you edit and
  remembered: seven dark presets shown as miniatures of the app (Midnight,
  Graphite, Deep Black, Slate, Forest, Plum, Ember); Hue, Saturation,
  Brightness, Contrast and Text knobs that tune the preset; a colour per role
  (selection, playing, record, LCD, text) with a colour picker; quick accent
  picks; and a warning when the result drops below WCAG AA contrast. Presets
  and accents are checked against WCAG contrast ratios in tests, and the
  colour maths lives in `core/ThemePresets`.
- Icons follow the theme: SVGs are drawn in placeholder colours that
  `Theme::icon()` swaps for the current text, accent and record colours at paint
  time (e.g. the browser's folder icons take the accent colour)
- Dark theme with a central palette (`ui/Theme`) and SVG icons
- macOS: tool bar in the title bar row; full screen keeps the window buttons
- Windows: no native title bar; the window buttons sit in the menu bar row and
  empty menu / tool bar space moves the window (resize, Aero Snap and the
  taskbar still work natively)
- Window size and position are remembered between sessions

### Fixed
- macOS: detached panels (playlist, piano roll, mixer) no longer show a separate
  title bar: like the main window, the panel header takes its place, holds the
  window buttons and moves the window.
- Detached panel windows are named "VLOOM Mixer", "VLOOM Playlist" and "VLOOM
  Piano roll" in the taskbar, the Dock and window switchers.
- Detached panels show "VLOOM" before their title in the header, which is the
  window's title bar on every platform.
