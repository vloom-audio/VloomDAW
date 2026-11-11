# VloomDAW

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-brightgreen.svg)](#requirements)
[![Framework: Qt 6](https://img.shields.io/badge/Qt-6.5%2B-green.svg)](https://www.qt.io/)
[![Platforms](https://img.shields.io/badge/Platforms-macOS%20|%20Windows%20|%20Linux-lightgrey.svg)](#quick-start)
[![Status: M0](https://img.shields.io/badge/Status-M0%20(First%20Sound)-orange.svg)](#roadmap)

**VloomDAW** is a free, open-source, pattern-based digital audio workstation for
macOS, Windows and Linux. It follows the fast pattern workflow of FL Studio and
aims to bring step sequencing, a channel rack, a piano roll, a pattern playlist
and a mixer to open source.

The audio engine lives in its own repository,
[vloom-engine](https://github.com/vloom-audio/vloom-engine), and has no UI.

> **Current milestone: M0 — first sound.** A window with a transport, a
> metronome, an LCD-style tempo and position display, a sample browser and audio
> settings.

---

## Features (M0)

- **Transport bar**
  - Play / pause, stop (returns to the start) and go to start.
  - Metronome on / off.
- **LCD displays**
  - **Tempo:** drag up / down to change the BPM (`Shift` for fine steps), scroll,
    or double-click to type a value. The time signature is shown next to it.
  - **Song position:** bar · beat · tick (960 PPQ), or click to switch to
    min : sec . ms.
  - **Tap tempo** (`T`): averages the last four intervals between taps.
- **Sidebar and sample browser**
  - An icon strip opens a resizable side panel.
  - The browser lists audio files (wav, aiff, flac, mp3, ogg) in your sample
    folders; add or remove folders and search by name.
- **Settings** (`Cmd/Ctrl + ,`)
  - Audio: output device, sample rate, buffer size, and the latency one buffer
    adds. The chosen device is remembered, even when two devices share a name.
  - General: which position format and metronome state to start with.
- **Dark theme**
  - All colours come from one palette (`ui/Theme`); icons are SVG.
  - macOS: the tool bar sits in the title bar row, full screen keeps the window
    buttons, and the window size and position are remembered.

---

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| <kbd>Space</kbd> | Play / pause |
| <kbd>Shift</kbd> + <kbd>Space</kbd> | Stop and return to the start |
| <kbd>Home</kbd> | Go to the start |
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
│   ├── audio/            # AudioController: owns vloom::Engine and the device (no Qt)
│   ├── core/             # Logic without Qt: formatting, tap tempo, file filters…
│   └── ui/               # Qt Widgets (target vloomdaw_ui)
│       ├── MainWindow    # The main window; connects widgets to the engine
│       ├── Theme         # Colours and style sheet
│       ├── browser/      # Sample browser panel
│       ├── dialogs/      # Settings window
│       └── widgets/      # Reusable widgets, one folder each (TransportBar, LcdDisplay, …)
├── resources/icons/      # SVG icons (sources and licences in its README)
├── tests/                # Catch2 tests for core/ (no Qt, no display)
├── cmake/                # Build helpers; dependencies in Dependencies.cmake
├── docs/                 # Getting started, architecture, UI guidelines
└── scripts/              # bootstrap.sh, format.sh
```

---

## Roadmap

| Milestone | Deliverables | Status |
|:---:|---|:---:|
| **M0** | Window, audio device settings, transport, metronome, sample browser | *In review* |
| **M1** | Channel rack and step sequencer with sample playback | *Next* |
| **M2** | Piano roll and a first built-in synth | *Planned* |
| **M3** | Playlist (patterns on a timeline), mixer, WAV export | *Planned* |
| **M4** | CLAP, then VST3 plugin hosting; project save / load → first alpha | *Planned* |

---

## Documentation

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
