// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

namespace vloomdaw {

class FilterDisplay;

/// The editor of the built-in filter ("vloom.filter"), in the design's
/// smaller window (640 x 420).
///
/// The display draws the filter's response over 20 Hz .. 20 kHz from the
/// effect's own maths (dsp::Filter::magnitudeDb(), through EffectPreview's
/// filterResponseDb()), with a handle at the cutoff: drag it sideways for
/// the cutoff and up or down for the resonance; a double-click puts both
/// back. The panel has the type (Low-pass, High-pass, Band-pass, Notch),
/// cutoff, resonance and drive; the footer the slope and the output peak.
///
/// Differences from the design: the design docks its controls in a bar
/// under the display with a larger "hero" frequency knob; this one keeps the
/// shell's floating panel and same-size knobs. Its "Response" choice has
/// Low-pass and High-pass; the effect also has Band-pass and Notch. The
/// effect has no input or output gain and no envelope follower, so the
/// design's Input, Follower and Output knobs, the follower's dashed curve
/// and its footer reading are left out; Drive (the effect's saturation
/// before the filter) is in the panel, and the output peak is in the
/// footer. The design's "Peak" is the effect's Resonance.
class FilterEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    FilterEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    FilterDisplay* display_ = nullptr;
    Readout* peak_ = nullptr;
};

} // namespace vloomdaw
