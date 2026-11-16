// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerSlotRack.hpp"

#include "ui/Theme.hpp"
#include "ui/plugins/PluginDrag.hpp"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kSwitchWidth = 14;
constexpr int kScrollbarStripWidth = 5;

/// Left switch button of an FX slot: shows a green triangle when active,
/// muted dim triangle when bypassed, and a small dark square when empty.
class FxSwitchButton : public QWidget {
    Q_OBJECT

public:
    explicit FxSwitchButton(std::size_t slot, QWidget* parent = nullptr)
        : QWidget(parent)
        , slot_(slot)
    {
        setFixedSize(kSwitchWidth, MixerSlotRack::kSlotHeight);
        setFocusPolicy(Qt::NoFocus);
        updateTip();
    }

    void setState(bool hasPlugin, bool bypassed, const QString& name)
    {
        hasPlugin_ = hasPlugin;
        bypassed_ = bypassed;
        name_ = name;
        updateTip();
        update();
    }

signals:
    void toggled(bool bypassed);
    void menuRequested(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        const QColor bg = hover_ ? QColor(0x1e, 0x22, 0x26) : QColor(0x14, 0x17, 0x1a);
        p.fillRect(rect(), bg);

        p.setPen(QColor(0x10, 0x12, 0x14));
        p.drawLine(width() - 1, 0, width() - 1, height() - 1);

        p.setPen(QColor(0x12, 0x14, 0x16));
        p.drawLine(0, height() - 1, width() - 1, height() - 1);

        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        if (hasPlugin_) {
            if (!bypassed_) {
                p.setBrush(QColor(0x00, 0xe6, 0x76)); // Vibrant mint green ▶
            } else {
                p.setBrush(QColor(0x55, 0x62, 0x5b)); // Dim grey-green ▶
            }
            QPolygonF triangle;
            triangle << QPointF(3.5, 3.0) << QPointF(3.5, 11.0) << QPointF(10.0, 7.0);
            p.drawPolygon(triangle);
        } else {
            p.setBrush(QColor(0x32, 0x36, 0x3a)); // Empty slot square ■
            p.drawRect(QRectF(5.0, 5.0, 4.0, 4.0));
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (hasPlugin_)
                emit toggled(!bypassed_);
        } else if (event->button() == Qt::RightButton) {
            emit menuRequested(mapToGlobal(event->pos()));
        }
    }

    void enterEvent(QEnterEvent* /*event*/) override
    {
        hover_ = true;
        update();
    }

    void leaveEvent(QEvent* /*event*/) override
    {
        hover_ = false;
        update();
    }

private:
    void updateTip()
    {
        if (!hasPlugin_) {
            setToolTip(tr("Slot %1 (scroll wheel for more slots)").arg(slot_ + 1));
        } else if (bypassed_) {
            setToolTip(tr("Enable %1 (Slot %2)").arg(name_).arg(slot_ + 1));
        } else {
            setToolTip(tr("Bypass %1 (Slot %2)").arg(name_).arg(slot_ + 1));
        }
    }

    std::size_t slot_ = 0;
    bool hasPlugin_ = false;
    bool bypassed_ = false;
    bool hover_ = false;
    QString name_;
};

/// Slot button of an FX slot: shows the plugin name on light mint green
/// when active, dark muted green when bypassed, or a dash when empty.
class FxSlotButton : public QWidget {
    Q_OBJECT

public:
    explicit FxSlotButton(std::size_t slot, QWidget* parent = nullptr)
        : QWidget(parent)
        , slot_(slot)
    {
        setFixedHeight(MixerSlotRack::kSlotHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAcceptDrops(true);
        updateTip();
    }

    void setState(bool hasPlugin, bool bypassed, const QString& name)
    {
        hasPlugin_ = hasPlugin;
        bypassed_ = bypassed;
        name_ = name;
        updateTip();
        update();
    }

signals:
    void clicked();
    void menuRequested(const QPoint& globalPos);
    void pluginDropped(std::size_t plugin);

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);

        QColor bg;
        if (hasPlugin_) {
            if (!bypassed_) {
                bg = hover_ ? QColor(0xd6, 0xf4, 0xe1) : QColor(0xc8, 0xeb, 0xd5);
            } else {
                bg = hover_ ? QColor(0x2f, 0x40, 0x37) : QColor(0x25, 0x33, 0x2b);
            }
        } else {
            bg = hover_ ? QColor(0x25, 0x28, 0x2c) : QColor(0x1c, 0x1f, 0x22);
        }
        if (dragHover_)
            bg = QColor(0x10, 0x44, 0x28);
        p.fillRect(rect(), bg);

        p.setPen(QColor(0x12, 0x14, 0x16));
        p.drawLine(0, height() - 1, width() - 1, height() - 1);

        if (dragHover_) {
            p.setPen(QColor(0x00, 0xe6, 0x76));
            p.drawRect(0, 0, width() - 1, height() - 1);
        }

        QFont f = font();
        f.setPointSize(8);
        if (hasPlugin_) {
            if (!bypassed_) {
                f.setWeight(QFont::Weight::DemiBold);
                p.setFont(f);
                p.setPen(QColor(0x0a, 0x22, 0x16));
            } else {
                f.setWeight(QFont::Weight::Normal);
                p.setFont(f);
                p.setPen(QColor(0x76, 0x98, 0x86));
            }
            const QFontMetrics fm(f);
            const QString elided = fm.elidedText(name_, Qt::ElideRight, width() - 6);
            p.drawText(QRect(3, 0, width() - 6, height() - 1), Qt::AlignLeft | Qt::AlignVCenter, elided);
        } else {
            p.setFont(f);
            p.setPen(QColor(0x3a, 0x40, 0x46));
            p.drawText(QRect(3, 0, width() - 6, height() - 1), Qt::AlignLeft | Qt::AlignVCenter,
                QStringLiteral("—"));
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
        } else if (event->button() == Qt::RightButton) {
            emit menuRequested(mapToGlobal(event->pos()));
        }
    }

    void enterEvent(QEnterEvent* /*event*/) override
    {
        hover_ = true;
        update();
    }

    void leaveEvent(QEvent* /*event*/) override
    {
        hover_ = false;
        update();
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (droppedPlugin(event->mimeData())) {
            event->acceptProposedAction();
            dragHover_ = true;
            update();
        }
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (droppedPlugin(event->mimeData()))
            event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent* /*event*/) override
    {
        dragHover_ = false;
        update();
    }

    void dropEvent(QDropEvent* event) override
    {
        dragHover_ = false;
        update();
        if (const auto plugin = droppedPlugin(event->mimeData())) {
            event->acceptProposedAction();
            emit pluginDropped(*plugin);
        }
    }

private:
    void updateTip()
    {
        if (!hasPlugin_) {
            setToolTip(tr("Slot %1: click to add effect or drop a plugin here").arg(slot_ + 1));
        } else if (bypassed_) {
            setToolTip(tr("%1 (Slot %2, bypassed)\nClick to open editor, right-click for menu")
                    .arg(name_)
                    .arg(slot_ + 1));
        } else {
            setToolTip(
                tr("%1 (Slot %2)\nClick to open editor, right-click for menu").arg(name_).arg(slot_ + 1));
        }
    }

    std::size_t slot_ = 0;
    bool hasPlugin_ = false;
    bool bypassed_ = false;
    bool hover_ = false;
    bool dragHover_ = false;
    QString name_;
};

} // namespace

/// One row in the slot rack combining the switch and the slot button.
class MixerSlotRow : public QWidget {
    Q_OBJECT

public:
    explicit MixerSlotRow(std::size_t slot, QWidget* parent = nullptr)
        : QWidget(parent)
        , slot_(slot)
    {
        setFixedHeight(MixerSlotRack::kSlotHeight);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        switchBtn_ = new FxSwitchButton(slot, this);
        layout->addWidget(switchBtn_);
        connect(switchBtn_, &FxSwitchButton::toggled, this, &MixerSlotRow::bypassToggled);
        connect(switchBtn_, &FxSwitchButton::menuRequested, this, &MixerSlotRow::menuRequested);

        slotBtn_ = new FxSlotButton(slot, this);
        layout->addWidget(slotBtn_);
        connect(slotBtn_, &FxSlotButton::clicked, this, &MixerSlotRow::slotClicked);
        connect(slotBtn_, &FxSlotButton::menuRequested, this, &MixerSlotRow::menuRequested);
        connect(slotBtn_, &FxSlotButton::pluginDropped, this, &MixerSlotRow::pluginDropped);
    }

    void setEffect(const QString& name, bool bypassed)
    {
        switchBtn_->setState(true, bypassed, name);
        slotBtn_->setState(true, bypassed, name);
    }

    void setEmpty()
    {
        switchBtn_->setState(false, false, QString());
        slotBtn_->setState(false, false, QString());
    }

signals:
    void bypassToggled(bool bypassed);
    void slotClicked();
    void menuRequested(const QPoint& globalPos);
    void pluginDropped(std::size_t plugin);

private:
    std::size_t slot_ = 0;
    FxSwitchButton* switchBtn_ = nullptr;
    FxSlotButton* slotBtn_ = nullptr;
};

MixerSlotRack::MixerSlotRack(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("fxRack"));
    setFixedWidth(60);
    setFixedHeight(static_cast<int>(MixerModel::kMaxEffects) * kSlotHeight + 2);

    clipHost_ = new QWidget(this);
    for (std::size_t i = 0; i < MixerModel::kMaxEffects; ++i) {
        auto* row = new MixerSlotRow(i, clipHost_);
        rows_[i] = row;

        connect(row, &MixerSlotRow::bypassToggled, this, [this, i](bool on) { emit bypassToggled(i, on); });
        connect(row, &MixerSlotRow::slotClicked, this, [this, i] { emit slotClicked(i); });
        connect(row, &MixerSlotRow::menuRequested, this,
            [this, i](const QPoint& at) { emit slotMenuRequested(i, at); });
        connect(
            row, &MixerSlotRow::pluginDropped, this, [this, i](std::size_t p) { emit pluginDropped(i, p); });
    }
    updateRowGeometry();
}

int MixerSlotRack::maxScrollSlot() const
{
    return std::max(0, static_cast<int>(MixerModel::kMaxEffects) - visibleSlots_);
}

void MixerSlotRack::setVisibleSlotCount(int count)
{
    const int clamped = std::clamp(count, kMinVisibleSlots, static_cast<int>(MixerModel::kMaxEffects));
    const int wantedHeight = clamped * kSlotHeight + 2;
    if (clamped == visibleSlots_ && height() == wantedHeight)
        return;
    visibleSlots_ = clamped;
    scrollSlot_ = std::clamp(scrollSlot_, 0, maxScrollSlot());
    setFixedHeight(wantedHeight);
    updateRowGeometry();
    update();
}

void MixerSlotRack::setScrollSlot(int slot)
{
    const int clamped = std::clamp(slot, 0, maxScrollSlot());
    if (clamped == scrollSlot_)
        return;
    scrollSlot_ = clamped;
    updateRowGeometry();
    update();
}

void MixerSlotRack::updateRowGeometry()
{
    if (clipHost_ == nullptr)
        return;
    const bool scrollable = maxScrollSlot() > 0;
    const int innerW = std::max(1, width() - 2 - (scrollable ? kScrollbarStripWidth : 0));
    const int innerH = std::max(1, height() - 2);
    clipHost_->setGeometry(1, 1, innerW, innerH);
    for (std::size_t i = 0; i < MixerModel::kMaxEffects; ++i) {
        if (rows_[i] != nullptr)
            rows_[i]->setGeometry(0, (static_cast<int>(i) - scrollSlot_) * kSlotHeight, innerW, kSlotHeight);
    }
}

void MixerSlotRack::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    updateRowGeometry();
}

void MixerSlotRack::wheelEvent(QWheelEvent* event)
{
    if (maxScrollSlot() <= 0) {
        QFrame::wheelEvent(event);
        return;
    }
    const int dy = event->angleDelta().y();
    if (dy != 0) {
        wheelDeltaRemainder_ += dy;
        constexpr int kStepDelta = 60; // responsive 1-slot step per half wheel notch or touchpad swipe
        const int steps = wheelDeltaRemainder_ / kStepDelta;
        if (steps != 0) {
            wheelDeltaRemainder_ %= kStepDelta;
            setScrollSlot(scrollSlot_ - steps);
        }
    }
    event->accept();
}

void MixerSlotRack::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
    if (maxScrollSlot() <= 0)
        return;

    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    const int trackX = width() - 1 - kScrollbarStripWidth;
    const int trackH = std::max(4, height() - 4);
    p.fillRect(QRect(trackX, 1, kScrollbarStripWidth, height() - 2), QColor(0x10, 0x13, 0x16));

    const auto total = static_cast<double>(MixerModel::kMaxEffects);
    const int thumbH = std::max(10, static_cast<int>(std::lround(trackH * (visibleSlots_ / total))));
    const int travel = std::max(0, trackH - thumbH);
    const int thumbY = 2
        + (maxScrollSlot() > 0
                ? static_cast<int>(std::lround(travel * (static_cast<double>(scrollSlot_) / maxScrollSlot())))
                : 0);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    QColor thumb = draggingScroll_ ? c.accent : c.textDim;
    if (!draggingScroll_)
        thumb.setAlpha(185);
    p.setBrush(thumb);
    p.drawRoundedRect(QRectF(trackX + 1.0, thumbY, kScrollbarStripWidth - 2.0, thumbH), 1.5, 1.5);
}

void MixerSlotRack::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && maxScrollSlot() > 0
        && event->position().x() >= width() - 1 - kScrollbarStripWidth) {
        draggingScroll_ = true;
        dragStartY_ = static_cast<int>(event->position().y());
        dragStartSlot_ = scrollSlot_;
        update();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void MixerSlotRack::mouseMoveEvent(QMouseEvent* event)
{
    if (draggingScroll_ && maxScrollSlot() > 0) {
        const int dy = static_cast<int>(event->position().y()) - dragStartY_;
        const int rangePx = std::max(12, height() - 16);
        const int deltaSlots
            = static_cast<int>(std::lround(static_cast<double>(dy) * maxScrollSlot() / rangePx));
        setScrollSlot(dragStartSlot_ + deltaSlots);
        event->accept();
        return;
    }
    QFrame::mouseMoveEvent(event);
}

void MixerSlotRack::mouseReleaseEvent(QMouseEvent* event)
{
    if (draggingScroll_) {
        draggingScroll_ = false;
        update();
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void MixerSlotRack::setEffects(const std::vector<PluginSlot>& effects)
{
    for (std::size_t i = 0; i < MixerModel::kMaxEffects; ++i) {
        if (i < effects.size())
            rows_[i]->setEffect(QString::fromStdString(effects[i].name), effects[i].bypassed);
        else
            rows_[i]->setEmpty();
    }
}

} // namespace vloomdaw

#include "MixerSlotRack.moc"
