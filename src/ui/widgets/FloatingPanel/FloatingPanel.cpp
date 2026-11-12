// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/FloatingPanel/FloatingPanel.hpp"

#include <QCloseEvent>
#include <QSettings>
#include <QStringList>
#include <QVBoxLayout>

#if defined(Q_OS_WIN)
#include "ui/Theme.hpp"
#include "ui/WinWindow.hpp"
#include "ui/widgets/WindowButtons/WindowButtons.hpp"

#include <QPainter>
#endif

namespace vloomdaw {

FloatingPanel::FloatingPanel(const QString& title, const QString& settingsKey)
    : QWidget(nullptr, Qt::Window) // minimise, maximise, close and a taskbar button
    , settingsKey_(settingsKey)
{
    setObjectName(QStringLiteral("floatingPanel"));
    setWindowTitle(title); // still shown on the taskbar
    setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    resize(900, 420);

#if defined(Q_OS_WIN)
    removeWindowsTitleBar(this);
    windowButtons_ = new WindowButtons(this);
    windowButtons_->hide();
    connect(windowButtons_, &WindowButtons::minimizeRequested, this, &QWidget::showMinimized);
    connect(windowButtons_, &WindowButtons::maximizeToggled, this,
        [this] { isMaximized() ? showNormal() : showMaximized(); });
    connect(windowButtons_, &WindowButtons::closeRequested, this, &QWidget::close);
#endif

    if (const QByteArray geometry = QSettings().value(settingsKey_).toByteArray(); !geometry.isEmpty())
        geometryRestored_ = restoreGeometry(geometry);
    updateFrame();
}

void FloatingPanel::host(QWidget* content, const std::function<void(QWidget*)>& addToHeader)
{
    if (content_ == content)
        return;
    content_ = content;
    layout()->addWidget(content_);
    content_->show();
#if defined(Q_OS_WIN)
    if (windowButtons_ != nullptr && addToHeader) {
        addToHeader(windowButtons_);
        windowButtons_->show();
    }
#else
    Q_UNUSED(addToHeader) // the native title bar keeps its own buttons
#endif
}

QWidget* FloatingPanel::release()
{
    QWidget* content = content_;
#if defined(Q_OS_WIN)
    if (windowButtons_ != nullptr) {
        // Out of the content's header before it goes back to the main window.
        windowButtons_->hide();
        windowButtons_->setParent(this);
    }
#endif
    if (content != nullptr) {
        layout()->removeWidget(content);
        content->setParent(nullptr);
    }
    content_ = nullptr;
    hide();
    return content;
}

void FloatingPanel::rememberGeometry() { QSettings().setValue(settingsKey_, saveGeometry()); }

void FloatingPanel::closeEvent(QCloseEvent* event)
{
    rememberGeometry();
    event->accept(); // hides the window
    emit closed();
}

void FloatingPanel::hideEvent(QHideEvent* event)
{
    rememberGeometry();
    QWidget::hideEvent(event);
}

void FloatingPanel::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        updateFrame();
}

void FloatingPanel::updateFrame()
{
#if defined(Q_OS_WIN)
    windowButtons_->setMaximized(isMaximized());
    // A 1 px outline keeps the edge visible over dark windows (as on the
    // main window); none when maximised.
    const int border = (isMaximized() || isFullScreen()) ? 0 : 1;
    setContentsMargins(border, border, border, border);
    update();
#endif
}

bool FloatingPanel::isCaptionAt(QPoint pos) const
{
    const QWidget* hit = childAt(pos);
    if (hit == nullptr)
        return false;
    // The header strip and its labels; its buttons stay clickable.
    static const QStringList kCaptionNames{QStringLiteral("panelHeader"), QStringLiteral("mixerHeader"),
        QStringLiteral("panelTitle"), QStringLiteral("panelContext")};
    return kCaptionNames.contains(hit->objectName());
}

#if defined(Q_OS_WIN)
void FloatingPanel::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);
    if (contentsMargins().left() == 0)
        return;
    QPainter painter(this);
    painter.setPen(Theme::colors().border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

bool FloatingPanel::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG"
        && handleWindowsTitleBarMessage(
            this, message, result, [this](QPoint pos) { return isCaptionAt(pos); }))
        return true;
    return QWidget::nativeEvent(eventType, message, result);
}
#endif

} // namespace vloomdaw
