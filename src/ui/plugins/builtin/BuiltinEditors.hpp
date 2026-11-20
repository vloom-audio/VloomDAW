// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>

#include <memory>

class QWidget;

namespace vloom {
class PluginInstance;
}

namespace vloomdaw {

class BuiltinEditorWindow;

/// Whether the built-in `instance` has a dedicated editor on the
/// BuiltinEditorWindow shell (the compressor, the limiter, the gate...).
[[nodiscard]] bool hasBuiltinEditor(const vloom::PluginInstance& instance);

/// Makes the dedicated editor of `instance`, or null for a plugin that has
/// none (it then gets ParameterEditorWindow). `sampleRate`: the engine's.
/// Add an editor to the table in BuiltinEditors.cpp (docs/builtin-editors.md).
[[nodiscard]] BuiltinEditorWindow* createBuiltinEditor(std::shared_ptr<vloom::PluginInstance> instance,
    const QString& title, double sampleRate, QWidget* parent);

} // namespace vloomdaw
