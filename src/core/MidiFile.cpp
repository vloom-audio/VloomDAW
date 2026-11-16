// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/MidiFile.hpp"

#include "core/Names.hpp"
#include "core/ProjectFile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>

namespace vloomdaw {

namespace {

constexpr std::uintmax_t kMaxMidiFileBytes = 32u << 20; // 32 MiB
constexpr std::size_t kMaxMidiTrackNotes = 100000;
constexpr std::int64_t kMaxScaledTick = std::numeric_limits<int>::max() / 4;

std::uint16_t readBe16(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

std::uint32_t readBe32(const std::uint8_t* p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
        | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

bool readVlq(std::span<const std::uint8_t> buf, std::size_t& pos, std::uint32_t& out) noexcept
{
    out = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= buf.size())
            return false;
        const std::uint8_t byte = buf[pos++];
        out = (out << 7) | static_cast<std::uint32_t>(byte & 0x7Fu);
        if ((byte & 0x80u) == 0)
            return true;
    }
    return false; // malformed >4-byte VLQ
}

int scaleTick(std::int64_t rawTick, int ticksPerBeat) noexcept
{
    if (rawTick <= 0 || ticksPerBeat <= 0)
        return 0;
    // Use double when rawTick is huge to avoid 64-bit multiplication overflow.
    if (rawTick > (std::numeric_limits<std::int64_t>::max() / (kTicksPerBeat * 2))) {
        const double scaled
            = (static_cast<double>(rawTick) * static_cast<double>(kTicksPerBeat)) / ticksPerBeat;
        const double clamped = std::clamp(scaled, 0.0, static_cast<double>(kMaxScaledTick));
        return static_cast<int>(std::llround(clamped));
    }
    const std::int64_t scaled = (rawTick * kTicksPerBeat + ticksPerBeat / 2) / ticksPerBeat;
    return static_cast<int>(std::clamp<std::int64_t>(scaled, 0, kMaxScaledTick));
}

struct ActiveNote {
    bool open = false;
    std::int64_t startRaw = 0;
    int velocity = kDefaultVelocity;
};

void emitClosedNote(std::vector<Note>& dest, int key, std::int64_t startRaw, std::int64_t endRaw,
    int velocity, int ticksPerBeat)
{
    if (dest.size() >= kMaxMidiTrackNotes)
        return;
    const int clampedKey = std::clamp(key, kLowestKey, kHighestKey);
    const int start = scaleTick(startRaw, ticksPerBeat);
    const int end = scaleTick(std::max(startRaw, endRaw), ticksPerBeat);
    const int length = std::max(kMinNoteLength, end - start);
    const int vel = std::clamp(velocity, kMinVelocity, kMaxVelocity);
    dest.push_back(Note{clampedKey, start, length, vel});
}

void sortMidiNotes(std::vector<Note>& notes)
{
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        if (a.start != b.start)
            return a.start < b.start;
        return a.key < b.key;
    });
}

} // namespace

MidiReadResult parseMidiBytes(std::span<const std::uint8_t> bytes)
{
    MidiReadResult res;
    if (bytes.size() < 14) {
        res.error = "File is too small to be a Standard MIDI File.";
        return res;
    }
    if (!(bytes[0] == 'M' && bytes[1] == 'T' && bytes[2] == 'h' && bytes[3] == 'd')) {
        res.error = "Missing MThd header (not a Standard MIDI File).";
        return res;
    }

    const std::uint32_t headerLen = readBe32(bytes.data() + 4);
    if (headerLen < 6 || 8u + static_cast<std::size_t>(headerLen) > bytes.size()) {
        res.error = "Invalid MIDI header chunk length.";
        return res;
    }

    const std::uint16_t format = readBe16(bytes.data() + 8);
    const std::uint16_t declaredTracks = readBe16(bytes.data() + 10);
    const std::uint16_t rawDivision = readBe16(bytes.data() + 12);
    if (format > 2) {
        res.error = "Unsupported MIDI file format.";
        return res;
    }

    int ticksPerBeat = 0;
    if ((rawDivision & 0x8000u) == 0) {
        ticksPerBeat = static_cast<int>(rawDivision);
    } else {
        const int fpsCode = -static_cast<int>(static_cast<std::int8_t>(rawDivision >> 8));
        const int ticksPerFrame = static_cast<int>(rawDivision & 0xFFu);
        const int fps = (fpsCode == 29) ? 30 : fpsCode;
        if (fps > 0 && ticksPerFrame > 0)
            ticksPerBeat = std::max(1, (fps * ticksPerFrame) / 2); // 120 BPM reference
    }
    if (ticksPerBeat <= 0) {
        res.error = "Invalid MIDI timing division (zero ticks per beat).";
        return res;
    }

    res.file.format = static_cast<int>(format);

    std::size_t cursor = 8u + static_cast<std::size_t>(headerLen);
    std::uint16_t parsedMTrks = 0;

    while (cursor + 8 <= bytes.size() && parsedMTrks < declaredTracks) {
        const bool isMTrk = bytes[cursor] == 'M' && bytes[cursor + 1] == 'T' && bytes[cursor + 2] == 'r'
            && bytes[cursor + 3] == 'k';
        const std::uint32_t chunkLen = readBe32(bytes.data() + cursor + 4);
        cursor += 8;
        if (chunkLen > bytes.size() - cursor) {
            // Truncated final chunk: clamp to remaining bytes rather than losing prior tracks,
            // or fail if this is the very first chunk and empty.
            if (!isMTrk)
                break;
        }
        const std::size_t trackLen = std::min<std::size_t>(chunkLen, bytes.size() - cursor);
        const std::span<const std::uint8_t> trk = bytes.subspan(cursor, trackLen);
        cursor += trackLen;

        if (!isMTrk)
            continue; // skip vendor chunks cleanly
        ++parsedMTrks;

        std::string trackName;
        std::string instrumentHint;
        std::array<std::array<ActiveNote, 128>, 16> active{};
        std::array<std::vector<Note>, 16> perChannelNotes{};
        std::array<bool, 16> channelUsed{};
        std::vector<Note> mergedNotes;

        std::size_t pos = 0;
        std::int64_t rawTick = 0;
        std::uint8_t runningStatus = 0;

        while (pos < trk.size()) {
            std::uint32_t delta = 0;
            if (!readVlq(trk, pos, delta))
                break;
            if (rawTick <= std::numeric_limits<std::int64_t>::max() - delta)
                rawTick += static_cast<std::int64_t>(delta);
            if (pos >= trk.size())
                break;

            std::uint8_t status = trk[pos];
            if (status < 0x80u) {
                if (runningStatus < 0x80u)
                    break; // invalid running status
                status = runningStatus;
            } else {
                ++pos;
                if (status < 0xF0u)
                    runningStatus = status;
                else if (status < 0xF8u)
                    runningStatus = 0; // system common cancels running status
            }

            if (status == 0xFFu) {
                if (pos >= trk.size())
                    break;
                const std::uint8_t metaType = trk[pos++];
                std::uint32_t metaLen = 0;
                if (!readVlq(trk, pos, metaLen) || metaLen > trk.size() - pos)
                    break;
                const std::span<const std::uint8_t> payload = trk.subspan(pos, metaLen);
                pos += metaLen;

                if (metaType == 0x2Fu) {
                    break; // End of Track
                }
                if (metaType == 0x03u && trackName.empty() && !payload.empty()) {
                    std::string raw(reinterpret_cast<const char*>(payload.data()), payload.size());
                    trackName = cleanName(raw, Project::kMaxTrackNameLength);
                } else if (metaType == 0x04u && instrumentHint.empty() && !payload.empty()) {
                    std::string raw(reinterpret_cast<const char*>(payload.data()), payload.size());
                    instrumentHint = cleanName(raw, Project::kMaxTrackNameLength);
                } else if (metaType == 0x51u && payload.size() == 3 && !res.file.tempoBpm.has_value()) {
                    const std::uint32_t usPerBeat = (static_cast<std::uint32_t>(payload[0]) << 16)
                        | (static_cast<std::uint32_t>(payload[1]) << 8)
                        | static_cast<std::uint32_t>(payload[2]);
                    if (usPerBeat > 0) {
                        const double bpm = 60000000.0 / static_cast<double>(usPerBeat);
                        if (std::isfinite(bpm))
                            res.file.tempoBpm = std::clamp(bpm, 10.0, 999.0);
                    }
                } else if (metaType == 0x58u && payload.size() >= 2) {
                    const int num = static_cast<int>(payload[0]);
                    const int denShift = static_cast<int>(payload[1]);
                    if (num >= 1 && num <= 32 && denShift >= 0 && denShift <= 5) {
                        res.file.timeSigNumerator = num;
                        res.file.timeSigDenominator = 1 << denShift;
                    }
                }
                continue;
            }

            if (status == 0xF0u || status == 0xF7u) {
                std::uint32_t sysexLen = 0;
                if (!readVlq(trk, pos, sysexLen) || sysexLen > trk.size() - pos)
                    break;
                pos += sysexLen;
                continue;
            }

            if (status >= 0xF0u) {
                // Other system common / real-time messages (0, 1, or 2 data bytes)
                if (status == 0xF2u)
                    pos = std::min(trk.size(), pos + 2);
                else if (status == 0xF1u || status == 0xF3u)
                    pos = std::min(trk.size(), pos + 1);
                continue;
            }

            const std::uint8_t kind = status & 0xF0u;
            const int ch = static_cast<int>(status & 0x0Fu);

            if (kind == 0x80u || kind == 0x90u) {
                if (pos + 2 > trk.size())
                    break;
                const int key = static_cast<int>(trk[pos] & 0x7Fu);
                const int vel = static_cast<int>(trk[pos + 1] & 0x7Fu);
                pos += 2;

                const bool isNoteOn = (kind == 0x90u && vel > 0);
                ActiveNote& slot = active[static_cast<std::size_t>(ch)][static_cast<std::size_t>(key)];
                if (isNoteOn) {
                    if (slot.open) {
                        auto& dest
                            = (format == 0) ? perChannelNotes[static_cast<std::size_t>(ch)] : mergedNotes;
                        emitClosedNote(dest, key, slot.startRaw, rawTick, slot.velocity, ticksPerBeat);
                        channelUsed[static_cast<std::size_t>(ch)] = true;
                    }
                    slot.open = true;
                    slot.startRaw = rawTick;
                    slot.velocity = vel;
                } else if (slot.open) {
                    auto& dest = (format == 0) ? perChannelNotes[static_cast<std::size_t>(ch)] : mergedNotes;
                    emitClosedNote(dest, key, slot.startRaw, rawTick, slot.velocity, ticksPerBeat);
                    channelUsed[static_cast<std::size_t>(ch)] = true;
                    slot.open = false;
                }
            } else if (kind == 0xA0u || kind == 0xB0u || kind == 0xE0u) {
                if (pos + 2 > trk.size())
                    break;
                pos += 2;
            } else if (kind == 0xC0u || kind == 0xD0u) {
                if (pos + 1 > trk.size())
                    break;
                pos += 1;
            }
        }

        // Close any notes left open at end-of-track.
        const std::int64_t quarter = ticksPerBeat / 4;
        for (int ch = 0; ch < 16; ++ch) {
            for (int key = 0; key < 128; ++key) {
                const ActiveNote& slot = active[static_cast<std::size_t>(ch)][static_cast<std::size_t>(key)];
                if (!slot.open)
                    continue;
                const std::int64_t minEnd
                    = slot.startRaw <= std::numeric_limits<std::int64_t>::max() - quarter
                    ? slot.startRaw + quarter
                    : std::numeric_limits<std::int64_t>::max();
                const std::int64_t fallbackEnd = std::max(rawTick, minEnd);
                auto& dest = (format == 0) ? perChannelNotes[static_cast<std::size_t>(ch)] : mergedNotes;
                emitClosedNote(dest, key, slot.startRaw, fallbackEnd, slot.velocity, ticksPerBeat);
                channelUsed[static_cast<std::size_t>(ch)] = true;
            }
        }

        const std::string baseTitle = !trackName.empty() ? trackName : instrumentHint;

        if (format == 0) {
            int activeChannelCount = 0;
            for (int ch = 0; ch < 16; ++ch)
                if (!perChannelNotes[static_cast<std::size_t>(ch)].empty())
                    ++activeChannelCount;

            for (int ch = 0; ch < 16; ++ch) {
                auto& notes = perChannelNotes[static_cast<std::size_t>(ch)];
                if (notes.empty())
                    continue;
                if (res.file.tracks.size() >= static_cast<std::size_t>(Project::kMaxPlaylistTracks))
                    break;
                sortMidiNotes(notes);
                std::string name;
                if (activeChannelCount == 1 && !baseTitle.empty()) {
                    name = baseTitle;
                } else if (ch == 9) {
                    name = baseTitle.empty() ? "Drums"
                                             : cleanName(baseTitle + " Drums", Project::kMaxTrackNameLength);
                } else if (!baseTitle.empty()) {
                    name = cleanName(
                        baseTitle + " Ch " + std::to_string(ch + 1), Project::kMaxTrackNameLength);
                } else {
                    name = "MIDI Ch " + std::to_string(ch + 1);
                }
                res.file.tracks.push_back(MidiTrackImport{std::move(name), ch, std::move(notes)});
            }
        } else if (!mergedNotes.empty()
            && res.file.tracks.size() < static_cast<std::size_t>(Project::kMaxPlaylistTracks)) {
            sortMidiNotes(mergedNotes);
            int soleChannel = -1;
            int usedChannels = 0;
            for (int ch = 0; ch < 16; ++ch) {
                if (channelUsed[static_cast<std::size_t>(ch)]) {
                    soleChannel = ch;
                    ++usedChannels;
                }
            }
            if (usedChannels != 1)
                soleChannel = -1;

            std::string name = baseTitle;
            if (name.empty()) {
                name = (soleChannel == 9) ? "Drums" : ("MIDI " + std::to_string(res.file.tracks.size() + 1));
            }
            res.file.tracks.push_back(MidiTrackImport{std::move(name), soleChannel, std::move(mergedNotes)});
        }
    }

    if (res.file.tracks.empty()) {
        res.error = "The MIDI file contains no playable notes.";
        return res;
    }

    res.ok = true;
    return res;
}

MidiReadResult readMidiFile(const std::filesystem::path& path)
{
    MidiReadResult res;
    if (isNetworkPath(path)) {
        res.error = "Network share paths are not opened directly; copy the MIDI file to a local drive first.";
        return res;
    }
    std::error_code ec;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        res.error = "Could not open the MIDI file.";
        return res;
    }
    if (fileSize > kMaxMidiFileBytes) {
        res.error = "The MIDI file is larger than 32 MiB.";
        return res;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        res.error = "Could not read the MIDI file.";
        return res;
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fileSize));
    if (!bytes.empty()
        && !in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        res.error = "Could not read the full MIDI file.";
        return res;
    }
    return parseMidiBytes(bytes);
}

} // namespace vloomdaw
