// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/SpectrumAnalyzer.hpp"
#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

#include <vector>

namespace vloomdaw {

class DeEsserDisplay;

/// The editor of the built-in de-esser ("vloom.deesser").
///
/// The display draws, over 20 Hz .. 20 kHz, the spectrum of what the
/// de-esser puts out (in the accent) and, in grey, that spectrum with the
/// gain it is taking off added back (EffectPreview's deEsserGainDb(): the
/// split-band shelf or the wideband cut, at the reduction it reports now):
/// an estimate of what came in. The band the detector listens to (its
/// high-pass at Frequency, up to the top) is shaded, with the reduction
/// over it, and the threshold is a dashed line with a handle at the band's
/// edge: drag it sideways for the frequency and up or down for the
/// threshold; a double-click puts both back. No audio: no spectrum.
/// The panel has frequency, threshold and range, and Listen; the footer the
/// mode and the gain reduction.
///
/// Differences from the design: the design's band has two edges and a
/// Width knob; the effect detects everything above Frequency (a 12 dB per
/// octave high-pass) and has no width, so the band runs to the top and the
/// knob is left out. The engine reports the output only, so the grey
/// "input" is worked out from it, and not drawn while Listen plays the band.
/// The design's "Band only" mode is the effect's "Split-band" (the other
/// is "Wideband").
class DeEsserEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    DeEsserEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    SpectrumAnalyzer analyzer_;
    std::vector<float> scope_;
    DeEsserDisplay* display_ = nullptr;
    Readout* reduction_ = nullptr;
};

} // namespace vloomdaw
