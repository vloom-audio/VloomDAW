// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/PluginThumbnails.hpp"

#include <QWidget>

#include <memory>

namespace vloom {
class PluginInstance;
}

namespace vloomdaw {

/// A window with a control for every parameter of a plugin that has no
/// editor of its own (the built-in effects): a knob per value, a check box
/// per switch, in a column per group (ParameterInfo::group: a band of an
/// equalizer). Frequencies (unit "Hz") turn in octaves.
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
    /// A parameter was changed: the project has unsaved changes.
    void edited();

private:
    std::shared_ptr<vloom::PluginInstance> instance_;
    PluginEditorHeader* header_ = nullptr;
};

} // namespace vloomdaw
