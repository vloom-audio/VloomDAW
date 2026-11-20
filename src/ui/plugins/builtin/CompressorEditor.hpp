// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/DynamicsMeter.hpp"
#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

namespace vloomdaw {

class LevelHistoryView;
class MeterBars;
class TransferCurve;

/// The editor of the built-in compressor ("vloom.compressor").
///
/// The display scrolls the last few seconds: the output level, the gain
/// reduction from the top and the input level (worked out from those two
/// and the makeup; not drawn while the mix blends in the dry sound or
/// Listen plays the key), with the threshold as a dashed line to drag. An
/// inset draws the transfer curve with a dot at the input level, and In /
/// GR / Out meters stand at the right. The panel has threshold, ratio, knee,
/// attack, release, makeup and mix, and auto makeup; the footer the
/// detector's low cut (the sidechain filter), Listen, the detector mode,
/// the stereo link and the gain reduction.
///
/// Animated: the levels scroll smoothly with a glowing output trace, the
/// gain reduction hangs as a breathing curtain from the top, the transfer
/// curve's dot leaves a trail, and the threshold line flashes when the
/// input rises over it.
class CompressorEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    CompressorEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    /// The makeup added after the curve (with auto makeup), dB.
    [[nodiscard]] double makeupDb() const;

    LevelHistoryView* levels_ = nullptr;
    TransferCurve* curve_ = nullptr;
    MeterBars* meters_ = nullptr;
    Readout* reduction_ = nullptr;
    double lastInput_ = kNoLevel; ///< the input at the last reading: the threshold's crossings
};

} // namespace vloomdaw
