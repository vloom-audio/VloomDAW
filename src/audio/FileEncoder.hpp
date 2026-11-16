// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vloomdaw {

/// The kinds of audio file the app can export.
enum class AudioFileFormat { Wav, Flac, Mp3, Ogg, M4a };

/// The file extension of `format`, without the dot: "wav", "flac", "mp3", "ogg", "m4a".
[[nodiscard]] std::string_view audioFileExtension(AudioFileFormat format);

/// Whether the FFmpeg the app runs with can write `format`. WAV is written by
/// the engine and always can. MP3 and Ogg Vorbis need FFmpeg built with LAME
/// and libvorbis, which the builds the app ships with have on Windows only.
[[nodiscard]] bool audioEncoderAvailable(AudioFileFormat format);

/// What a file says about itself: its tags. Empty fields are left out.
/// Text is UTF-8; control characters are dropped and each field is cut to
/// kMaxField bytes when it is written.
struct AudioMetadata {
    static constexpr std::size_t kMaxField = 500;

    std::string title;
    std::string artist;
    std::string album;
    std::string year;
    std::string genre;
    std::string comment;

    [[nodiscard]] bool empty() const
    {
        return title.empty() && artist.empty() && album.empty() && year.empty() && genre.empty()
            && comment.empty();
    }
    friend bool operator==(const AudioMetadata&, const AudioMetadata&) = default;
};

/// Adds `metadata` to the WAV file at `path` (UTF-8), which must have none
/// yet (as the engine's export writes it): a LIST / INFO chunk after the
/// audio, the way players and editors read it. Nothing is written for empty
/// metadata. False with the reason in `error`; the audio is untouched then.
bool writeWavMetadata(const std::string& path, const AudioMetadata& metadata, std::string* error);

/// How encodeWavFile() writes.
struct EncodeOptions {
    AudioFileFormat format = AudioFileFormat::Flac; ///< not Wav
    int bitDepth = 24; ///< FLAC: 16 or 24
    bool dither = true; ///< FLAC at 16 bits: triangular dither
    int bitrateKbps = 256; ///< MP3, Ogg and M4A: clamped to what the format takes
    AudioMetadata metadata; ///< the file's tags (ID3, Vorbis comments, MP4 atoms)
};

/// The bit rates on offer for a format with one (MP3, Ogg, M4A), lowest to
/// highest, in kbit/s. Empty for the others.
[[nodiscard]] const std::vector<int>& audioBitrates(AudioFileFormat format);

/// Called with the share done, 0 .. 1. Return false to cancel.
using EncodeProgress = std::function<bool(double done)>;

/// Encodes the WAV file at `wav` (UTF-8; as the engine's export writes it)
/// to `target` (UTF-8) in `options.format`, with FFmpeg. The rate is kept
/// unless the format cannot take it (MP3 above 48 kHz, M4A above 96 kHz:
/// resampled to 48 kHz). The file appears under its name only when it is
/// whole. Returns false with the reason in `error` (empty when cancelled).
///
/// Slow for long files: call it off the audio thread.
bool encodeWavFile(const std::string& wav, const std::string& target, const EncodeOptions& options,
    std::string* error, const EncodeProgress& progress = {});

} // namespace vloomdaw
