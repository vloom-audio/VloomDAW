// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"
#include "ui/plugins/builtin/TimeEffectParts.hpp"

namespace vloomdaw {

class VoiceDisplay;

/// The editor of the built-in chorus ("vloom.chorus").
///
/// The display draws each voice's delay over a few cycles of the LFO, in
/// its own colour: from Delay up to Delay + Depth x 8 ms, each voice at its
/// own phase, at the rate (or the note at the song's tempo, with Sync). The
/// panel has the rate (a note while synced), depth, delay, feedback, width
/// and mix; the footer the voices, the stereo spread, LFO sync and the output.
///
/// Differs from the design: the voices sweep up from Delay (the design
/// swings them round it), on a fixed scale of milliseconds so depth shows;
/// their numbers sit at the left, not on a playhead (the plugin does not
/// report its LFO's phase); a fourth voice, Spread and Sync, which the
/// design leaves out, are there.
class ChorusEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    ChorusEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void tempoChanged(double bpm) override;
    void meter(const Meter& reading) override;

private:
    void refresh();

    VoiceDisplay* display_ = nullptr;
    std::optional<OutputLevel> output_;
};

} // namespace vloomdaw
