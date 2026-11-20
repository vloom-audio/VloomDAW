// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

namespace vloomdaw {

class ChoiceButtons;
class SaturatorDisplay;

/// The editor of the built-in saturator ("vloom.saturator").
///
/// The display draws, on the left, the shaping curve, output against input
/// (EffectPreview's saturatorCurve(): the curve, drive, output and mix),
/// with a handle at -6 dB in to drag the drive up or down; on the right a
/// 100 Hz sine before (dashed) and after the effect itself, run offline
/// (tone, oversampling and DC blocker included). The panel has the curve
/// (Soft, Tube, Tape, Hard), drive, tone, output and mix; the footer the
/// oversampling, the latency it brings and the output peak.
///
/// Differences from the design: the design's "Character" offers Soft, Tube
/// and Tape; the effect also has Hard. Tone reads in % (-100 .. +100, the
/// effect's tilt) rather than a bare number. The footer's Output (a
/// second copy of the knob) became the measured output peak, and the
/// latency joined the oversampling. The curve's axes run to +-1.25, past
/// full scale (dashed), since the tube curve and the output gain go beyond it.
class SaturatorEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    SaturatorEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    void showLatency();

    SaturatorDisplay* display_ = nullptr;
    Readout* latency_ = nullptr;
    Readout* peak_ = nullptr;
};

} // namespace vloomdaw
