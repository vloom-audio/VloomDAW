// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"
#include "ui/plugins/builtin/TimeEffectParts.hpp"

namespace vloomdaw {

class EchoDisplay;

/// The editor of the built-in delay ("vloom.delay").
///
/// The display draws what a centred hit becomes: the dry sound, then each
/// echo on the left (above the line) and the right (below), as the mode,
/// the times, feedback, width and mix make them (heights in dB, 42 dB to
/// nothing), with the feedback's decay dashed. The handle on that decay
/// at the second echo drags the time (sideways, to the nearest note while
/// synced) and the feedback (up and down). The panel has Sync and Ping-pong, the time (a note while synced),
/// feedback, its low and high cut, width and mix; the footer the tempo, the
/// delay that makes, the stereo offset and the output.
///
/// Differs from the design: the handle sits on the second echo (the first
/// is as loud whatever the feedback, so it could not set it); Stereo Offset,
/// which the design leaves out, reads in the footer; the dry bar and the
/// echoes are as loud as the mix makes them, in dB (the design draws them
/// linearly, the first echo nearly as tall as the dry sound); note names
/// read as the plugin gives them ("1/8D", the design "1/8 dot").
class DelayEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    DelayEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void tempoChanged(double bpm) override;
    void meter(const Meter& reading) override;

private:
    /// The delay of the left side (false) or the right, in seconds, as
    /// dsp::Delay::delaySeconds() works it out.
    [[nodiscard]] double delaySeconds(bool right) const;
    void refresh();
    /// The second echo's handle dragged to `seconds` and `feedback` (0 .. 1;
    /// nullopt: not known, the mix is 0).
    void dragged(double seconds, std::optional<double> feedback);

    EchoDisplay* display_ = nullptr;
    Readout* tempo_ = nullptr;
    Readout* delay_ = nullptr;
    std::optional<OutputLevel> output_;
};

} // namespace vloomdaw
