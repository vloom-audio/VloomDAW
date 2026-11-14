// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/MixerModel.hpp"
#include "core/Project.hpp"
#include "core/Scales.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vloomdaw {

/// Song-wide settings saved with a project (the transport's).
struct SongSettings {
    double tempo = 120.0; ///< BPM
    int keyRoot = 0; ///< pitch class, 0 = C
    Scale scale = Scale::Major;
    bool songMode = false; ///< plays the playlist instead of looping the pattern
};

/// Plugin states, by where each plugin sits. Empty bytes: none saved (the
/// plugin starts at its defaults).
struct PluginStates {
    std::vector<std::vector<std::uint8_t>> instruments; ///< per channel
    std::vector<std::vector<std::vector<std::uint8_t>>> effects; ///< per mixer track (master first), per slot
};

/// Everything a project file holds.
struct ProjectData {
    Project project;
    MixerModel mixer{0};
    SongSettings song;
    PluginStates states;
};

/// The .vloom project file (docs/project-file.md): UTF-8 JSON with a format
/// name and version. Sample paths inside the project's folder are stored
/// relative to it, so the folder can move.
///
/// Reading is built for untrusted files: the size, nesting and every count
/// are capped, every value is checked and clamped through the model's own
/// setters, and a file from a newer version is refused rather than half
/// read. Unknown keys are ignored.
class ProjectFile {
public:
    static constexpr std::string_view kFormat = "vloomdaw-project";
    static constexpr int kVersion = 1;
    static constexpr std::uintmax_t kMaxFileBytes = 256ull << 20; ///< plugin states make files big
    static constexpr std::size_t kMaxChannels = 1000;
    static constexpr std::size_t kMaxNotes = 100000; ///< per channel and pattern
    static constexpr std::size_t kMaxClips = 100000; ///< pattern and audio clips, each
    static constexpr std::size_t kMaxStateBytes = 64ull << 20; ///< per plugin
    static constexpr int kMaxDepth = 32; ///< JSON nesting

    struct ReadResult {
        std::optional<ProjectData> data; ///< set on success
        std::string error; ///< why not, for the user
        /// What was left out but did not stop the read (a value out of
        /// range, an unknown plugin format), for the user.
        std::vector<std::string> warnings;
    };

    /// The project as JSON text. `folder` is the project file's folder:
    /// sample paths inside it are written relative to it.
    [[nodiscard]] static std::string write(const ProjectData& data, const std::filesystem::path& folder);
    /// Reads JSON text written by write(); relative sample paths are taken
    /// from `folder`.
    [[nodiscard]] static ReadResult read(std::string_view text, const std::filesystem::path& folder);

    /// Writes the file at `path` (UTF-8) safely: to a temporary file next to
    /// it, then renamed over it, so a failed save never leaves a broken file.
    [[nodiscard]] static bool save(
        const std::filesystem::path& path, const ProjectData& data, std::string* error);
    [[nodiscard]] static ReadResult load(const std::filesystem::path& path);
};

/// A UTF-8 string as a path, and back (std::filesystem's own conversions
/// use the system code page on Windows).
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view utf8);
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);

} // namespace vloomdaw
