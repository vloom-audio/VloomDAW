# Feature list

Everything VloomDAW has today, with how far each feature goes. The
[roadmap](../README.md#roadmap) lists what comes next; the
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
| Pitch knob | ⏳ | |
| Pattern / song mode | ⏳ | `L`; the channel rack always loops the current pattern, song mode arrives with the playlist in M3 |
| Record arm | 🎨 | `R`; keeps the armed state only, no audio input yet |
| Key LCD (root note and scale) | 🎨 | C .. B; major, minors, modes, pentatonics, blues, chromatic. Scale shading in the piano roll arrives in M2 |
| Pattern selector | 🎨 | Add, pick and delete patterns |
| CPU and RAM meter | ✅ | Engine load and process memory |

## Song editors

Editing is backed by a tested project model (`core/Project`). The channel
rack plays; notes play in M2 and playlist clips in M3. The playhead follows
the transport in all three editors.

| Feature | Status | Notes |
|---|---|---|
| Channel rack (`F6`) | ✅ | 16 step pads per channel: click or drag to paint, right-click to clear; mute light; add channels; double-click a name for its piano roll |
| Samples on channels | ✅ | Drop a WAV file from the browser or the desktop on a channel (below the last one for a new channel); edits apply while playing without cutting notes; channels without a sample are dimmed |
| Channel volume | ✅ | Knob per channel: drag (`Shift`: fine), scroll, double-click for 0 dB |
| Channel routing | ✅ | Insert box per channel: click to pick a mixer insert, or scroll; `-` plays straight into the master |
| Channel pan | ⏳ | The engine supports it; no control in the rack yet |
| Piano roll (`F7`) | 🎨 | Add, move, resize and delete notes; snap (`Alt`: off); `Ctrl+wheel` zoom; scale shading |
| Playlist (`F5`) | 🎨 | 16 tracks; place, move (`Shift`: snap to beats) and delete pattern clips |

## Mixer

| Feature | Status | Notes |
|---|---|---|
| Mixer panel (`F9`) | ✅ / 🎨 | Master and 16 insert tracks: pan, fader (unity at 80 %), dB readout, mute, solo |
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
| Loading and playing plugins | ⏳ | CLAP, then VST3 hosting in M4 |

## Browser

| Feature | Status | Notes |
|---|---|---|
| Sample browser | ✅ | Audio files (wav, aiff, flac, mp3, ogg) in your sample folders; add or remove folders; search by name (`Alt+F8`) |
| Drag samples onto the channel rack | ✅ | |
| Preview samples | ⏳ | |

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
| Narrow windows | ✅ | Transport items that do not fit go behind a » button |

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
| Light theme | ⏳ | |

## Projects and files

| Feature | Status | Notes |
|---|---|---|
| New project | ✅ | `Ctrl/Cmd+N`: stop, rewind, reset tempo |
| Open, Open Recent, Save, Save As, Import, Export | ⏳ | Menu items and shortcuts exist; project files arrive in M4, WAV export in M3 |

## Performance and diagnostics

| Feature | Status | Notes |
|---|---|---|
| Screen-rate refresh | ✅ | 30–240 Hz, matched to the screen; precise timer |
| Partial repaints | ✅ | Editors repaint only the strip the playhead covers |
| Performance overlay | ✅ | View menu, `Ctrl/Cmd+Shift+D`: fps, paint time, tick spacing, UI work and audio load |
| `VLOOM_REFRESH_HZ` | ✅ | Environment variable that forces a refresh rate for testing |
