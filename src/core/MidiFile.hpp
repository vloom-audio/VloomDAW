// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/Project.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vloomdaw {

/// One musical track extracted from a Standard MIDI File (`.mid` / `.midi`),
/// with note positions and lengths already scaled to VloomDAW's `kTicksPerBeat` (960 PPQ).
struct MidiTrackImport {
    std::string name;
    int midiChannel = -1; ///< 0..15 when all notes on the track share one MIDI channel (9 = GM Drums)
    std::vector<Note> notes;
};

/// Parsed contents of a Standard MIDI File (Format 0, 1, or 2).
struct MidiFileImport {
    int format = 1;
    std::optional<double> tempoBpm; ///< initial `FF 51` tempo if present, clamped to 10..999 BPM
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    std::vector<MidiTrackImport> tracks; ///< non-empty tracks in file order
};

struct MidiReadResult {
    bool ok = false;
    MidiFileImport file;
    std::string error;
};

/// Parses a Standard MIDI File from raw bytes without throwing.
/// Defensive against untrusted input: validates chunk bounds, variable-length
/// quantities, running status, and note/track count limits.
[[nodiscard]] MidiReadResult parseMidiBytes(std::span<const std::uint8_t> bytes);

/// Reads and parses a `.mid` / `.midi` file from disk (up to 32 MiB).
[[nodiscard]] MidiReadResult readMidiFile(const std::filesystem::path& path);

} // namespace vloomdaw
