// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/PluginThumbnails.hpp"

#include <vloom/engine/plugin/AudioProcessor.hpp>

#include <QWidget>

#include <cstdint>
#include <memory>

namespace vloom {
class PluginInstance;
}

namespace vloomdaw {

/// A window holding a plugin's own editor (vloom-engine ADR 0009): a native
/// child widget the plugin draws into, topped by a Studio One-style header bar
/// for bypass, slot actions and thumbnail capture. Sized as the plugin asks,
/// and resizable by the user when the plugin allows it.
///
/// Deletes itself when closed, by the user or by the plugin. Closing it
/// closes the editor; it must be closed before its instance is dropped.
class PluginEditorWindow : public QWidget, private vloom::EditorListener {
    Q_OBJECT

public:
    PluginEditorWindow(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, QWidget* parent,
        PluginEditorContext context = {});
    ~PluginEditorWindow() override;
    PluginEditorWindow(const PluginEditorWindow&) = delete;
    PluginEditorWindow& operator=(const PluginEditorWindow&) = delete;

    void setEditorContext(PluginEditorContext context);
    void setBypassedState(bool bypassed);

    /// Shows the window and opens the editor in it. False, with `error`
    /// set, if the plugin has no editor or would not open it.
    bool open(QString* error);

signals:
    /// A parameter was changed in the editor: the project has unsaved changes.
    void edited();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void editorResizeRequested(vloom::EditorSize size) override;
    void editorClosed() override;
    void parameterEdited(std::uint32_t id, double value) override;

    /// Plugin sizes are pixels on Windows and Linux, points on macOS.
    [[nodiscard]] qreal pluginScale() const;
    [[nodiscard]] QSize toLogical(vloom::EditorSize size) const;
    [[nodiscard]] vloom::EditorSize toPlugin(QSize size) const;
    /// Sizes the window to hold an editor of `size`.
    void fitTo(vloom::EditorSize size);
    void closeEditor();

    std::shared_ptr<vloom::PluginInstance> instance_;
    PluginEditorHeader* header_ = nullptr;
    QWidget* view_ = nullptr; ///< native; the plugin's parent window
    bool fitting_ = false; ///< resizing to the plugin's size: not a user resize
};

} // namespace vloomdaw
