# Architecture

## Layers

```
app/      VloomDAW (executable)      main(), object wiring
  │
ui/       vloomdaw_ui   (Qt)          windows, widgets — no audio or business logic
  │
audio/    vloomdaw_core (no Qt)       AudioController: vloom::Engine + audio device;
                                      project and mixer turned into engine state
core/     vloomdaw_core (no Qt)       pure logic: project and mixer models, plugin scan,
                                      device watch, formatting; later undo
probe/    vloomdaw-plugin-probe       child process that loads one plugin for its metadata
  │
vloom-engine                          real-time audio (separate repo)
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
TransportBar ──playToggled/tempoChanged──▶ MainWindow ──▶ Engine control API (queued)
     ▲                                                              │
     └──── setPosition/setPlaying ◀── screen-rate timer ◀── Engine atomics (play state, playhead)

ChannelRackView ──step/volume/insert──▶ Project ──ChannelRackAudio──▶ SequencerState snapshot
MixerPanel ──fader/pan/mute/solo──▶ MixerModel ──MixerAudio──▶ MixerState snapshot
     ▲                                                              │
     └──── meters ◀── screen-rate timer ◀── Engine peaks (output, per insert)
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

- `core/commands/`: every user edit as a Command, for undo / redo.
- Per-feature controllers that take the wiring out of `MainWindow`.
