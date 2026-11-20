// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

namespace vloomdaw {

class TransientDisplay;

/// The editor of the built-in transient shaper ("vloom.transient").
///
/// The display draws the level of four drum-like hits (a 2 ms rise, a
/// 120 ms decay, one every half second) before the shaper, in grey, and
/// after it, in the accent: the effect itself run offline on them
/// (EffectPreview's transientPreview()), so attack, sustain, speed, output
/// and mix all show. Levels are drawn mirrored about the centre, full
/// scale at the top (the hits peak at -6 dB; a bigger boost is cut off).
/// The panel has attack, sustain, output and mix; the footer the speed
/// and the measured output peak.
///
/// Differences from the design: the design scrolls the envelope of the
/// playing sound; the engine only reports the output, so the input
/// cannot be shown next to it, and the display shows the effect on a
/// fixed set of hits instead. Attack and sustain read in % (-100 .. +100,
/// the effect's parameters; +100 % is up to +18 dB), not dB. Speed reads
/// in % rather than "Fast".
class TransientEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    TransientEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    TransientDisplay* display_ = nullptr;
    Readout* peak_ = nullptr;
};

} // namespace vloomdaw
