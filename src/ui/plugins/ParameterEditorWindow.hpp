// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/PluginThumbnails.hpp"

#include <QWidget>

#include <memory>
#include <vector>

class QLabel;
class QTimer;

namespace vloom {
class PluginInstance;
}

namespace vloomdaw {

class SampleSlotControl;

/// A window with a control for every parameter of a plugin that has no
/// editor of its own (the built-in effects): a knob per value, a check box
/// per switch, a list per choice (ParameterInfo::valueLabels: a mode, a
/// note length), in a column per group (ParameterInfo::group: a band of an
/// equalizer). Frequencies (unit "Hz") turn in octaves.
///
/// A processor that plays samples (the built-in sampler and drum sampler)
/// gets a "Load sample…" button per slot, at the top of the column of the
/// group with the slot's name ("Sample", "Pad 3"); an audio file dragged from
/// the browser onto it loads too. One that meters its gain reduction shows it.
///
/// Shows the values the plugin reports (AudioProcessor::parameterValue());
/// one that reports none starts at its defaults. Deletes itself when closed.
class ParameterEditorWindow : public QWidget {
    Q_OBJECT

public:
    ParameterEditorWindow(std::shared_ptr<vloom::PluginInstance> instance, const QString& title,
        QWidget* parent, PluginEditorContext context = {});

    void setEditorContext(PluginEditorContext context);
    void setBypassedState(bool bypassed);

signals:
    /// A parameter was changed or a sample loaded: the project has unsaved changes.
    void edited();

private:
    /// Loads `path` into sample slot `slot` (UI thread: decoding a non-WAV
    /// file blocks for a moment, as anywhere else in the app).
    void loadSample(int slot, const QString& path);
    void refreshSampleSlots();

    std::shared_ptr<vloom::PluginInstance> instance_;
    PluginEditorHeader* header_ = nullptr;
    std::vector<SampleSlotControl*> slots_;
    QLabel* reduction_ = nullptr; ///< the gain reduction meter, if it has one
    QTimer* meterTimer_ = nullptr;
};

} // namespace vloomdaw
