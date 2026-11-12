// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/BrowserFilter.hpp"

#include <catch2/catch_test_macros.hpp>

using vloomdaw::isAudioFile;
using vloomdaw::matchesSearch;

TEST_CASE("Audio files are recognised by extension, ignoring case", "[core][browser]")
{
    CHECK(isAudioFile("kick.wav"));
    CHECK(isAudioFile("Kick.WAV"));
    CHECK(isAudioFile("pad.aiff"));
    CHECK(isAudioFile("pad.Aif"));
    CHECK(isAudioFile("loop.flac"));
    CHECK(isAudioFile("vox.mp3"));
    CHECK(isAudioFile("vox.ogg"));
    CHECK_FALSE(isAudioFile("notes.txt"));
    CHECK_FALSE(isAudioFile("wav")); // no extension
    CHECK_FALSE(isAudioFile(".wav")); // a hidden file, not a sample
    CHECK_FALSE(isAudioFile("kick.wav.bak"));
    CHECK_FALSE(isAudioFile("kick.wave"));
}

TEST_CASE("No search shows every audio file", "[core][browser]")
{
    CHECK(matchesSearch("kick.wav", ""));
    CHECK(matchesSearch("kick.wav", "   "));
}

TEST_CASE("Search terms match anywhere in the name, ignoring case", "[core][browser]")
{
    CHECK(matchesSearch("Big Kick 01.wav", "kick"));
    CHECK(matchesSearch("Big Kick 01.wav", "KICK"));
    CHECK(matchesSearch("Big Kick 01.wav", "g k"));
    CHECK_FALSE(matchesSearch("Big Snare 01.wav", "kick"));
}

TEST_CASE("Digits and spaces are kept, surrounding spaces trimmed", "[core][browser]")
{
    CHECK(matchesSearch("808 a.wav", "  808 a "));
    CHECK_FALSE(matchesSearch("808a.wav", "808 a"));
}

TEST_CASE("The extension is not searched", "[core][browser]")
{
    CHECK_FALSE(matchesSearch("kick.wav", "wav"));
    CHECK(matchesSearch("wav kick.wav", "wav"));
}

TEST_CASE("Typed wildcards cannot change the match", "[core][browser]")
{
    CHECK(matchesSearch("x.wav", "*[x]?"));
    CHECK_FALSE(matchesSearch("y.wav", "*[x]?"));
    CHECK(matchesSearch("anything.wav", "**"));
}
