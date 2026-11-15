// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace vloomdaw {

/// The rate the stem models work at; the separator's input and its stems.
inline constexpr int kStemSampleRate = 44100;

/// Writes the audio file at `source` (UTF-8) as the stem separator reads it:
/// raw 32-bit float samples, stereo interleaved, at kStemSampleRate (mono is
/// put on both sides; of more channels the first two are taken). `scratch`
/// (UTF-8) is a file it may write on the way and removes again. Returns the
/// number of frames, or 0 with the reason in `error`.
///
/// Slow (decodes the file): call it off the UI thread.
std::int64_t writeStemInput(const std::string& source, const std::filesystem::path& target,
    const std::string& scratch, std::string* error);

/// Writes a stem the separator made (`raw`: as writeStemInput()'s output) to
/// `wav` (UTF-8) as a 32-bit float WAV. `frames` is how many it must hold:
/// anything else is refused. False with the reason in `error`.
bool writeStemWav(
    const std::filesystem::path& raw, const std::string& wav, std::int64_t frames, std::string* error);

} // namespace vloomdaw
