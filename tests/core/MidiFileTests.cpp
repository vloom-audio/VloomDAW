// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/BrowserFilter.hpp"
#include "core/MidiFile.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using vloomdaw::isMidiFile;
using vloomdaw::kTicksPerBeat;
using vloomdaw::parseMidiBytes;
using vloomdaw::readMidiFile;

namespace {

void pushBe16(std::vector<std::uint8_t>& buf, std::uint16_t v)
{
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void pushBe32(std::vector<std::uint8_t>& buf, std::uint32_t v)
{
    buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}

void pushVlq(std::vector<std::uint8_t>& buf, std::uint32_t v)
{
    std::uint8_t bytes[4];
    int count = 0;
    bytes[count++] = static_cast<std::uint8_t>(v & 0x7Fu);
    while ((v >>= 7) > 0)
        bytes[count++] = static_cast<std::uint8_t>((v & 0x7Fu) | 0x80u);
    for (int i = count - 1; i >= 0; --i)
        buf.push_back(bytes[i]);
}

void pushChunk(std::vector<std::uint8_t>& out, const char (&tag)[5], const std::vector<std::uint8_t>& payload)
{
    out.insert(out.end(), tag, tag + 4);
    pushBe32(out, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

void pushTrackName(std::vector<std::uint8_t>& trk, const std::string& name)
{
    pushVlq(trk, 0);
    trk.push_back(0xFFu);
    trk.push_back(0x03u);
    pushVlq(trk, static_cast<std::uint32_t>(name.size()));
    trk.insert(trk.end(), name.begin(), name.end());
}

void pushEndOfTrack(std::vector<std::uint8_t>& trk)
{
    pushVlq(trk, 0);
    trk.push_back(0xFFu);
    trk.push_back(0x2Fu);
    trk.push_back(0x00u);
}

} // namespace

TEST_CASE("MIDI file extensions are recognised case-insensitively", "[core][midi]")
{
    CHECK(isMidiFile("song.mid"));
    CHECK(isMidiFile("Lead_Melody.MID"));
    CHECK(isMidiFile("arrangement.midi"));
    CHECK(isMidiFile("Chords.MIDI"));
    CHECK_FALSE(isMidiFile(".mid"));
    CHECK_FALSE(isMidiFile("song.wav"));
}

TEST_CASE(
    "Format 1 multi-track MIDI parses tempo, track names, running status and PPQ scaling", "[core][midi]")
{
    std::vector<std::uint8_t> smf;

    // Header: Format 1, 3 MTrks (conductor + Lead + Bass), 480 PPQ (half of 960 PPQ)
    std::vector<std::uint8_t> hdr;
    pushBe16(hdr, 1);
    pushBe16(hdr, 3);
    pushBe16(hdr, 480);
    pushChunk(smf, "MThd", hdr);

    // Track 0: Conductor tempo = 150 BPM (400,000 us/beat = 0x061A80), no notes -> skipped in tracks[]
    std::vector<std::uint8_t> trk0;
    pushTrackName(trk0, "Conductor");
    pushVlq(trk0, 0);
    trk0.push_back(0xFFu);
    trk0.push_back(0x51u);
    trk0.push_back(0x03u);
    trk0.push_back(0x06u);
    trk0.push_back(0x1Au);
    trk0.push_back(0x80u);
    pushEndOfTrack(trk0);
    pushChunk(smf, "MTrk", trk0);

    // Track 1: "Lead Synth" with running status & Note-On velocity 0 as Note-Off
    std::vector<std::uint8_t> trk1;
    pushTrackName(trk1, "Lead Synth");
    pushVlq(trk1, 0);
    trk1.push_back(0x90u);
    trk1.push_back(60);
    trk1.push_back(100);
    pushVlq(trk1, 480); // 1 beat = 960 scaled ticks: close C4 via running status vel=0, open E4
    trk1.push_back(60);
    trk1.push_back(0);
    pushVlq(trk1, 0);
    trk1.push_back(64);
    trk1.push_back(110);
    pushVlq(trk1, 240); // 0.5 beat = 480 scaled ticks: close E4 via explicit 0x80 Note-Off
    trk1.push_back(0x80u);
    trk1.push_back(64);
    trk1.push_back(64);
    pushEndOfTrack(trk1);
    pushChunk(smf, "MTrk", trk1);

    // Track 2: "Sub Bass" starting at bar 2 (4 * 480 = 1920 raw ticks -> 3840 scaled ticks)
    std::vector<std::uint8_t> trk2;
    pushTrackName(trk2, "Sub Bass");
    pushVlq(trk2, 1920);
    trk2.push_back(0x91u);
    trk2.push_back(36);
    trk2.push_back(120);
    pushVlq(trk2, 960);
    trk2.push_back(0x81u);
    trk2.push_back(36);
    trk2.push_back(0);
    pushEndOfTrack(trk2);
    pushChunk(smf, "MTrk", trk2);

    const auto parsed = parseMidiBytes(smf);
    REQUIRE(parsed.ok);
    REQUIRE(parsed.file.tempoBpm.has_value());
    CHECK(*parsed.file.tempoBpm == Catch::Approx(150.0));
    REQUIRE(parsed.file.tracks.size() == 2);

    CHECK(parsed.file.tracks[0].name == "Lead Synth");
    CHECK(parsed.file.tracks[0].midiChannel == 0);
    REQUIRE(parsed.file.tracks[0].notes.size() == 2);
    CHECK(parsed.file.tracks[0].notes[0].key == 60);
    CHECK(parsed.file.tracks[0].notes[0].start == 0);
    CHECK(parsed.file.tracks[0].notes[0].length == kTicksPerBeat);
    CHECK(parsed.file.tracks[0].notes[0].velocity == 100);
    CHECK(parsed.file.tracks[0].notes[1].key == 64);
    CHECK(parsed.file.tracks[0].notes[1].start == kTicksPerBeat);
    CHECK(parsed.file.tracks[0].notes[1].length == kTicksPerBeat / 2);
    CHECK(parsed.file.tracks[0].notes[1].velocity == 110);

    CHECK(parsed.file.tracks[1].name == "Sub Bass");
    CHECK(parsed.file.tracks[1].midiChannel == 1);
    REQUIRE(parsed.file.tracks[1].notes.size() == 1);
    CHECK(parsed.file.tracks[1].notes[0].key == 36);
    CHECK(parsed.file.tracks[1].notes[0].start == kTicksPerBeat * 4);
    CHECK(parsed.file.tracks[1].notes[0].length == kTicksPerBeat * 2);
    CHECK(parsed.file.tracks[1].notes[0].velocity == 120);
}

TEST_CASE("Format 0 multi-channel MIDI splits active channels into separate tracks", "[core][midi]")
{
    std::vector<std::uint8_t> smf;
    std::vector<std::uint8_t> hdr;
    pushBe16(hdr, 0);
    pushBe16(hdr, 1);
    pushBe16(hdr, 960);
    pushChunk(smf, "MThd", hdr);

    std::vector<std::uint8_t> trk;
    // Ch 1 (index 0): Piano C4
    pushVlq(trk, 0);
    trk.push_back(0x90u);
    trk.push_back(60);
    trk.push_back(95);
    // Ch 10 (index 9, GM Drums): Kick 36
    pushVlq(trk, 0);
    trk.push_back(0x99u);
    trk.push_back(36);
    trk.push_back(127);
    pushVlq(trk, 240);
    trk.push_back(0x89u);
    trk.push_back(36);
    trk.push_back(0);
    pushVlq(trk, 720);
    trk.push_back(0x80u);
    trk.push_back(60);
    trk.push_back(0);
    pushEndOfTrack(trk);
    pushChunk(smf, "MTrk", trk);

    const auto parsed = parseMidiBytes(smf);
    REQUIRE(parsed.ok);
    REQUIRE(parsed.file.tracks.size() == 2);
    CHECK(parsed.file.tracks[0].midiChannel == 0);
    CHECK(parsed.file.tracks[0].notes.size() == 1);
    CHECK(parsed.file.tracks[1].midiChannel == 9);
    CHECK(parsed.file.tracks[1].name == "Drums");
    CHECK(parsed.file.tracks[1].notes.size() == 1);
}

TEST_CASE("Malformed, zero-division and note-less MIDI inputs fail cleanly", "[core][midi]")
{
    CHECK_FALSE(parseMidiBytes({}).ok);
    const std::uint8_t garbage[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 0, 0};
    CHECK_FALSE(parseMidiBytes(garbage).ok);

    // Valid header with division == 0
    std::vector<std::uint8_t> zeroDiv;
    std::vector<std::uint8_t> hdr;
    pushBe16(hdr, 1);
    pushBe16(hdr, 1);
    pushBe16(hdr, 0);
    pushChunk(zeroDiv, "MThd", hdr);
    CHECK_FALSE(parseMidiBytes(zeroDiv).ok);

    // UNC / network-share paths are refused without touching SMB.
    CHECK_FALSE(readMidiFile(std::filesystem::path("//server/share/song.mid")).ok);
}
