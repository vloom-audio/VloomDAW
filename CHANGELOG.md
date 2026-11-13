# Changelog

All notable changes to VloomDAW are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Security
- Supply chain: GitHub Actions are pinned to commit SHAs (checkout v7.0.1, cache
  v6.1.0, install-qt-action v4.5.0; Dependabot keeps them current), the CPM.cmake
  download is checked against a pinned SHA-256 (a cached copy that does not match is
  fetched again), Catch2 is pinned to the commit of its v3.7.1 tag, and vloom-engine
  is pinned to a commit in `cmake/Dependencies.cmake`, which CI checks out too, instead
  of following the engine's `main`.

### Added
- Pan knob for each channel in the channel rack, beside the volume knob: drag
  (Shift: fine), scroll, double-click for the centre; the tooltip reads C,
  L 35, R 100.
- Browser refresh: the ⟳ button, or F5 while the browser's list has the
  keyboard (F5 still opens the playlist elsewhere), reads the folder again.
  Open folders stay open and the selection and scroll position stay; a search
  runs again.
- Reorder mixer inserts (right-click a strip, Move Left / Right) and playlist
  tracks (right-click a track name, Move Up / Down). Settings, names and
  clips go along; channels follow their insert.
- Song mode plays the playlist: with SONG (`L`) on the transport, Play runs
  through the pattern clips (each channel's steps of that pattern) and the
  audio clips from the playhead; starting or jumping into the middle of an
  audio clip plays it from there. PAT still loops the current pattern.
- Audio clips in the playlist: drop a WAV file from the browser or the
  desktop on a track (snapped to the bar; below the last track adds one). As
  in FL Studio, the clip plays through a channel of the channel rack: a new
  one named after the file, or the channel already playing it, so the
  channel's volume, pan, mute and mixer insert apply. The clip shows the
  channel's name and the waveform and is as long as the sound at the current
  tempo; drag to move (Shift: beats), right-click to delete. Dropping one in
  pattern mode turns song mode on, so Play is not silent.
- Name mixer inserts: double-click an insert's name, or right-click its strip
  and choose Rename…; an empty name gives back "Insert N". A long name is cut
  with "…" on the strip and shown whole in its tooltip; the channel rack's
  insert menu lists it as "3 · Drums". A named insert keeps its name when an
  earlier one is deleted.
- Playlist tracks: a new project starts with one track instead of 16; "+ Track"
  in the playlist header adds one (up to 500), "−"
  deletes the last, and a track name's right-click menu renames or deletes that
  track (double-click or right-click Rename… to rename inline; Enter confirms,
  Escape cancels). Deleting a track with clips asks
  first; the clips on later tracks move up with their track.
- Add and delete mixer inserts: "+ Insert" in the mixer header adds one at the
  end (up to 125, the engine's limit), "−" deletes the last, and a strip's
  right-click menu deletes that insert. The inserts after a deleted one keep
  their settings and take the numbers one lower; channels routed to it play
  into the master (it asks first when there are some), and channels on later
  inserts follow theirs. The channel rack's insert menu follows the mixer.
- More step pads in the channel rack: the "+" after the last pad, or "+ Bar" in
  the header, adds a bar of 16 pads to every channel of the current pattern (up
  to 16 bars); "−" removes the last bar, asking first when it has steps on. Each
  pattern keeps its own length, which its loop and its playlist clips follow,
  and clone and split by channel keep it.
- Sample preview in the browser: clicking a file plays it through the master
  bus, even while the transport is stopped; clicking it again stops it, and so
  does Esc or Stop. With Auto preview (the speaker button next to search,
  remembered) the arrow keys play each file they reach. Files load on a worker
  thread and the last 16 stay cached, so moving through a big folder never
  stalls the UI; a file that cannot be read says why in the status bar.
- Patterns menu (after Edit), as in FL Studio: find the first or next empty
  pattern (Shift+F4, F4, Ctrl+F4 without naming), rename (F2), transpose every
  note, insert one (Shift+Ctrl+Ins), clone (Shift+Ctrl+C), delete
  (Shift+Ctrl+Del; asks first when the pattern has steps or notes, as there is no
  undo yet), move up / down (Shift+Ctrl+Up / Down) and split by channel.
  Playlist clips follow their pattern through every change. Colours and
  rendering a pattern to an audio clip say they arrive in M3.
- Edit menu between File and Options: Undo (Ctrl/Cmd+Z), Redo (Shift+Ctrl/Cmd+Z,
  also Ctrl+Y on Windows), Cut, Copy, Paste, Duplicate, Delete and Select All with
  their shortcuts. Like Open and Save, they say when they arrive (undo at the
  start of milestone M2, the rest with the M2 editing tools); a focused text
  field keeps its own copy, paste, undo and select-all keys.
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
- The pattern selector shows the project's pattern names: after deleting a
  pattern it showed renumbered names that no longer matched the panel titles,
  and deleting no longer renames the remaining patterns.
- A narrow window no longer turns the transport bar into two rows with the
  second one sometimes blank: groups that do not fit open from a » drop-down
  instead, the bar keeps its height and the displays stay centred while there
  is room. Shortcuts of the moved buttons (T, Ctrl/Cmd+M, F5-F9) keep working.
- Opening a big folder in the sample browser no longer freezes the app: folders
  are read and sorted on a background thread and their rows added at once (a
  folder of 20 000 samples now shows in about 50 ms instead of hanging the
  window for 2 s). Hidden folders and macOS "._" files stay hidden; a folder
  with more than 100 000 items shows the first 100 000 and says so.
- Browser search finds samples in every sub-folder, not only in folders that
  are open: matches show as one list (hover for the folder), drag onto the
  channel rack as before. It runs in the background once typing pauses, a new
  search cancels the previous one, and clearing it brings the folder tree
  back as it was.
- The app has its own icon (Windows executable, macOS bundle, window icon).
- No more UI stutter every 2 s while the chosen output device is missing: the
  app lists devices only when the OS reports a change, and on a background
  thread, instead of polling on the UI thread (each listing took 50-200 ms).
- macOS: detached panels (playlist, piano roll, mixer) no longer show a separate
  title bar: like the main window, the panel header takes its place, holds the
  window buttons and moves the window.
- Detached panel windows are named "VLOOM Mixer", "VLOOM Playlist" and "VLOOM
  Piano roll" in the taskbar, the Dock and window switchers.
- Detached panels show "VLOOM" before their title in the header, which is the
  window's title bar on every platform.
