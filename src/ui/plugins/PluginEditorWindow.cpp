// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginEditorWindow.hpp"

#include <vloom/engine/plugin/PluginInstance.hpp>

#include <QCloseEvent>
#include <QGuiApplication>
#include <QVBoxLayout>

#include <cmath>

namespace vloomdaw {

namespace {

/// The size used while a plugin gives none.
constexpr vloom::EditorSize kFallbackSize{480, 320};

} // namespace

PluginEditorWindow::PluginEditorWindow(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , instance_(std::move(instance))
{
    setObjectName(QStringLiteral("pluginEditor"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    view_ = new QWidget(this);
    view_->setAttribute(Qt::WA_NativeWindow); // the plugin needs a window of its own to draw into
    view_->setAttribute(Qt::WA_DontCreateNativeAncestors);
    layout->addWidget(view_);
}

PluginEditorWindow::~PluginEditorWindow() { closeEditor(); }

bool PluginEditorWindow::open(QString* error)
{
    vloom::AudioProcessor& processor = instance_->processor();
    if (!processor.hasEditor()) {
        if (error != nullptr)
            *error = tr("This plugin has no editor of its own.");
        return false;
    }
#if defined(Q_OS_LINUX)
    if (QGuiApplication::platformName() != QLatin1String("xcb")) {
        if (error != nullptr)
            *error = tr("Plugin editors need X11. Start the app with QT_QPA_PLATFORM=xcb (XWayland).");
        return false;
    }
#endif
    fitTo(processor.editorSize());
    show();
    // The plugin's parent: HWND, NSView* or an X11 window id.
    void* parent = reinterpret_cast<void*>(view_->winId()); // NOLINT(performance-no-int-to-ptr)
    if (!processor.openEditor(parent, this)) {
        if (error != nullptr)
            *error = tr("The plugin would not open its editor.");
        return false;
    }
    fitTo(processor.editorSize()); // known for sure only once open
    raise();
    activateWindow();
    return true;
}

void PluginEditorWindow::closeEvent(QCloseEvent* event)
{
    closeEditor();
    QWidget::closeEvent(event);
}

void PluginEditorWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    vloom::AudioProcessor& processor = instance_->processor();
    if (fitting_ || !processor.isEditorOpen() || !processor.canResizeEditor())
        return;
    const vloom::EditorSize wanted = toPlugin(view_->size());
    const vloom::EditorSize got = processor.setEditorSize(wanted);
    if (got.width != wanted.width || got.height != wanted.height)
        fitTo(got); // the plugin has its own steps or limits
}

void PluginEditorWindow::editorResizeRequested(vloom::EditorSize size)
{
    fitTo(size);
    instance_->processor().setEditorSize(size);
}

void PluginEditorWindow::editorClosed()
{
    // The plugin closed its editor itself: no closeEditor() back to it.
    QMetaObject::invokeMethod(this, &QWidget::close, Qt::QueuedConnection);
}

qreal PluginEditorWindow::pluginScale() const
{
#if defined(Q_OS_MACOS)
    return 1.0;
#else
    return devicePixelRatioF();
#endif
}

QSize PluginEditorWindow::toLogical(vloom::EditorSize size) const
{
    const qreal scale = pluginScale();
    return {static_cast<int>(std::lround(size.width / scale)),
        static_cast<int>(std::lround(size.height / scale))};
}

vloom::EditorSize PluginEditorWindow::toPlugin(QSize size) const
{
    const qreal scale = pluginScale();
    return {static_cast<int>(std::lround(size.width() * scale)),
        static_cast<int>(std::lround(size.height() * scale))};
}

void PluginEditorWindow::fitTo(vloom::EditorSize size)
{
    if (size.width <= 0 || size.height <= 0)
        size = kFallbackSize;
    fitting_ = true;
    const QSize logical = toLogical(size);
    if (instance_->processor().canResizeEditor()) {
        setMinimumSize(1, 1);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        resize(logical);
    } else {
        setFixedSize(logical);
    }
    fitting_ = false;
}

void PluginEditorWindow::closeEditor()
{
    if (instance_ != nullptr && instance_->processor().isEditorOpen())
        instance_->processor().closeEditor();
}

} // namespace vloomdaw
