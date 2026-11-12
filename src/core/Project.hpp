// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace vloomdaw {

// Musical time in ticks, the same resolution as vloom-engine (960 PPQ).
inline constexpr int kTicksPerBeat = 960;
inline constexpr int kBeatsPerBar = 4; ///< 4/4 until time signatures arrive
inline constexpr int kTicksPerBar = kTicksPerBeat * kBeatsPerBar;
inline constexpr int kStepsPerBar = 16; ///< channel rack steps are 1/16 notes
inline constexpr int kTicksPerStep = kTicksPerBar / kStepsPerBar;
inline constexpr int kMinNoteLength = kTicksPerStep / 4; ///< 1/64
inline constexpr int kLowestKey = 0; ///< MIDI note numbers
inline constexpr int kHighestKey = 127;

/// A note in the piano roll; positions in ticks from the pattern start.
struct Note {
    int key = 60; ///< MIDI note number, 60 = C5 (middle C)
    int start = 0;
    int length = kTicksPerStep;

    [[nodiscard]] int end() const { return start + length; }
    friend bool operator==(const Note&, const Note&) = default;
};

/// What one channel plays in one pattern: step sequencer and piano roll.
struct ChannelPattern {
    std::vector<bool> steps = std::vector<bool>(kStepsPerBar, false);
    std::vector<Note> notes;
};

struct Pattern {
    std::string name;
    std::vector<ChannelPattern> channels; ///< index = channel index
};

/// An instrument line in the channel rack.
struct Channel {
    std::string name;
    bool muted = false;
    std::string samplePath; ///< UTF-8 path of the sample it plays; empty = none (silent)
};

/// A pattern placed in the playlist. Its length is the pattern's length.
struct Clip {
    std::size_t pattern = 0;
    int track = 0; ///< 0-based playlist track
    int start = 0; ///< ticks from the song start

    friend bool operator==(const Clip&, const Clip&) = default;
};

/// The song: channels, patterns (steps and notes per channel) and the
/// playlist, as in FL Studio. Settings only: audio/ChannelRackAudio turns
/// the channel rack into sound.
///
/// Indices are 0-based. Every method that takes an index ignores (or
/// returns empty for) one out of range, and every position, key and length
/// is clamped to a valid value, so the UI cannot corrupt the model.
class Project {
public:
    static constexpr int kPlaylistTracks = 16;

    /// A new song: four channels (Kick, Clap, Hat, Snare) and one pattern.
    Project();

    [[nodiscard]] const std::vector<Channel>& channels() const { return channels_; }
    [[nodiscard]] const std::vector<Pattern>& patterns() const { return patterns_; }
    [[nodiscard]] const std::vector<Clip>& clips() const { return clips_; }

    // ---- channels
    /// Adds a channel to every pattern; returns its index.
    std::size_t addChannel(const std::string& name);
    void setChannelMuted(std::size_t channel, bool muted);
    void setChannelName(std::size_t channel, const std::string& name);
    /// The sample the channel plays (UTF-8 path; empty = none).
    void setChannelSample(std::size_t channel, const std::string& path);

    // ---- patterns
    /// Appends "Pattern N"; returns its index.
    std::size_t addPattern();
    /// Removes a pattern (never the last one) and its clips; later patterns
    /// move down one index, like the pattern selector renumbers them.
    void removePattern(std::size_t pattern);
    /// Length in ticks: whole bars covering the steps and every note, at
    /// least one bar.
    [[nodiscard]] int patternLength(std::size_t pattern) const;

    /// Steps in the pattern's length (16 per bar).
    [[nodiscard]] int patternSteps(std::size_t pattern) const
    {
        return patternLength(pattern) / kTicksPerStep;
    }

    // ---- step sequencer
    [[nodiscard]] bool step(std::size_t pattern, std::size_t channel, int step) const;
    void setStep(std::size_t pattern, std::size_t channel, int step, bool on);
    void toggleStep(std::size_t pattern, std::size_t channel, int step);

    // ---- piano roll
    [[nodiscard]] const std::vector<Note>* notes(std::size_t pattern, std::size_t channel) const;
    /// Adds a note (clamped); returns its index.
    std::optional<std::size_t> addNote(std::size_t pattern, std::size_t channel, Note note);
    /// Replaces note `index` (clamped), e.g. after a move or resize.
    void setNote(std::size_t pattern, std::size_t channel, std::size_t index, Note note);
    void removeNote(std::size_t pattern, std::size_t channel, std::size_t index);

    // ---- playlist
    /// Places `pattern` on `track` at `start` (clamped); returns its index.
    std::optional<std::size_t> addClip(Clip clip);
    void setClip(std::size_t index, Clip clip);
    void removeClip(std::size_t index);
    [[nodiscard]] int clipLength(const Clip& clip) const { return patternLength(clip.pattern); }

    /// Clamps a note to the valid key range, start >= 0 and length >= kMinNoteLength.
    [[nodiscard]] static Note clamped(Note note);

private:
    [[nodiscard]] ChannelPattern* channelPattern(std::size_t pattern, std::size_t channel);
    [[nodiscard]] const ChannelPattern* channelPattern(std::size_t pattern, std::size_t channel) const;
    [[nodiscard]] Clip clamped(Clip clip) const;

    std::vector<Channel> channels_;
    std::vector<Pattern> patterns_;
    std::vector<Clip> clips_;
};

} // namespace vloomdaw
