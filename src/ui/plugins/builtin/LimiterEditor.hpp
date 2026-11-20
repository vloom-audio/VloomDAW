// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

namespace vloomdaw {

class LevelHistoryView;

/// The editor of the built-in limiter ("vloom.limiter").
///
/// The display scrolls the last few seconds: the input level (worked out
/// from the output, the gain reduction and the input gain), the limited
/// output and the gain reduction from the top, with the ceiling as a dashed
/// line to drag. The panel has the input gain, the ceiling, release,
/// lookahead and true peak; the footer the latency, the stereo link and the
/// gain reduction.
class LimiterEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    LimiterEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    void showLatency();

    LevelHistoryView* levels_ = nullptr;
    Readout* latency_ = nullptr;
    Readout* reduction_ = nullptr;
};

} // namespace vloomdaw
