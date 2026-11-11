# Getting started

## 1. Install the tools

| OS | Compiler & CMake | Qt 6.5+ |
|---|---|---|
| macOS | `xcode-select --install` · `brew install cmake ninja pipx` | `brew install qt` → `export QT_DIR=$(brew --prefix qt)` |
| Windows | Visual Studio 2022 (*Desktop development with C++*) + CMake | [Qt online installer](https://www.qt.io/download-qt-installer-oss), component *MSVC 2022 64-bit* → `set QT_DIR=C:\Qt\6.11.3\msvc2022_64` |
| Ubuntu / Debian | `sudo apt install build-essential cmake ninja-build pipx libasound2-dev` | `sudo apt install qt6-base-dev qt6-svg-dev libgl1-mesa-dev` (QT_DIR not needed) |

Put the `QT_DIR` line in your shell profile so every terminal has it.

Formatting uses **clang-format 23.1.3 exactly** (other versions format differently and
fail CI). Install it on any OS with `pipx install clang-format==23.1.3`
(or `pip install --user clang-format==23.1.3`). If another clang-format comes first on
your `PATH`, point the script at the right one: `export CLANG_FORMAT=/path/to/clang-format`.

## 2. Get the code

```sh
mkdir vloom-audio && cd vloom-audio
git clone https://github.com/vloom-audio/VloomDAW.git
VloomDAW/scripts/bootstrap.sh     # clones vloom-engine next to it
```

You end up with:

```
vloom-audio/
├── VloomDAW/        ← the app (this repo)
└── vloom-engine/    ← the engine, used directly by the app build
```

Edits in `../vloom-engine` are picked up by the next VloomDAW build.

## 3. Build, test, run

```sh
cd VloomDAW
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

| OS | Run |
|---|---|
| macOS | `open build/dev/src/VloomDAW.app` |
| Windows | `build\dev\src\Debug\VloomDAW.exe` (Visual Studio generator) |
| Linux | `build/dev/src/VloomDAW` |

Speed tip: `export CPM_SOURCE_CACHE="$HOME/.cache/cpm"` shares downloaded
dependencies between builds and repos.

## 4. Editor setup

- **Qt Creator / CLion / VS Code (CMake Tools)**: open the folder and pick the `dev` preset.
- **clangd**: presets write `build/<preset>/compile_commands.json`.

## Day-to-day

| Task | Command |
|---|---|
| Format your changes | `scripts/format.sh` |
| Check formatting like CI | `scripts/format.sh --check` |
| Check commit messages like CI | `scripts/check_commit_messages.sh origin/main..HEAD` |
| Build without Qt (core + tests) | `cmake --preset core && cmake --build --preset core && ctest --preset core` |
| Use a specific engine version | `cmake --preset dev -DVLOOM_ENGINE_DIR=/nonexistent -DVLOOM_ENGINE_GIT_TAG=v0.1.0` |

Next: [architecture.md](architecture.md) and [ui-guidelines.md](ui-guidelines.md).
