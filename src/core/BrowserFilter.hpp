// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace vloomdaw {

/// File types the browser shows.
inline constexpr std::array<std::string_view, 6> kAudioFileExtensions{
    "wav", "aif", "aiff", "flac", "mp3", "ogg"};

/// Wildcard name filters for audio files whose name contains `search`, for
/// QFileSystemModel::setNameFilters(). Matching ignores letter case on every
/// platform (letters become [aA] classes); wildcard characters typed by the
/// user are dropped so they cannot change the pattern. Empty search = all
/// audio files.
std::vector<std::string> audioFileNameFilters(std::string_view search);

} // namespace vloomdaw
