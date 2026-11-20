// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"
#include "ui/plugins/builtin/TimeEffectParts.hpp"

namespace vloomdaw {

/// The editor of the built-in phaser ("vloom.phaser").
///
/// The display draws the phaser's notches from 20 Hz to 20 kHz: its
/// response with the all-passes at the centre frequency, filled, and at
/// both ends of the sweep (up to two octaves either way at full depth)
/// dashed, from the phaser's own maths (stages, feedback, mix). The handle
/// on the 0 dB line at the centre drags it sideways. The panel has the rate
/// (a note while synced), depth, centre, feedback, stereo phase and mix;
/// the footer the stages, LFO sync and the output.
///
/// Differs from the design: Width, which the phaser does not have, is
/// Stereo Phase; both ends of the sweep are dashed (the design shows one).
class PhaserEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    PhaserEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void tempoChanged(double bpm) override;
    void meter(const Meter& reading) override;

private:
    void refresh();
    /// The LFO's rate, Hz (the note at the song's tempo while synced).
    [[nodiscard]] double lfoHz() const;

    ResponseDisplay* display_ = nullptr;
    std::optional<OutputLevel> output_;
};

} // namespace vloomdaw
