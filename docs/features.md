# Feature list

Everything VloomDAW has today, with how far each feature goes. The
[roadmap](roadmap.md) lists what comes next and when each unfinished feature lands; the
[changelog](../CHANGELOG.md) lists changes in detail.

**Status**

| Mark | Meaning |
|---|---|
| ✅ | Works end to end |
| 🎨 | UI only: you can use and edit it, but no audio goes through it yet |
| ⏳ | Shown in the UI but not implemented yet; tells you the milestone it arrives in |

---

## Transport and timing

| Feature | Status | Notes |
|---|---|---|
| Play / pause, stop and return to start, go to start | ✅ | `Space`, `Shift+Space`, `Home` |
| Metronome | ✅ | `Ctrl/Cmd+M`; can be on at start-up (Settings > General) |
| Tempo LCD | ✅ | Drag (`Shift`: fine), scroll or double-click to type the BPM |
| Tap tempo | ✅ | `T`; averages the last four taps |
| Time signature display | ✅ | Next to the tempo |
| Song position LCD | ✅ | Bar · beat · tick (960 PPQ), or click for min : sec . ms |
| Master volume knob | ✅ | Drives the engine's master gain |
| Pitch knob | ⏳ | M2: transposes instruments |
| Pattern / song mode | ✅ | `L`: PAT loops the current pattern; SONG plays the playlist (pattern and audio clips) from the playhead, once — looping the song arrives in M3 |
| Record arm | 🎨 | `R`; keeps the armed state only; audio input in M6 |
| Key LCD (root note and scale) | ✅ | C .. B; major, minors, modes, pentatonics, blues, chromatic; shades the piano roll |
| Pattern selector | ✅ | Add, pick and delete patterns by name; the channel rack plays the selected one |
| Patterns menu | ✅ / ⏳ | Find first / next empty (`Shift+F4`, `F4`, `Ctrl+F4` without naming), Rename (`F2`), Transpose, Insert one (`Shift+Ctrl+Ins`), Clone (`Shift+Ctrl+C`), Delete (`Shift+Ctrl+Del`, asks when the pattern is not empty), Move up / down (`Shift+Ctrl+Up / Down`), Split by channel. Colours and rendering to an audio clip arrive in M3 |
| CPU and RAM meter | ✅ | Engine load and process memory |

## Song editors

Editing is backed by a tested project model (`core/Project`). The channel
rack plays; notes play in M2 and playlist clips in M3. The playhead follows
the transport in all three editors.

| Feature | Status | Notes |
|---|---|---|
| Channel rack (`F6`) | ✅ | 16 step pads per bar, 1-16 bars per pattern (+ after the last pad or + Bar / − in the header): click or drag to paint, right-click to clear; mute light; volume and pan knobs (drag, scroll, double-click to reset); add channels; double-click a name for its piano roll |
| Samples on channels | ✅ | Drop a WAV file from the browser or the desktop on a channel (below the last one for a new channel); edits apply while playing without cutting notes; channels without a sample are dimmed |
| Channel volume | ✅ | Knob per channel: drag (`Shift`: fine), scroll, double-click for 0 dB |
| Channel routing | ✅ | Insert box per channel: click to pick a mixer insert, or scroll; `-` plays straight into the master |
| Channel pan | ⏳ | M1.5: the engine supports it, no control in the rack yet |
| Piano roll (`F7`) | 🎨 | Add, move, resize and delete notes; snap menu (beat, 1/8, step, 1/32, 1/64; `Alt`: off); `Ctrl+wheel` zoom; scale shading. Plays in M2 |
| Playlist (`F5`) | 🎨 | One track in a new project, 1-500 with + Track / − in the header; double-click a track name (or right-click, Rename…) to rename it inline (Enter confirms, Escape cancels); right-click to move up / down or delete; place, move and delete pattern clips, snapped to the Snap menu (auto, bar, beat, step, 1/32; `Shift`: steps); drop WAV files (browser or desktop) for audio clips with a waveform, below the last track for a new one; each plays through a channel of the channel rack (made for the file, or the one already playing it), so its volume, pan, mute and insert apply. Dropping one turns song mode on. Plays in song mode |

## Mixer

| Feature | Status | Notes |
|---|---|---|
| Mixer panel (`F9`) | ✅ / 🎨 | Master and 16 insert tracks to start, 0-125 with + Insert / − in the header or Delete in a strip's right-click menu; double-click a name (or right-click, Rename…) to name an insert; right-click, Move Left / Right to reorder (channels follow their insert): pan, fader (unity at 80 %), dB readout, mute, solo |
| Master track | ✅ | Gain, pan and mute drive the engine; the meter shows the real output |
| Insert tracks | ✅ | Fader, pan, mute, solo and meter act on the channels routed to them |
| Peak meters | ✅ | Stereo, with peak hold and a clip light |

## Plugins

| Feature | Status | Notes |
|---|---|---|
| Plugins side panel | ✅ | Plugins found by the last scan, grouped by category (instruments split into Synth, Sampler, Drums, Keys…), vendor or format, with search over names and vendors |
| Plugin Manager | ✅ | Options > Plugin Manager…: default folders, plus folders you add or remove |
| Scan | ✅ | Only when you press Scan, never automatically. Runs in the background and the results are remembered across restarts |
| Vendor and category | ✅ | With "Read vendor and category" on, each new or changed plugin is loaded in a separate process (`vloomdaw-plugin-probe`); a plugin that crashes or hangs (20 s) is skipped and marked. Rescan All reads every plugin again |
| Your own categories | ✅ | Right-click a plugin to set its category; kept across scans |
| Loading and playing plugins | ✅ | CLAP and VST 3, loaded in the app's process (a plugin that crashes takes the app down). Drag a plugin from the Plugins panel onto the channel rack (an instrument: a new channel, or the one it is dropped on) or onto a mixer strip (an effect), or right-click it: Add to Channel Rack / Add to Mixer… |
| Instrument plugins | ✅ | A channel with a loaded instrument plays it instead of its sample: steps and piano-roll notes, through the channel's volume, pan and insert. Double-click the channel's name for the plugin's editor |
| Mixer effects | ✅ | Up to 10 per track, the master's included (the master's run on the whole mix, not the metronome or the browser preview). A strip's FX button lists them: open the editor, bypass, remove. Plugin latency is compensated, also when a plugin changes it while running (oversampling, linear phase) |
| Plugin editors | ✅ | The plugin's own window, sized as it asks and resizable when it allows. Windows and macOS; on Linux X11 only (start with `QT_QPA_PLATFORM=xcb` under Wayland) |
| Generic controls | ⏳ | Knobs for plugins without an editor of their own |
| Plugins in the project file | ⏳ | Arrives with the File menu (#19) |

## Browser

| Feature | Status | Notes |
|---|---|---|
| Sample browser | ✅ | Audio files (wav, aiff, flac, mp3, ogg) in your sample folders; add or remove folders; search by name (`Alt+F8`) |
| Refresh | ✅ | ⟳ button, or `F5` in the list: reads the folder again; open folders stay open, the selection and scroll stay |
| Drag samples onto the channel rack | ✅ | |
| Preview samples | ✅ | M1.5 |

## Windows and layout

| Feature | Status | Notes |
|---|---|---|
| Sidebar | ✅ | Icon strip (with hover labels) opens a resizable side panel: Browser, Plugins |
| View buttons | ✅ | Playlist, channel rack, piano roll, mixer and browser on the transport bar |
| Detachable panels | ✅ | Mixer, piano roll and playlist open in their own window and dock back; position remembered |
| Frameless windows (Windows) | ✅ | No native title bar; window buttons in the menu bar row; drag empty bar space to move; Aero Snap still works |
| macOS window | ✅ | Tool bar in the title bar row; full screen keeps the window buttons |
| Detached panel windows | ✅ | No separate title bar on Windows or macOS: the panel header reads "VLOOM MIXER" etc., holds the window buttons and moves the window; named "VLOOM Mixer" etc. in the taskbar and the Dock |
| Window size and position remembered | ✅ | Also each detached window and the open sidebar page |
| Narrow windows | ✅ | Transport groups that do not fit open from a » drop-down (CPU / RAM first, then pitch, tap / metronome, views, volume, key, patterns); the bar keeps one row and its shortcuts keep working |

## Settings and theme

| Feature | Status | Notes |
|---|---|---|
| Settings window (`Ctrl/Cmd+,`) | ✅ | Pages: General, Audio, Theme |
| Audio settings | ✅ | Output device, sample rate, buffer size and latency; device remembered by name |
| Unplugged device | ✅ | Playback moves to the system default at once, the status bar says so, and it switches back when your device returns |
| Output device switcher in the status bar | ✅ | Click the device name |
| Theme presets | ✅ | Seven dark presets: Midnight, Graphite, Deep Black, Slate, Forest, Plum, Ember |
| Theme adjustments | ✅ | Hue, saturation, brightness, contrast and text knobs; changes show at once |
| Colour per role | ✅ | Selection, playing, record, LCD and text, with a colour picker |
| Contrast warning | ✅ | Warns when the theme drops below WCAG AA |
| Themed icons | ✅ | SVG icons take the theme's colours |
| Light theme | ⏳ | Any time (UI only) |

## Projects and files

| Feature | Status | Notes |
|---|---|---|
| New project | ✅ | `Ctrl/Cmd+N`: stop, rewind, reset tempo |
| Edit menu: Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select All | ⏳ | Menu and shortcuts exist (`Ctrl/Cmd+Z`, `Shift+Ctrl/Cmd+Z`, `Ctrl+Y` on Windows, …) and say when they arrive: undo at the start of M2, the rest with the M2 editing tools. Text fields keep their own keys |
| New, Open, Open Recent, Save, Save As | ✅ | `.vloom` project files ([format](project-file.md)): channels, patterns, playlist, mixer, plugins and their settings, song tempo and key. Unsaved changes show as "*" in the title and are asked about before New, Open and quitting. Opening a project loads only plugins your scan found |
| Import Audio | ✅ | File > Import Audio adds audio files as new channels |
| Export WAV | ✅ | File > Export WAV renders the song (or the pattern, with an empty playlist) to 24-bit, 16-bit or 32-bit float WAV, plugins included, with progress and cancel; audio output pauses meanwhile |

## Performance and diagnostics

| Feature | Status | Notes |
|---|---|---|
| Screen-rate refresh | ✅ | 30–240 Hz, matched to the screen; precise timer |
| Partial repaints | ✅ | Editors repaint only the strip the playhead covers |
| Performance overlay | ✅ | View menu, `Ctrl/Cmd+Shift+D`: fps, paint time, tick spacing, UI work and audio load |
| `VLOOM_REFRESH_HZ` | ✅ | Environment variable that forces a refresh rate for testing |
