// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerSlotRack.hpp"

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
#include <QVBoxLayout>

namespace vloomdaw {

namespace {

constexpr int kSlotHeight = 18;
constexpr int kSwitchWidth = 15;

/// Left switch button of an FX slot: shows a green triangle when active,
/// muted dim triangle when bypassed, and a small dark square when empty.
class FxSwitchButton : public QWidget {
    Q_OBJECT

public:
    explicit FxSwitchButton(std::size_t slot, QWidget* parent = nullptr)
        : QWidget(parent)
        , slot_(slot)
    {
        setFixedSize(kSwitchWidth, kSlotHeight);
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
        // Background
        const QColor bg = hover_ ? QColor(0x1e, 0x22, 0x26) : QColor(0x14, 0x17, 0x1a);
        p.fillRect(rect(), bg);

        // Right separator border
        p.setPen(QColor(0x10, 0x12, 0x14));
        p.drawLine(width() - 1, 0, width() - 1, height() - 1);

        // Bottom separator border
        p.setPen(QColor(0x12, 0x14, 0x16));
        p.drawLine(0, height() - 1, width() - 1, height() - 1);

        // Icon
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        if (hasPlugin_) {
            if (!bypassed_) {
                p.setBrush(QColor(0x00, 0xe6, 0x76)); // Vibrant mint green ▶
            } else {
                p.setBrush(QColor(0x55, 0x62, 0x5b)); // Dim grey-green ▶
            }
            QPolygonF triangle;
            triangle << QPointF(4.0, 4.5) << QPointF(4.0, 13.5) << QPointF(11.0, 9.0);
            p.drawPolygon(triangle);
        } else {
            p.setBrush(QColor(0x32, 0x36, 0x3a)); // Empty slot square ■
            p.drawRect(QRectF(5.5, 7.0, 4.0, 4.0));
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
            setToolTip(tr("Slot %1").arg(slot_ + 1));
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
        setFixedHeight(kSlotHeight);
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

        // Background
        QColor bg;
        if (hasPlugin_) {
            if (!bypassed_) {
                bg = hover_ ? QColor(0xd6, 0xf4, 0xe1) : QColor(0xc8, 0xeb, 0xd5); // light mint
            } else {
                bg = hover_ ? QColor(0x2f, 0x40, 0x37) : QColor(0x25, 0x33, 0x2b); // dark muted green
            }
        } else {
            bg = hover_ ? QColor(0x25, 0x28, 0x2c) : QColor(0x1c, 0x1f, 0x22); // dark charcoal
        }
        if (dragHover_)
            bg = QColor(0x10, 0x44, 0x28);
        p.fillRect(rect(), bg);

        // Bottom separator border
        p.setPen(QColor(0x12, 0x14, 0x16));
        p.drawLine(0, height() - 1, width() - 1, height() - 1);

        if (dragHover_) {
            p.setPen(QColor(0x00, 0xe6, 0x76));
            p.drawRect(0, 0, width() - 1, height() - 1);
        }

        // Text
        QFont f = font();
        f.setPointSize(8);
        if (hasPlugin_) {
            if (!bypassed_) {
                f.setWeight(QFont::Weight::DemiBold);
                p.setFont(f);
                p.setPen(QColor(0x0a, 0x22, 0x16)); // dark text on mint
            } else {
                f.setWeight(QFont::Weight::Normal);
                p.setFont(f);
                p.setPen(QColor(0x76, 0x98, 0x86)); // muted green-grey text
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
        setFixedHeight(kSlotHeight);

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
    setFixedHeight(kSlotHeight);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    for (std::size_t i = 0; i < MixerModel::kMaxEffects; ++i) {
        auto* row = new MixerSlotRow(i, this);
        rows_[i] = row;
        row->setVisible(i == 0); // Initially only 1 empty slot visible
        layout->addWidget(row);

        connect(row, &MixerSlotRow::bypassToggled, this, [this, i](bool on) { emit bypassToggled(i, on); });
        connect(row, &MixerSlotRow::slotClicked, this, [this, i] { emit slotClicked(i); });
        connect(row, &MixerSlotRow::menuRequested, this,
            [this, i](const QPoint& at) { emit slotMenuRequested(i, at); });
        connect(
            row, &MixerSlotRow::pluginDropped, this, [this, i](std::size_t p) { emit pluginDropped(i, p); });
    }
}

void MixerSlotRack::setEffects(const std::vector<PluginSlot>& effects)
{
    // Initially 1 empty slot. If N effects loaded, show N effects + 1 trailing empty slot (up to kMaxEffects).
    const std::size_t visibleCount = std::clamp<std::size_t>(effects.size() + 1, 1, MixerModel::kMaxEffects);

    for (std::size_t i = 0; i < MixerModel::kMaxEffects; ++i) {
        if (i < effects.size())
            rows_[i]->setEffect(QString::fromStdString(effects[i].name), effects[i].bypassed);
        else
            rows_[i]->setEmpty();

        rows_[i]->setVisible(i < visibleCount);
    }
    setFixedHeight(static_cast<int>(visibleCount * kSlotHeight));
}

} // namespace vloomdaw

#include "MixerSlotRack.moc"
