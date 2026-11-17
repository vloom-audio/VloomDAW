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
| Metronome | ✅ | `Ctrl/Cmd+M`; off by default at start-up (can be turned on in Settings > General) |
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
| Piano roll (`F7`) | ✅ | Notes play, in pattern and song mode, through the channel's sample or instrument, each as hard as its velocity. Studio One-style header toolbar: Channel switcher + `Inst` VSTi button, `Select (E)` (default: plain left-drag marquee-selects notes, double-click empty space inserts a note), `Draw (P)`, `Paint (B)` (horizontal step repeat), `Slice (C)` (cut line note splitter), `Delete (D)`, `Playback (Y)` + top ruler click/drag scrubbing, `-Oct / -1 / +1 / +Oct` pitch transpose buttons, `Action ▾` menu (`Transpose Custom…`, `Quantize`, `Legato`, `Snap Pitch to Scale`, `Velocity ±15%`), Snap menu + `Snap` toggle + `Q` Quantize + `Audition` speaker toggle + `Follow` playhead + `- / +` zoom. Drag selected notes up/down with live pitch tooltip (`Pitch: +3 st · D#5`), or transpose with `Up / Down` (`Shift+Up / Down`: octave) or `Alt+Wheel` (`Alt+Shift+Wheel`: octave); note names render inside note bars and switching channels auto-centers on the channel's octave |
| Playlist (`F5`) | ✅ | Empty (0 tracks) in a new project, 0-500 with `+ Track` (`Ctrl+T` opens `Add Tracks` dialog for Audio or Instrument tracks with count, pack folder, colour, FX chain and routing) or right-clicking empty header/grid space; Studio One-style **Loop Bar** along the top of the timeline ruler (drag top lane or `Ctrl+drag` to set range, drag left/right flags or body, click to toggle active, `Ctrl+L` to loop selection/song, right-click `Export Loop Range…`); drag track headers vertically (or right-click `Move to Top` / `Move to Bottom`) to reorder tracks in 1:1 lockstep with Mixer strips; double-click a track name to rename inline; right-click to delete (synced with its dedicated Mixer strip); click or drag the time bar ruler to scrub playback directly (or use the Playback tool `Y`); Pattern clips render miniature MIDI notes and step ticks inside each clip (double-click to open in Piano Roll); drop `.vloom` projects, audio files, Standard MIDI Files (`.mid` / `.midi`, creating one empty MIDI track + pattern + Mixer insert per track ready for your chosen VSTi), or instrument plugins directly onto the Playlist |

| Stem extractor | ✅ | Right-click an audio clip, Stem Extractor…: its sample is split into stems (drums, bass, other, vocals; guitar and piano with the six-stem model) on new tracks under the clip, which is muted. Options > Stem Models… is the model store: download the Demucs model you want (from huggingface.co, checked against its SHA-256) and pick the one in use; it then works offline. **Windows:** HT Demucs runs on the graphics card through ONNX Runtime and DirectML (any DirectX 12 card: NVIDIA, AMD, Intel; about a quarter of the audio's length on a GTX 1650), and falls back to the processor by itself when there is no usable card, the card fails, or its result does not add up to the song; HT Demucs 6 and FT run on the processor through ONNX Runtime. Four MDX-Net models from Ultimate Vocal Remover (from its releases on github.com) give one stem and the rest of the song: Vocals, Instrumental and Karaoke run on the graphics card, De-Reverb on the processor. **Everywhere:** the "compact" models run on the processor with demucs.cpp (AVX2 on x86-64), several times the audio's length. Windows builds ship `DirectML.dll`, which is under Microsoft's licence (free to redistribute, not open source) |

## Mixer

| Feature | Status | Notes |
|---|---|---|
| Mixer panel (`F9`) | ✅ | Master (`Main`) and 0-125 console strips (`Standard`, `Bus`, `FX`, `Aux`), added via `+ Bus / + FX / + Aux / + Channel` or right-clicking empty space; draggable `Inserts` FX rack height (0–9 visible rows, remembered across sessions), `Sends` rack (up to 8 pre/post-fader sends per strip), submix output routing (`Main`, `Bus`, `Aux`), `Stereo / Mono` DSP summing toggle on inserts and Master, `<C>` bipolar pan bar, fader (unity at 80 %), dB readout, mute, solo, and double-click inline rename |
| Master track | ✅ | Gain, pan, mute, `Stereo / Mono` summing and master FX drive the engine; the meter shows the real output |
| Insert tracks | ✅ | Fader, pan, mute, solo, `Stereo / Mono` summing, FX slots, sends, submix output and meter act on the channels and buses routed to them |
| Peak meters | ✅ | Stereo, with peak hold and a clip light |

## Plugins

| Feature | Status | Notes |
|---|---|---|
| Plugins side panel | ✅ | Plugins found by the last scan, grouped by category (instruments split into Synth, Sampler, Drums, Keys…), vendor or format, with search over names and vendors |
| Plugin Manager | ✅ | Options > Plugin Manager…: default folders, plus folders you add or remove |
| Scan | ✅ | Only when you press Scan, never automatically. Runs in the background and the results are remembered across restarts |
| Vendor and category | ✅ | With "Read vendor and category" on, each new or changed plugin is loaded in a separate process (`vloomdaw-plugin-probe`); a plugin that crashes or hangs (20 s) is skipped and marked. Rescan All reads every plugin again |
| Your own categories | ✅ | Right-click a plugin to set its category; kept across scans |
| Loading and playing plugins | ✅ | CLAP, VST 3 and VST 2 (`vloom-engine` ADR 0010/0011; 32-bit Windows VST 2 plugins run automatically in `vloom-plugin-bridge32.exe` out of process, preserving their last saved settings if a bridge process exits). Drag a plugin from the Plugins panel onto the channel rack, playlist or mixer strip, or right-click it: Add to Channel Rack / Add to Mixer… |
| Instrument plugins | ✅ | A channel with a loaded instrument plays it instead of its sample: steps and piano-roll notes, through the channel's volume, pan and insert. Double-click the channel's name for the plugin's editor |
| Built-in synth | ✅ | A wavetable synthesizer that comes with the app, in the instrument lists: two wavetable oscillators (four tables, unison), sub, noise, a filter with drive, three envelopes, four LFOs, four macros, 16 voices or mono with legato and glide. Its editor shows each table, the filter response and the envelope and LFO shapes, has a keyboard to play it by hand and a Matrix page of what moves what |
| Mixer effects | ✅ | Built-in effects come with the app (EQ: low and high cut, low and high shelf, two bells; Gain), listed with the scanned plugins. The EQ opens as its response curve with a point per section to drag (wheel: Q, double-click: flat or switch a cut) over the spectrum of its output, with presets (the app's and your own) and A/B compare; the others get a knob per parameter. Up to 9 per track, the master's included (the master's run on the whole mix, not the metronome or the browser preview). A strip's FX button lists them: open the editor, bypass, remove. Plugin latency is compensated, also when a plugin changes it while running (oversampling, linear phase) |
| Plugin editors | ✅ | The plugin's own window, sized as it asks and resizable when it allows. Windows and macOS; on Linux X11 only (start with `QT_QPA_PLATFORM=xcb` under Wayland) |
| Generic controls | ⏳ | Knobs for plugins without an editor of their own |
| Plugins in the project file | ✅ | A project saves every plugin (channel instruments, insert and master effects) with its settings, and loads them again when it opens. Only plugins your scan found are loaded; the others stay in the project and are listed when it opens |

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
| Settings window (`Ctrl/Cmd+,`) | ✅ | Pages: General, Audio, Theme, Stem Extractor (model in use, model store, graphics card, overlap of 25, 50 or 75 %, mute the clip after extracting) |
| Audio settings | ✅ | Output device, sample rate, buffer size and latency; device remembered by name |
| Unplugged device | ✅ | Playback moves to the system default at once, the status bar says so, and it switches back when your device returns |
| Output device switcher in the status bar | ✅ | Click the device name |
| Theme presets | ✅ | Seven dark presets: Midnight, Graphite, Deep Black, Slate, Forest, Plum, Ember |
| Theme adjustments | ✅ | Hue, saturation, brightness, contrast and text knobs; changes show at once |
| Colour per role | ✅ | Selection, playing, record, LCD and text, with a colour picker |
| Contrast warning | ✅ | Warns when the theme drops below WCAG AA |
| Themed icons | ✅ | SVG icons take the theme's colours |
| Light theme | ⏳ | Any time (UI only) |
| Automatic updates | ✅ | Once a day, `Help > Check for Updates…` or `Settings > General`. Shows the release notes; `Install and Restart` downloads the update, checks its minisign signature and SHA-256, offers to save the project and restarts into the new version (Windows installer, macOS app, Linux AppImage). Optional beta channel; a version can be skipped. Needs the signing key in the build ([releasing](releasing.md#signing-the-update)) |

## Projects and files

| Feature | Status | Notes |
|---|---|---|
| New project | ✅ | `Ctrl/Cmd+N`: stop, rewind, reset tempo |
| Edit menu: Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select All, Quantize | ✅ / ⏳ | Work on the piano roll's notes when it is the editor in front (`Ctrl/Cmd+Z`, `Shift+Ctrl/Cmd+Z`, `Ctrl+Y` on Windows, …). Undo covers note edits only; the other editors get these commands in M2. Text fields keep their own keys |
| New, Open, Open Recent, Save, Save As, Close Project | ✅ | `.vloom` project files ([format](project-file.md)): channels, patterns, playlist, Loop Bar markers, mixer, plugins and their settings, song tempo and key. Double-clicking a `.vloom` file in Windows Explorer opens it directly in VloomDAW with a custom Vloom project document icon (configurable in `Settings > General`). Unsaved changes show as "*" in the title and are asked about before New, Open, Close Project and quitting. `File > Close Project` (`Ctrl/Cmd+W`, or the Home button on the sidebar rail) unloads the current project and returns to the standalone Start Hub without exiting VloomDAW |
| Start Hub | ✅ | Shown at startup (can be turned off), after `Close Project`, and from `File > Start Hub…`. Features Vloom brand header, quick-tap BPM presets (`90`, `120`, `140`, `160`), live Audio Setup card (`kHz`, buffer, latency, `Configure Audio Device…`), and recent project cards with arrangement previews, tempo, length and plugin count. Pin, search and filter (all, pinned, this week); right-click to show its folder or remove it from the list |
| Import Audio & MIDI | ✅ | `File > Import Audio…` (`Ctrl+I`) adds audio files as new channels; `File > Import MIDI…` (`Ctrl+Shift+M`), `File > Open…`, or dragging `.mid` / `.midi` files onto the Playlist imports all MIDI tracks (Format 0, 1, 2, tempo and PPQ scaling) into dedicated tracks, patterns and Mixer strips without auto-loading instruments |
| Export | ✅ | File > Export… (`Ctrl+E`) renders the full song (or only the **Loop Bar range**, or the current pattern when the playlist is empty), plugins included, with progress and cancel; audio output pauses meanwhile. Formats: WAV (16-bit, 24-bit, 32-bit float), FLAC (16 or 24-bit), MP3, Ogg Vorbis and M4A (AAC) at 96 to 320 kbps; stereo or mono; dithering; with or without the tail after the end; the choices are remembered. Tags (title, artist, album, year, genre, comment) are written into every format: ID3 for MP3, Vorbis comments for FLAC and Ogg, MP4 atoms for M4A, a LIST/INFO chunk for WAV; the title starts as the project's name, artist and genre are remembered. MP3 and Ogg need an FFmpeg with LAME and libvorbis: the Windows build and system FFmpeg have them, the static macOS and Linux release builds do not (greyed out there) |

## Performance and diagnostics

| Feature | Status | Notes |
|---|---|---|
| Screen-rate refresh | ✅ | 30–240 Hz, matched to the screen; precise timer |
| Partial repaints | ✅ | Editors repaint only the strip the playhead covers |
| Performance overlay | ✅ | View menu, `Ctrl/Cmd+Shift+D`: fps, paint time, tick spacing, UI work and audio load |
| `VLOOM_REFRESH_HZ` | ✅ | Environment variable that forces a refresh rate for testing |
