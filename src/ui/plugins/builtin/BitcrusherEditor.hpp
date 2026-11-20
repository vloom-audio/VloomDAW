// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

namespace vloomdaw {

class BitcrusherDisplay;

/// The editor of the built-in bitcrusher ("vloom.bitcrusher").
///
/// The display draws a sine before (dashed) and after the effect itself,
/// run offline (EffectPreview's crusherPreview(): slow enough that the
/// held samples show as steps), with grid lines at the quantizer's levels
/// when they are far enough apart to see. The panel has bit depth,
/// downsample, jitter, drive and mix, and the anti-alias filter; the footer
/// the dither, the rate the downsampling leaves, the output and its peak.
///
/// Differences from the design: "Sample rate" is the effect's Downsample
/// factor (x1 .. x64; the rate it leaves reads in the footer), and the
/// design's "Filter" frequency knob is the Anti-alias switch (the effect
/// puts its filter at the reduced rate's Nyquist frequency itself). Jitter
/// has a knob the design does not show. Dither is a 0 .. 100 % amount, not
/// a switch ("Off" at 0). The output peak joined the footer.
class BitcrusherEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    BitcrusherEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    BitcrusherDisplay* display_ = nullptr;
    Readout* rate_ = nullptr;
    Readout* peak_ = nullptr;
};

} // namespace vloomdaw
