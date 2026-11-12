# VloomDAW

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-brightgreen.svg)](#requirements)
[![Framework: Qt 6](https://img.shields.io/badge/Qt-6.5%2B-green.svg)](https://www.qt.io/)
[![Platforms](https://img.shields.io/badge/Platforms-macOS%20|%20Windows%20|%20Linux-lightgrey.svg)](#quick-start)
[![Status: M1](https://img.shields.io/badge/Status-M1%20(Channel%20Rack)-orange.svg)](#roadmap)

**VloomDAW** is a free, open-source, pattern-based digital audio workstation for
macOS, Windows and Linux. It follows the fast pattern workflow of FL Studio and
aims to bring step sequencing, a channel rack, a piano roll, a pattern playlist
and a mixer to open source.

The audio engine lives in its own repository,
[vloom-engine](https://github.com/vloom-audio/vloom-engine), and has no UI.

> **Current milestone: M1 — channel rack.** Drop WAV samples on the channel
> rack and its step patterns play through the engine, each channel into the
> mixer insert you pick. The piano roll and playlist can already be edited;
> they get audio in M2 and M3.

---

## Features

Highlights. The **[feature list](docs/features.md)** has everything, with how far
each feature goes (works / UI only / planned).

- **Transport:** play, stop, metronome, LCD tempo and song position, tap tempo,
  master volume, and a key display (root note and scale).
- **Channel rack:** drop a WAV file on a channel and its 16 step pads play it;
  each channel has a volume knob and picks the mixer insert it plays into.
  Edits apply while playing without cutting notes.
- **Song editors** (editing only for now): piano roll (audio in M2) and a
  pattern playlist (M3).
- **Mixer:** master and 16 insert tracks with pan, fader, mute, solo and peak
  meters, all acting on the sound.
- **Audio output:** WASAPI, CoreAudio or ALSA through the engine; when the
  device is unplugged, playback moves to the system default and comes back
  when it returns.
- **Plugins:** the Plugin Manager scans your VST3 and CLAP folders when you press
  Scan, never automatically. It can read each plugin's vendor and category in a
  separate process, so a crashing plugin cannot take the app down; the sidebar
  groups them by category, vendor or format. Plugins are not played yet (M4).
- **Sample browser** for the audio files in your sample folders; drag samples
  onto the channel rack.
- **Window layout:** the mixer, piano roll and playlist detach into their own windows.
  No separate title bar on Windows or macOS: the panel header holds the window
  buttons, and windows still snap and resize natively.
- **Settings and themes:** audio device and format; a theme editor with seven
  dark presets, colour adjustments and a contrast check.
- **Performance overlay** (`Ctrl/Cmd+Shift+D`) and a refresh rate that follows the
  screen (up to 240 Hz).

---

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| <kbd>Space</kbd> | Play / pause |
| <kbd>Shift</kbd> + <kbd>Space</kbd> | Stop and return to the start |
| <kbd>Home</kbd> | Go to the start |
| <kbd>R</kbd> | Arm / disarm recording (no audio input yet) |
| <kbd>L</kbd> | Pattern / song mode |
| <kbd>F5</kbd> / <kbd>F6</kbd> / <kbd>F7</kbd> / <kbd>F9</kbd> | Playlist / Channel rack / Piano roll / Mixer (the piano roll plays in M2, the playlist in M3) |
| <kbd>Alt</kbd> + <kbd>F8</kbd> | Show / hide the browser |
| <kbd>Ctrl</kbd> / <kbd>Cmd</kbd> + <kbd>M</kbd> | Metronome on / off |
| <kbd>T</kbd> | Tap tempo |
| <kbd>Ctrl</kbd> / <kbd>Cmd</kbd> + <kbd>,</kbd> | Settings |
| <kbd>Ctrl</kbd> + <kbd>Cmd</kbd> + <kbd>F</kbd> (macOS) | Full screen |

---

## Requirements

- CMake ≥ 3.22 and Git
- A C++20 compiler: Apple Clang 15+ (macOS), GCC 12+ (Linux), MSVC 2022 (Windows)
- **Qt ≥ 6.5** with the Widgets and Svg modules
- [vloom-engine](https://github.com/vloom-audio/vloom-engine) cloned next to this
  repository (`scripts/bootstrap.sh` does it for you)

---

## Quick start

### 1. Set up the workspace

```sh
mkdir vloom-audio && cd vloom-audio
git clone https://github.com/vloom-audio/VloomDAW.git
cd VloomDAW

# Clones ../vloom-engine if it is missing and tells you if QT_DIR is not set
scripts/bootstrap.sh
```

### 2. Configure and build

Point `QT_DIR` at your Qt 6 install, e.g. `export QT_DIR=$(brew --prefix qt)` on macOS:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

*No Qt yet?* The Qt-free core and its tests build with the `core` preset:

```sh
cmake --preset core
cmake --build --preset core
ctest --preset core
```

### 3. Run it

- **macOS:** `open build/dev/src/VloomDAW.app`
- **Linux / Windows:** `./build/dev/src/VloomDAW`

Per-platform details: **[docs/getting-started.md](docs/getting-started.md)**.

---

## Repository layout

```
VloomDAW/
├── src/
│   ├── app/              # main(): creates the AudioController and the window
│   ├── audio/            # AudioController (engine + device); project and mixer → engine state (no Qt)
│   ├── core/             # Logic without Qt: project model, mixer model, plugin scan, formatting…
│   ├── probe/            # vloomdaw-plugin-probe: reads one plugin's metadata in a child process
│   └── ui/               # Qt Widgets (target vloomdaw_ui)
│       ├── MainWindow    # The main window; connects widgets to the engine
│       ├── Theme         # Colours and style sheet
│       ├── browser/      # Sample browser panel
│       ├── channelrack/  # Channel rack (steps, volume, insert)
│       ├── mixer/        # Mixer panel and strips
│       ├── pianoroll/    # Piano roll
│       ├── playlist/     # Playlist
│       ├── plugins/      # Plugins side panel and Plugin Manager
│       ├── dialogs/      # Settings window
│       └── widgets/      # Reusable widgets, one folder each (TransportBar, LcdDisplay, …)
├── resources/icons/      # SVG icons (sources and licences in its README)
├── tests/                # Catch2 tests for core/ and audio/ (no Qt, no display), probe tests
├── cmake/                # Build helpers; dependencies in Dependencies.cmake
├── docs/                 # Getting started, architecture, UI guidelines
└── scripts/              # bootstrap.sh, format.sh
```

---

## Roadmap

| Milestone | Deliverables | Status |
|:---:|---|:---:|
| **M0** | Window, audio device settings, transport, metronome, sample browser | *Done* |
| **M1** | Channel rack and step sequencer with sample playback; mixer routing to inserts | *Done* |
| **M1.5** | Channel pan, sample preview | *Next* |
| **M2** | Piano roll, first built-in synth, undo / redo | *Planned* |
| **M3** | Playlist (patterns on a timeline), song mode, WAV export | *Planned* |
| **M4** | Project save / load, CLAP hosting, mixer effect slots → first alpha | *Planned* |
| **M5** | VST3 hosting, built-in effects, sends | *Planned* |
| **M6** | Audio recording, MIDI input | *Planned* |

Every unfinished feature, the milestone it lands in and what "done" means:
**[docs/roadmap.md](docs/roadmap.md)**.

---

## Documentation

- **[Feature list](docs/features.md)**: everything VloomDAW has today, and how far each feature goes.
- **[Roadmap](docs/roadmap.md)**: the plan until every feature works.
- **[Getting started](docs/getting-started.md)**: tools, Qt setup, IDE setup.
- **[Architecture](docs/architecture.md)**: how the UI, AudioController and engine fit together.
- **[UI guidelines](docs/ui-guidelines.md)**: widget conventions, theme and icons.
- **[Engine documentation](https://github.com/vloom-audio/vloom-engine/tree/main/docs)**: audio thread rules, DSP, timing.

---

## Support the project

VloomDAW is free software and will stay open source. If you would like to
support its development, you can [sponsor Vloom Audio](https://github.com/sponsors/vloom-audio).

---

## Contributing

Code, UI improvements and bug reports are welcome. Please read the
[contributing guide](https://github.com/vloom-audio/.github/blob/main/CONTRIBUTING.md)
before opening a pull request.

---

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
