# Architecture

## Layers

```
app/      VloomDAW (executable)      main(), Start Hub & Settings lifecycle, object wiring
  │
ui/       vloomdaw_ui   (Qt)          windows, editors, mixer, dialogs — no audio or business logic
  │
audio/    vloomdaw_core (no Qt)       AudioController: vloom::Engine + audio/MIDI devices,
                                      lock-free Audio I/O matrix routing & engine state builders
core/     vloomdaw_core (no Qt)       pure logic: Project, MixerModel, AudioIoSetup, UndoHistory,
                                      NoteHistory, MidiFile, PluginCatalog, StemModels, UpdateFeed
probe/    vloomdaw-plugin-probe       child process that loads one VST3/CLAP/VST2 plugin for metadata
stems/    vloomdaw-stems              child process running ONNX stem separation out of process
  │
vloom-engine (+ vloom-plugin-bridge)  real-time audio, recording, VST3/CLAP & bridged VST2 hosting
```

Rules:

1. **Dependencies point down only.** `core/` and `audio/` never include Qt.
   `ui/` never talks to the audio thread directly. It calls `AudioController`
   and the engine's control API.
2. **Logic goes in core, not in widgets.** If it can be tested without a window
   (formatting, snapping, note maths, undo), it belongs in `core/` with a test.
3. **Widgets are views.** A widget emits signals for what the user did and has
   setters for what it shows. `MainWindow` (later: per-feature controllers)
   connects widgets to the model and engine.

## Data flow

```
TransportBar ──play/record/tempo/loop──▶ MainWindow ──▶ Engine control API (queued)
     ▲                                                              │
     └──── setPosition/setPlaying/setRecording ◀── screen-rate timer ◀── Engine atomics

ChannelRackView / PianoRollView ──notes/controls/routing──▶ Project ──ChannelRackAudio──▶ SequencerState
PlaylistView ──clips/automation/record-arm──▶ Project ──PlaylistAudio──▶ SongState
MixerPanel ──fader/pan/mute/solo/sends/sidechain──▶ MixerModel ──MixerAudio──▶ MixerState
AudioIoDialog ──Mono/Stereo bus matrix──▶ AudioIoSetup ──AudioController──▶ lock-free atomic channel map
     ▲                                                              │
     └──── meters & live waveform ◀── screen-rate timer ◀── Engine peaks (output, input, per insert)
```

Edits never wait for the audio thread: `ChannelRackAudio` and `MixerAudio`
build immutable engine state from the models and publish it; samples are
loaded on the control thread before they are published.

The UI never blocks on the audio thread and the audio thread never waits for
the UI. See the engine's
[architecture](https://github.com/vloom-audio/vloom-engine/blob/main/docs/architecture.md)
for the threading rules.

## Where does my code go?

| You are adding… | Folder | Target |
|---|---|---|
| A new panel (channel rack, piano roll, mixer) | `ui/<feature>/` | vloomdaw_ui |
| A small reusable widget (knob, meter) | `ui/widgets/<Name>/` | vloomdaw_ui |
| Colours, fonts, widget styling | `ui/Theme.cpp` | vloomdaw_ui |
| A side panel (browser, …) | `ui/<feature>/`, registered with `ActivityBar` in `MainWindow::createCentralArea()` | vloomdaw_ui |
| A dialog | `ui/dialogs/<Name>/` | vloomdaw_ui |
| Icons | `resources/icons/*.svg` | vloomdaw_ui (Qt resource) |
| Project data, commands, undo | `core/` (+ tests) | vloomdaw_core |
| Engine set-up, device settings | `audio/` | vloomdaw_core |
| Audio processing | **vloom-engine**, not here | — |

Add new files to `src/CMakeLists.txt` under the right block and tests to
`tests/CMakeLists.txt`.

## Planned

- Per-feature controllers that decompose wiring out of `MainWindow`.
- Autosave & crash recovery journal on top of `UndoHistory` and `.vloom` snapshots.
