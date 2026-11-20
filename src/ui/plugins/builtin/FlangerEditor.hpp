// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"
#include "ui/plugins/builtin/TimeEffectParts.hpp"

namespace vloomdaw {

/// The editor of the built-in flanger ("vloom.flanger").
///
/// The display draws the flanger's comb up to 8 kHz: its response with the
/// delay at Delay, filled, and at the far end of the sweep (Delay + Depth x
/// 5 ms) dashed, from the flanger's own maths (mix, feedback and all). The
/// handle on the 0 dB line at the lowest notch drags the delay sideways.
/// The panel has the rate (a note while synced), depth, delay, feedback,
/// stereo phase and mix; the footer LFO sync and the output.
///
/// Differs from the design: Width, which the flanger does not have, is
/// Stereo Phase; the handle sits on the 0 dB line at the lowest notch (the
/// notch itself is too deep to hold one).
class FlangerEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    FlangerEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
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
