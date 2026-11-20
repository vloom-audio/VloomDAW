// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

namespace vloomdaw {

class UtilityDisplay;

/// The editor of the built-in utility ("vloom.utility").
///
/// The display draws what the settings do to a stereo sound, worked out
/// from the effect itself (EffectPreview's utilityMatrix(): swap, invert,
/// width, mono, gain and balance):
/// - on the left, the gain of each side for a sound in the middle;
/// - in the centre, a stereo field (mid up, side across, L and R on the
///   diagonals) with a line for each of a fan of sounds panned from hard
///   left to hard right, where the settings put them: narrower with less
///   width, a single upright line in mono, lying across when one side is
///   inverted, leaning with the balance, shorter with less gain;
/// - on the right, the balance, the width and each side's polarity.
/// The panel has gain, pan, width, Mono, Invert L and Invert R; the footer
/// Swap, DC block and the output peak.
///
/// Differences from the design: the design's level bars, scope and
/// correlation meter need each side's signal, but the engine reports the
/// output with left and right averaged; so the bars show each side's gain,
/// the field shows where the settings put a fan of sounds, the correlation
/// meter became balance, width and polarity readings, and the footer's
/// Peak L / Peak R / Correlation became the output peak. Swap and DC block,
/// which the design does not show, are in the footer.
class UtilityEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    UtilityEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    UtilityDisplay* display_ = nullptr;
    Readout* peak_ = nullptr;
};

} // namespace vloomdaw
