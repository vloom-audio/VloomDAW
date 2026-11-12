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
| Pattern / song mode | ⏳ | `L` |
| Record arm | 🎨 | `R`; keeps the armed state only, no audio input yet |
| Key LCD (root note and scale) | 🎨 | C .. B; major, minors, modes, pentatonics, blues, chromatic. Scale shading in the piano roll arrives in M2 |
| Pattern selector | 🎨 | Add, pick and delete patterns |
| CPU and RAM meter | ✅ | Engine load and process memory |

## Song editors

Editing works and is backed by a tested project model (`core/Project`);
nothing plays the patterns, notes or clips yet (M1–M3). The playhead follows
the transport in all three editors.

| Feature | Status | Notes |
|---|---|---|
| Channel rack (`F6`) | 🎨 | 16 step pads per channel: click or drag to paint, right-click to clear; mute light; add channels; double-click a name for its piano roll |
| Piano roll (`F7`) | 🎨 | Add, move, resize and delete notes; snap (`Alt`: off); `Ctrl+wheel` zoom; scale shading |
| Playlist (`F5`) | 🎨 | 16 tracks; place, move (`Shift`: snap to beats) and delete pattern clips |

## Mixer

| Feature | Status | Notes |
|---|---|---|
| Mixer panel (`F9`) | ✅ / 🎨 | Master and 16 insert tracks: pan, fader (unity at 80 %), dB readout, mute, solo |
| Master track | ✅ | Gain, pan and mute drive the engine; the meter shows the real output |
| Insert tracks | 🎨 | Get audio when channels play (M1) |
| Peak meters | ✅ | Stereo, with peak hold and a clip light |

## Plugins

| Feature | Status | Notes |
|---|---|---|
| Plugins side panel | ✅ | Plugins found by the last scan, grouped as VST3 / CLAP, with search |
| Plugin Manager | ✅ | Options > Plugin Manager…: default folders, plus folders you add or remove |
| Scan | ✅ | Only when you press Scan, never automatically. Runs in the background, reads file names only (no plugin code is loaded), and the results are remembered across restarts |
| Loading and playing plugins | ⏳ | CLAP, then VST3 hosting in M4 |

## Browser

| Feature | Status | Notes |
|---|---|---|
| Sample browser | ✅ | Audio files (wav, aiff, flac, mp3, ogg) in your sample folders; add or remove folders; search by name (`Alt+F8`) |
| Preview or drag samples into the project | ⏳ | M1 |

## Windows and layout

| Feature | Status | Notes |
|---|---|---|
| Sidebar | ✅ | Icon strip (with hover labels) opens a resizable side panel: Browser, Plugins |
| View buttons | ✅ | Playlist, channel rack, piano roll, mixer and browser on the transport bar |
| Detachable panels | ✅ | Mixer, piano roll and playlist open in their own window and dock back; position remembered |
| Frameless windows (Windows) | ✅ | No native title bar; window buttons in the menu bar row; drag empty bar space to move; Aero Snap still works |
| macOS window | ✅ | Tool bar in the title bar row; full screen keeps the window buttons |
| Window size and position remembered | ✅ | Also each detached window and the open sidebar page |
| Narrow windows | ✅ | Transport items that do not fit go behind a » button |

## Settings and theme

| Feature | Status | Notes |
|---|---|---|
| Settings window (`Ctrl/Cmd+,`) | ✅ | Pages: General, Audio, Theme |
| Audio settings | ✅ | Output device, sample rate, buffer size and latency; device remembered by name |
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
