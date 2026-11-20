// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

namespace vloomdaw {

class LevelHistoryView;

/// The editor of the built-in noise gate ("vloom.gate").
///
/// The display scrolls the last few seconds: the input level in grey
/// (worked out from the output and how far the gate turned it down) and
/// what the gate lets through in the accent, with the threshold as a dashed
/// line to drag. The panel has threshold, range, attack, hold, release and
/// hysteresis; the footer the detector's low cut (the sidechain filter),
/// Listen and whether the gate is open.
class GateEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    GateEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    void meter(const Meter& reading) override;

private:
    LevelHistoryView* levels_ = nullptr;
    Readout* state_ = nullptr;
};

} // namespace vloomdaw
