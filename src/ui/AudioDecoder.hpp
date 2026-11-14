// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>

namespace vloomdaw {

/// Audio files the app takes besides WAV (MP3, FLAC, Ogg, AIFF, M4A): they
/// are decoded once with FFmpeg (audio/FileDecoder, which also says what
/// FFmpeg is allowed to read) into a 32-bit float WAV in the app's cache,
/// at the engine's rate, which the engine then loads like any WAV file. The
/// project keeps the original path.

/// True for a file the engine reads directly (WAV).
[[nodiscard]] bool isWavFile(const QString& path);

/// True for a file playableWav() can turn into WAV (the browser's other
/// audio types).
[[nodiscard]] bool needsDecoding(const QString& path);

/// The WAV file to load for `path`: `path` itself for a WAV file, else the
/// decoded copy in the cache (decoded now if there is none for this version
/// of the file and this rate). Empty on failure, with the reason in `error`.
///
/// With `sampleRate` > 0 the copy is made at that rate (the engine's), so
/// loading it needs no resampling: FFmpeg resamples while decoding, much
/// faster than resampling afterwards. 0 keeps the file's own rate.
///
/// Blocks while decoding (up to a second or two for a song): call it off
/// the UI thread.
[[nodiscard]] QString playableWav(const QString& path, double sampleRate, QString* error = nullptr);

} // namespace vloomdaw
