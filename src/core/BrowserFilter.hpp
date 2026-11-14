// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <array>
#include <string_view>

namespace vloomdaw {

/// File types the browser shows and the app loads: WAV directly, the others
/// decoded with FFmpeg (audio/FileDecoder).
inline constexpr std::array<std::string_view, 9> kAudioFileExtensions{
    "wav", "aif", "aiff", "flac", "mp3", "ogg", "oga", "opus", "m4a"};

/// True when `fileName` ends in one of kAudioFileExtensions, ignoring case.
[[nodiscard]] bool isAudioFile(std::string_view fileName) noexcept;

/// True when the browser shows audio file `fileName` for `search`: the search
/// text appears in the name before the extension, ignoring ASCII letter case.
/// Wildcard characters (`* ? [ ] \`) are dropped from the search and
/// surrounding spaces trimmed; an empty search matches every file.
[[nodiscard]] bool matchesSearch(std::string_view fileName, std::string_view search) noexcept;

} // namespace vloomdaw
