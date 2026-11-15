<div align="center">

# VloomDAW

**A modern, pattern-centric Digital Audio Workstation for macOS, Windows, and Linux.**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-7c3aed.svg?style=flat-square)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-ec4899.svg?style=flat-square)](#system-requirements)
[![Framework: Qt 6](https://img.shields.io/badge/Qt-6.5%2B-10b981.svg?style=flat-square)](https://www.qt.io/)
[![Platforms](https://img.shields.io/badge/Platforms-macOS%20%7C%20Windows%20%7C%20Linux-475569.svg?style=flat-square)](#quick-start)
[![Milestone: M1](https://img.shields.io/badge/Milestone-M1%20(Channel%20Rack)-f59e0b.svg?style=flat-square)](#roadmap)

[**Quick Start**](#quick-start) · [**Features**](#features) · [**Roadmap**](#roadmap) · [**Documentation**](#documentation) · [**Contributing**](#contributing) · [**Sponsor**](https://github.com/sponsors/vloom-audio)

</div>

---

**VloomDAW** is a free, 100% open-source Digital Audio Workstation engineered around a fast, pattern-based production workflow. Designed from the ground up for beatmakers, composers, and sound designers, it brings step sequencing, a channel rack, piano roll editing, timeline arrangement, and real-time mixing to a native cross-platform studio environment.

To ensure deterministic performance and strict separation of concerns, real-time audio processing lives in a standalone, UI-free repository: **[vloom-engine](https://github.com/vloom-audio/vloom-engine)**.

> **🎯 Current Milestone: M1 — Channel Rack (Complete)**  
> Drop audio samples directly onto the Channel Rack to sequence 16-step patterns through the real-time engine, with per-channel routing into dedicated Mixer inserts. Both the Piano Roll and Playlist interfaces are available for editing today, with audio playback scheduled for **M2** and **M3**.

---

## ✨ Features

Below is an overview of current capabilities. For a granular breakdown of implementation status (*active*, *UI-only*, or *planned*), see the **[Full Feature Matrix](docs/features.md)**.

### 🥁 Sequencing & Arrangement
* **Transport & Timing** — Play, stop, metronome, tap tempo, LCD tempo/position readouts, master output trim, and musical key/scale display.
* **Channel Rack & Step Sequencer** — Drag-and-drop sample loading across 16-step pads with per-channel gain and target mixer insert routing. Pattern edits apply seamlessly in real time without cutting active voices.
* **Sample Browser** — Integrated directory browser for auditioning and dragging local audio assets straight onto the rack.
* **Song Editors *(UI Preview)*** — Expressive Piano Roll editor (audio engine integration in **M2**) and Pattern Playlist timeline (**M3**).

### 🎚️ Mixing, Audio & Plugins
* **Real-Time Mixer** — Master bus plus 16 insert tracks featuring real-time pan, faders, mute, solo, and responsive peak metering.
* **Native Audio Backends** — Low-latency output via **CoreAudio** (macOS), **WASAPI** (Windows), and **ALSA** (Linux) powered by `vloom-engine`, including graceful hot-plug fallback to the system default device and automatic recovery.
* **Sandboxed Plugin Discovery** — On-demand **VST3** and **CLAP** scanning (never triggered unprompted on startup). Metadata probing runs in an isolated child process (`vloomdaw-plugin-probe`) so faulty third-party plugins cannot crash the host. Browse by category, vendor, or format *(plugin audio hosting lands in **M4/M5**)*.

### 🖥️ Workspace & Performance
* **Detachable Frameless Panels** — Detach the Mixer, Piano Roll, and Playlist into independent windows. Custom client-side headers integrate window controls while preserving native OS snapping and resizing on Windows and macOS.
* **High-Refresh Rendering** — Adaptive UI frame pacing up to **240 Hz**, paired with a built-in real-time performance overlay (`Ctrl/Cmd + Shift + D`).
* **Theme Engine** — Seven curated dark presets with customizable color parameters and built-in contrast validation.

---

## ⌨️ Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| <kbd>Space</kbd> | Play / Pause |
| <kbd>Shift</kbd> + <kbd>Space</kbd> | Stop and return to start |
| <kbd>Home</kbd> | Return playhead to start |
| <kbd>L</kbd> | Toggle Pattern / Song mode |
| <kbd>R</kbd> | Arm / Disarm recording *(audio input in M6)* |
| <kbd>T</kbd> | Tap tempo |
| <kbd>Ctrl</kbd> / <kbd>Cmd</kbd> + <kbd>M</kbd> | Toggle metronome |
| <kbd>F5</kbd> / <kbd>F6</kbd> / <kbd>F7</kbd> / <kbd>F9</kbd> | Focus Playlist / Channel Rack / Piano Roll / Mixer |
| <kbd>Alt</kbd> + <kbd>F8</kbd> | Show / Hide Sample Browser |
| <kbd>Ctrl</kbd> / <kbd>Cmd</kbd> + <kbd>,</kbd> | Open Settings |
| <kbd>Ctrl</kbd> / <kbd>Cmd</kbd> + <kbd>Shift</kbd> + <kbd>D</kbd> | Toggle Performance Overlay |
| <kbd>Ctrl</kbd> + <kbd>Cmd</kbd> + <kbd>F</kbd> | Toggle Full Screen *(macOS)* |

---

## 🛠️ System Requirements

* **Build System:** CMake ≥ 3.22 and Git
* **C++20 Compiler:** Apple Clang 15+ (macOS), GCC 12+ (Linux), or MSVC 2022 (Windows)
* **UI Framework:** **Qt ≥ 6.5** (`Widgets` and `Svg` modules)
* **Media Decoding:** **FFmpeg ≥ 5.1** (`libavformat`, `libavcodec`, `libswresample`) for MP3, FLAC, Ogg, AIFF, and M4A decoding — fetched automatically on Windows; resolved via system packages on macOS and Linux (see **[Getting Started](docs/getting-started.md)**)
* **Audio Engine:** **[vloom-engine](https://github.com/vloom-audio/vloom-engine)** cloned as a sibling directory (automated via `scripts/bootstrap.sh`)

---

## 🚀 Quick Start

### 1. Initialize the Workspace

```sh
mkdir vloom-audio && cd vloom-audio
git clone https://github.com/vloom-audio/VloomDAW.git
cd VloomDAW

# Automatically clones ../vloom-engine if absent and verifies QT_DIR
scripts/bootstrap.sh
```

### 2. Configure & Build

Point `QT_DIR` to your Qt 6 installation (for example, `export QT_DIR=$(brew --prefix qt)` on macOS):

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

> **💡 Building without Qt:** You can compile and test the headless application core (`core` and `audio` targets) without a Qt installation using the `core` preset:
> ```sh
> cmake --preset core
> cmake --build --preset core
> ctest --preset core
> ```

### 3. Launch VloomDAW

* **macOS:** `open build/dev/src/VloomDAW.app`
* **Linux / Windows:** `./build/dev/src/VloomDAW`

For platform-specific dependencies and IDE configuration, refer to **[docs/getting-started.md](docs/getting-started.md)**.

---

## 📂 Repository Architecture

```text
VloomDAW/
├── src/
│   ├── app/              # Application entry point; wires AudioController and MainWindow
│   ├── audio/            # Qt-free AudioController; bridges project/mixer state to vloom-engine
│   ├── core/             # Qt-free domain models: project state, mixer graph, plugin scanner
│   ├── probe/            # vloomdaw-plugin-probe: out-of-process VST3/CLAP metadata extractor
│   └── ui/               # Qt 6 Widgets frontend (target: vloomdaw_ui)
│       ├── MainWindow    # Primary workspace shell and engine binding
│       ├── Theme         # Theme presets, color tokens, and stylesheet engine
│       ├── browser/      # Sample browser panel
│       ├── channelrack/  # Step sequencer, channel controls, and insert routing
│       ├── mixer/        # Master/insert console strips and peak metering
│       ├── pianoroll/    # Note and velocity editor
│       ├── playlist/     # Timeline pattern arrangement editor
│       ├── plugins/      # Plugin browser sidebar and Plugin Manager dialog
│       ├── dialogs/      # Preferences and audio/theme configuration dialogs
│       └── widgets/      # Modular custom controls (TransportBar, LcdDisplay, knobs, etc.)
├── resources/icons/      # Vector SVG assets (see directory README for attribution)
├── tests/                # Headless Catch2 test suites for core/, audio/, and plugin probe
├── cmake/                # CMake toolchain helpers and Dependencies.cmake
├── docs/                 # Architecture specs, roadmap, feature matrix, and UI guidelines
└── scripts/              # Developer automation (bootstrap.sh, format.sh)
```

---

## 🗺️ Roadmap

| Milestone | Key Deliverables | Status |
| :---: | :--- | :---: |
| **M0** | Workspace shell, audio device configuration, transport, metronome, sample browser | ✅ Complete |
| **M1** | Channel rack & 16-step sequencer with real-time sample playback and mixer insert routing | ✅ Complete |
| **M1.5** | Per-channel panning and browser sample preview | 🎯 Next Up |
| **M2** | Piano roll audio playback, first built-in synthesizer, undo/redo history | 🗓️ Planned |
| **M3** | Playlist timeline playback, song arrangement mode, offline WAV export | 🗓️ Planned |
| **M4** | Project serialization (save/load), CLAP plugin hosting, mixer effect slots *(First Alpha)* | 🗓️ Planned |
| **M5** | VST3 plugin hosting, built-in DSP effects, auxiliary mixer sends | 🗓️ Planned |
| **M6** | Hardware audio recording and real-time MIDI input | 🗓️ Planned |

Detailed milestone specifications and completion criteria are tracked in **[docs/roadmap.md](docs/roadmap.md)**.

---

## 📚 Documentation

* **[Feature Matrix](docs/features.md)** — Comprehensive status of every subsystem and UI component.
* **[Development Roadmap](docs/roadmap.md)** — Milestone definitions and path to v1.0.
* **[Getting Started](docs/getting-started.md)** — Toolchain setup, Qt/FFmpeg configuration, and IDE workflows.
* **[System Architecture](docs/architecture.md)** — Threading model and state synchronization between UI, `AudioController`, and `vloom-engine`.
* **[UI Guidelines](docs/ui-guidelines.md)** — Custom widget conventions, theming tokens, and iconography rules.
* **[Engine Documentation](https://github.com/vloom-audio/vloom-engine/tree/main/docs)** — Real-time audio thread invariants, DSP graph, and timing specs.

---

## 🤝 Contributing & Support

### Contributing
Code contributions, UI refinements, and detailed bug reports are always welcome. Please review our **[Contributing Guide](https://github.com/vloom-audio/.github/blob/main/CONTRIBUTING.md)** before submitting a pull request.

### Support Vloom Audio
VloomDAW is free software and will remain open source forever—funded by community backing rather than restrictive licenses. If you want to accelerate development, consider **[sponsoring Vloom Audio on GitHub](https://github.com/sponsors/vloom-audio)**.

---

## 📄 License

Distributed under the **GNU General Public License v3.0 or later** (`GPL-3.0-or-later`). See **[LICENSE](LICENSE)** for full terms.
