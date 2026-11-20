// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"
#include "ui/plugins/builtin/TimeEffectParts.hpp"

namespace vloomdaw {

class DecayDisplay;

/// The editor of the built-in reverb ("vloom.reverb").
///
/// The display draws what a hit becomes over time, to scale: the dry
/// sound, the pre-delay, the first trips round the reverb's lines (as Size
/// spaces them and Diffusion smears them) and the tail falling by 60 dB in
/// the decay time, with the high frequencies (8 kHz), which Damping makes
/// die sooner, dashed. Freeze holds the tail level. The handle on the tail
/// drags the decay sideways. The panel has size, decay, pre-delay,
/// diffusion, damping, width, mix and Freeze; the footer the low and high
/// cut of the reverb and the output.
///
/// Differs from the design: the pre-delay and the reflections are drawn
/// to scale (the design stretches them); Diffusion and Freeze, which the
/// design leaves out, are in the panel.
class ReverbEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    ReverbEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    void refresh();

    DecayDisplay* display_ = nullptr;
    std::optional<OutputLevel> output_;
};

} // namespace vloomdaw
