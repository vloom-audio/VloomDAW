// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/FloatingPanel/FloatingPanel.hpp"

#include <QCloseEvent>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#include "ui/MacWindow.hpp"
#define VLOOMDAW_MAC_CLIENT_TITLE_BAR 1
#endif

#if defined(Q_OS_WIN)
#include "ui/Theme.hpp"
#include "ui/WinWindow.hpp"
#include "ui/widgets/WindowButtons/WindowButtons.hpp"

#include <QPainter>
#endif

namespace vloomdaw {

namespace {

#if defined(VLOOMDAW_MAC_CLIENT_TITLE_BAR)
constexpr int kMacButtonsInset = 14; ///< as on the main window
constexpr int kMacButtonsWidth = 80; ///< inset + three buttons + a gap
#endif

/// Object names of the headers a panel can bring (PanelFrame, MixerPanel).
const QStringList& headerNames()
{
    static const QStringList names{QStringLiteral("panelHeader"), QStringLiteral("mixerHeader")};
    return names;
}

} // namespace

FloatingPanel::FloatingPanel(const QString& title, const QString& settingsKey)
    : QWidget(nullptr, Qt::Window) // minimise, maximise, close and a taskbar button
    , settingsKey_(settingsKey)
{
    setObjectName(QStringLiteral("floatingPanel"));
    // Named after the app, as each window stands on its own in the taskbar,
    // the Dock, window switchers and remote-desktop clients.
    setWindowTitle(QStringLiteral("VLOOM %1").arg(title));
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
#elif defined(VLOOMDAW_MAC_CLIENT_TITLE_BAR)
    // As on the main window: the content runs under the title bar, whose
    // buttons move into the panel's header (adoptHeader()).
    setWindowFlag(Qt::ExpandedClientAreaHint);
    setWindowFlag(Qt::NoTitleBarBackgroundHint);
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    hideMacWindowTitle(this);
    // Native full screen would hide the buttons with nothing to bring them back.
    disableMacNativeFullScreen(this);
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
    Q_UNUSED(addToHeader) // the system's window buttons stay
#endif
    adoptHeader();
}

QWidget* FloatingPanel::release()
{
    releaseHeader();
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
    if (event->type() == QEvent::WindowStateChange) {
        updateFrame();
        placeButtons();
    }
}

void FloatingPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
#if defined(VLOOMDAW_MAC_CLIENT_TITLE_BAR)
    hideMacWindowTitle(this); // in case AppKit set it up again for the new window
#endif
    placeButtons();
}

void FloatingPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeButtons();
}

void FloatingPanel::adoptHeader()
{
#if defined(VLOOMDAW_MAC_CLIENT_TITLE_BAR)
    releaseHeader();
    if (content_ == nullptr)
        return;
    QWidget* header = nullptr;
    for (const QString& name : headerNames())
        if ((header = content_->findChild<QWidget*>(name)) != nullptr)
            break;
    if (header == nullptr || header->layout() == nullptr)
        return;
    header_ = header;

    // Room on the left for the window buttons.
    headerMargins_ = header->layout()->contentsMargins();
    const int headerX = header->mapTo(this, QPoint(0, 0)).x();
    QMargins margins = headerMargins_;
    margins.setLeft(std::max(margins.left(), kMacButtonsWidth - headerX));
    header->layout()->setContentsMargins(margins);

    // The header and its labels move the window; its buttons stay buttons.
    header->installEventFilter(this);
    for (auto* label : header->findChildren<QLabel*>())
        label->installEventFilter(this);
    placeButtons();
#endif
}

void FloatingPanel::releaseHeader()
{
#if defined(VLOOMDAW_MAC_CLIENT_TITLE_BAR)
    if (header_ == nullptr)
        return;
    header_->removeEventFilter(this);
    for (auto* label : header_->findChildren<QLabel*>())
        label->removeEventFilter(this);
    if (header_->layout() != nullptr)
        header_->layout()->setContentsMargins(headerMargins_);
    header_ = nullptr;
#endif
}

void FloatingPanel::placeButtons()
{
#if defined(VLOOMDAW_MAC_CLIENT_TITLE_BAR)
    if (header_ == nullptr || !isVisible())
        return;
    // Centred in the band from the window top to the header's bottom edge.
    const auto place = [this] {
        if (header_ != nullptr)
            placeMacWindowButtons(
                this, header_->mapTo(this, QPoint(0, header_->height())).y(), kMacButtonsInset);
    };
    place();
    // AppKit may lay the title bar out again after this event; repeat once
    // the event loop is idle so our position wins.
    QTimer::singleShot(0, this, place);
#endif
}

bool FloatingPanel::eventFilter(QObject* watched, QEvent* event)
{
#if defined(VLOOMDAW_MAC_CLIENT_TITLE_BAR)
    if (header_ != nullptr && watched->isWidgetType()
        && (watched == header_.data() || static_cast<QWidget*>(watched)->parentWidget() == header_.data())) {
        if (event->type() == QEvent::MouseButtonPress
            && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton && windowHandle() != nullptr) {
            windowHandle()->startSystemMove();
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            isMaximized() ? showNormal() : showMaximized(); // the zoom a title bar double-click does
            return true;
        }
    }
#endif
    return QWidget::eventFilter(watched, event);
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
    static const QStringList kLabelNames{QStringLiteral("panelTitle"), QStringLiteral("panelContext")};
    return headerNames().contains(hit->objectName()) || kLabelNames.contains(hit->objectName());
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
