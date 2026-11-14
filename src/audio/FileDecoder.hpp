// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <string>

namespace vloomdaw {

/// What decodeToWav() accepts at most, in line with the engine's WAV reader
/// (vloom::WavReadLimits): a file past them is refused while it is decoded,
/// before it can fill memory.
struct DecodeLimits {
    double maxSeconds = 20.0 * 60.0;
    int maxChannels = 8;
    std::size_t maxBytes = std::size_t{1} << 30; ///< of 32-bit float samples
};

/// Decodes the audio file at `path` (UTF-8) with FFmpeg and writes it to
/// `target` as a 32-bit float WAV, at `sampleRate` (0: the file's own rate),
/// so the engine can load it. FFmpeg resamples while decoding, which is much
/// faster than resampling afterwards. Returns false with the reason in
/// `error`; `target` is then not written.
///
/// Security: FFmpeg parses the file in this process. To keep what a crafted
/// file can reach small, only local files are read (no network or other
/// protocols, so a playlist-like file cannot make FFmpeg fetch URLs), and
/// only these containers and codecs are allowed: MP3, FLAC, Ogg (Vorbis,
/// Opus, FLAC), WAV and AIFF (PCM), and M4A (AAC, ALAC).
///
/// Slow for long files (seconds): call it off the UI thread.
bool decodeToWav(const std::string& path, const std::string& target, int sampleRate, std::string* error,
    const DecodeLimits& limits = {});

} // namespace vloomdaw
