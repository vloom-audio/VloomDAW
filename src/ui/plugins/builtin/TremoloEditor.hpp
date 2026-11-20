// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"
#include "ui/plugins/builtin/TimeEffectParts.hpp"

namespace vloomdaw {

class GainDisplay;

/// The editor of the built-in tremolo and auto-pan ("vloom.tremolo").
///
/// The display draws the gain of the left (teal) and the right (pink) side
/// over a few cycles of the LFO, from 0 dB at the top to silence at the
/// bottom, as the mode, shape, depth and stereo phase make it, at the rate
/// (or the note at the song's tempo, with Sync). The panel has the mode
/// (Tremolo or Pan), the rate (a note while synced), depth, shape and
/// stereo phase (not used by Pan, so dimmed then); the footer Sync and the
/// output.
///
/// Differs from the design: Shape is a choice of sine, triangle and square
/// (the design has a 0 .. 100 % amount), and reads its name.
class TremoloEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    TremoloEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void tempoChanged(double bpm) override;
    void meter(const Meter& reading) override;

private:
    void refresh();

    GainDisplay* display_ = nullptr;
    std::optional<OutputLevel> output_;
};

} // namespace vloomdaw
