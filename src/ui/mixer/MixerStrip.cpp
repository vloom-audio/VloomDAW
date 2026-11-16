// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/mixer/MixerStrip.hpp"

#include "core/Decibels.hpp"
#include "ui/Theme.hpp"
#include "ui/mixer/MixerSlotRack.hpp"
#include "ui/plugins/PluginDrag.hpp"
#include "ui/widgets/ColorMenu/ColorMenu.hpp"
#include "ui/widgets/Fader/Fader.hpp"
#include "ui/widgets/LevelMeter/LevelMeter.hpp"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

namespace vloomdaw {

namespace {

constexpr int kInsertWidth = 82;
constexpr int kInsertWideWidth = 108;
constexpr int kMasterWidth = 88;
constexpr int kMasterWideWidth = 114;
constexpr int kMinStripHeight = 276;
constexpr int kMinFaderHeight = 52;
constexpr int kSendRowHeight = 19;

QString formatStudioOnePan(double pan)
{
    const int pct = static_cast<int>(std::lround(std::abs(pan) * 100.0));
    if (pct == 0)
        return QStringLiteral("<C>");
    return pan < 0.0 ? QStringLiteral("<L%1").arg(pct) : QStringLiteral("R%1>").arg(pct);
}

} // namespace

/// Studio One-style horizontal bipolar Pan slider + `<C>` readout (enlarged for 1080p+ clarity).
class StripPanBar : public QWidget {
    Q_OBJECT

public:
    explicit StripPanBar(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(30);
        setCursor(Qt::SizeHorCursor);
        setFocusPolicy(Qt::NoFocus);
        updateTip();
    }

    [[nodiscard]] double value() const { return pan_; }

    void setValue(double pan)
    {
        const double clamped = std::clamp(pan, -1.0, 1.0);
        if (std::abs(clamped - pan_) < 1e-6)
            return;
        pan_ = clamped;
        updateTip();
        update();
    }

signals:
    void valueChanged(double pan);

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF barRect(1.0, 1.0, std::max(12.0, width() - 2.0), 15.0);
        p.setPen(QPen(dragging_ ? QColor(0x38, 0xbd, 0xf8) : c.border, 1.0));
        p.setBrush(c.display);
        p.drawRoundedRect(barRect, 3.0, 3.0);

        const double midX = barRect.center().x();
        // Subtle center notch & quarter ticks
        QColor notchCol = c.border;
        notchCol.setAlpha(190);
        p.fillRect(QRectF(midX - 0.5, barRect.top() + 2.0, 1.0, barRect.height() - 4.0), notchCol);

        const double thumbX
            = barRect.left() + 3.5 + (pan_ * 0.5 + 0.5) * std::max(1.0, barRect.width() - 7.0);

        if (std::abs(pan_) > 0.01) {
            QColor fill(0x38, 0xbd, 0xf8);
            fill.setAlpha(170);
            p.fillRect(QRectF(std::min(midX, thumbX), barRect.top() + 2.0, std::abs(thumbX - midX),
                           barRect.height() - 4.0),
                fill);
        }
        // Prominent Studio One pan thumb handle.
        p.setPen(Qt::NoPen);
        p.setBrush(std::abs(pan_) > 0.01 || dragging_ ? QColor(0x7d, 0xd3, 0xfc) : c.text);
        p.drawRoundedRect(QRectF(thumbX - 1.75, barRect.top() + 1.5, 3.5, barRect.height() - 3.0), 1.4, 1.4);

        QFont f = font();
        f.setPointSizeF(8.4);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(std::abs(pan_) > 0.01 || dragging_ ? c.text : c.textDim);
        const QRect textRect(0, 16, width(), 14);
        p.drawText(textRect, Qt::AlignCenter, formatStudioOnePan(pan_));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        dragging_ = true;
        lastX_ = event->position().x();
        if (!event->modifiers().testFlag(Qt::ShiftModifier)) {
            const double norm
                = std::clamp((event->position().x() - 3.0) / std::max(1, width() - 6), 0.0, 1.0);
            const double target = norm * 2.0 - 1.0;
            if (std::abs(target - pan_) > 0.18)
                applyUserPan(target, event->globalPosition().toPoint());
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_)
            return;
        const double dx = event->position().x() - lastX_;
        lastX_ = event->position().x();
        const double scale = event->modifiers().testFlag(Qt::ShiftModifier) ? 0.2 : 1.0;
        const double next = pan_ + (dx * scale * 2.0) / std::max(24, width() - 6);
        applyUserPan(next, event->globalPosition().toPoint());
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        dragging_ = false;
        QToolTip::hideText();
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            applyUserPan(0.0, event->globalPosition().toPoint());
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const int steps
            = (event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x()) / 120;
        if (steps == 0) {
            event->ignore();
            return;
        }
        const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 0.01 : 0.05;
        applyUserPan(pan_ + steps * step, event->globalPosition().toPoint());
        event->accept();
    }

private:
    void applyUserPan(double next, const QPoint& globalPos)
    {
        const double clamped = std::clamp(next, -1.0, 1.0);
        if (std::abs(clamped - pan_) < 1e-6)
            return;
        pan_ = clamped;
        updateTip();
        update();
        emit valueChanged(pan_);
        QToolTip::showText(globalPos, formatStudioOnePan(pan_), this);
    }

    void updateTip()
    {
        setToolTip(
            tr("Pan: %1\nDrag or scroll (Shift: fine), double-click for <C>").arg(formatStudioOnePan(pan_)));
    }

    double pan_ = 0.0;
    bool dragging_ = false;
    double lastX_ = 0.0;
};

/// Studio One-style compact Sends rack showing active post/pre-fader sends with
/// power toggle, target name, Pre toggle, and horizontal blue level slider.
class MixerSendRack : public QFrame {
    Q_OBJECT

public:
    explicit MixerSendRack(QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(QStringLiteral("fxRack"));
        setMouseTracking(true);
        syncHeight();
    }

    void setSends(const std::vector<MixerSendDisplay>& sends)
    {
        sends_ = sends;
        scrollOffset_ = std::clamp(scrollOffset_, 0, maxScroll());
        syncHeight();
        update();
    }

    void setVisibleRowBudget(int rows, bool allowEmptyPlaceholder)
    {
        const int clamped = std::clamp(rows, 0, 4);
        if (clamped == visibleBudget_ && allowEmptyPlaceholder == showEmptyBox_)
            return;
        visibleBudget_ = clamped;
        showEmptyBox_ = allowEmptyPlaceholder;
        scrollOffset_ = std::clamp(scrollOffset_, 0, maxScroll());
        syncHeight();
        update();
    }

signals:
    void addSendRequested(const QPoint& globalPos);
    void sendLevelChanged(std::size_t slot, double level);
    void sendMuteToggled(std::size_t slot, bool muted);
    void sendPreFaderToggled(std::size_t slot, bool preFader);
    void sendMenuRequested(std::size_t slot, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QFrame::paintEvent(event);
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (sends_.empty()) {
            p.fillRect(rect().adjusted(1, 1, -1, -1), QColor(0x1b, 0x1e, 0x22));
            QFont f = font();
            f.setPointSizeF(7.2);
            p.setFont(f);
            p.setPen(c.textDim);
            p.drawText(rect().adjusted(4, 0, -4, 0), Qt::AlignCenter, tr("+ Add Send"));
            return;
        }

        const int rowsShown = shownRowCount();
        QFont f = font();
        f.setPointSizeF(7.4);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        const QFontMetrics fm(f);

        for (int vis = 0; vis < rowsShown; ++vis) {
            const int idx = scrollOffset_ + vis;
            if (idx < 0 || static_cast<std::size_t>(idx) >= sends_.size())
                break;
            const MixerSendDisplay& s = sends_[static_cast<std::size_t>(idx)];
            const QRect rowRect(1, 1 + vis * kSendRowHeight, width() - 2, kSendRowHeight);
            p.fillRect(rowRect, QColor(0x1c, 0x20, 0x25));
            p.setPen(QColor(0x11, 0x13, 0x16));
            p.drawLine(rowRect.left(), rowRect.bottom(), rowRect.right(), rowRect.bottom());

            // Power indicator dot (left 13 px)
            const QColor pwrCol = s.muted ? QColor(0x52, 0x58, 0x62) : QColor(0x38, 0xbd, 0xf8);
            p.setPen(QPen(pwrCol, 1.3));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(rowRect.left() + 6.5, rowRect.top() + 6.5), 3.2, 3.2);

            // Pre-fader badge on right (13 px)
            const QRect preRect(rowRect.right() - 13, rowRect.top() + 1, 12, 10);
            p.setPen(s.preFader ? QColor(0xf5, 0x9e, 0x0b) : c.textDim);
            QFont pf = f;
            pf.setPointSizeF(6.5);
            p.setFont(pf);
            p.drawText(preRect, Qt::AlignCenter, QStringLiteral("P"));

            // Target name
            p.setFont(f);
            p.setPen(s.muted ? c.textDim : c.text);
            const QRect nameRect(rowRect.left() + 13, rowRect.top(), std::max(8, rowRect.width() - 28), 11);
            p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                fm.elidedText(s.targetName, Qt::ElideRight, nameRect.width()));

            // Studio One blue horizontal send level bar along the bottom 4 px
            const QRectF barBg(
                rowRect.left() + 3.0, rowRect.bottom() - 4.5, std::max(6.0, rowRect.width() - 6.0), 3.2);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x0f, 0x12, 0x15));
            p.drawRoundedRect(barBg, 1.2, 1.2);

            const double fillW = std::clamp(s.level, 0.0, 1.0) * barBg.width();
            if (fillW >= 1.0) {
                p.setBrush(s.muted ? QColor(0x4b, 0x55, 0x63) : QColor(0x38, 0xbd, 0xf8));
                p.drawRoundedRect(QRectF(barBg.left(), barBg.top(), fillW, barBg.height()), 1.2, 1.2);
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (sends_.empty()) {
            if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
                emit addSendRequested(event->globalPosition().toPoint());
            return;
        }
        const int idx = slotAtY(static_cast<int>(event->position().y()));
        if (idx < 0 || static_cast<std::size_t>(idx) >= sends_.size())
            return;
        const auto slot = static_cast<std::size_t>(idx);
        if (event->button() == Qt::RightButton) {
            emit sendMenuRequested(slot, event->globalPosition().toPoint());
            return;
        }
        if (event->button() == Qt::LeftButton) {
            if (event->position().x() <= 14.0
                && (static_cast<int>(event->position().y()) - 1) % kSendRowHeight < 12) {
                emit sendMuteToggled(slot, !sends_[slot].muted);
                return;
            }
            if (event->position().x() >= width() - 15.0
                && (static_cast<int>(event->position().y()) - 1) % kSendRowHeight < 12) {
                emit sendPreFaderToggled(slot, !sends_[slot].preFader);
                return;
            }
            draggingSlot_ = idx;
            lastX_ = event->position().x();
            dragLevel_ = sends_[slot].level;
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (draggingSlot_ < 0 || static_cast<std::size_t>(draggingSlot_) >= sends_.size())
            return;
        const auto slot = static_cast<std::size_t>(draggingSlot_);
        const double dx = event->position().x() - lastX_;
        lastX_ = event->position().x();
        const double scale = event->modifiers().testFlag(Qt::ShiftModifier) ? 0.2 : 1.0;
        dragLevel_ = std::clamp(dragLevel_ + (dx * scale) / std::max(24, width() - 8), 0.0, 1.0);
        sends_[slot].level = dragLevel_;
        update();
        emit sendLevelChanged(slot, dragLevel_);
        QToolTip::showText(event->globalPosition().toPoint(),
            tr("%1: %2").arg(
                sends_[slot].targetName, QString::fromStdString(formatDb(faderToGain(dragLevel_)))),
            this);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        draggingSlot_ = -1;
        QToolTip::hideText();
        QFrame::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (sends_.empty()) {
            emit addSendRequested(event->globalPosition().toPoint());
            return;
        }
        const int idx = slotAtY(static_cast<int>(event->position().y()));
        if (idx >= 0 && static_cast<std::size_t>(idx) < sends_.size() && event->button() == Qt::LeftButton) {
            const auto slot = static_cast<std::size_t>(idx);
            sends_[slot].level = kUnityFaderPosition;
            update();
            emit sendLevelChanged(slot, kUnityFaderPosition);
        }
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (maxScroll() <= 0) {
            QFrame::wheelEvent(event);
            return;
        }
        const int steps = event->angleDelta().y() / 60;
        if (steps != 0) {
            scrollOffset_ = std::clamp(scrollOffset_ - steps, 0, maxScroll());
            update();
        }
        event->accept();
    }

private:
    [[nodiscard]] int shownRowCount() const
    {
        if (sends_.empty())
            return 0;
        return std::min(std::max(1, visibleBudget_), static_cast<int>(sends_.size()));
    }

    [[nodiscard]] int maxScroll() const
    {
        return std::max(0, static_cast<int>(sends_.size()) - shownRowCount());
    }

    [[nodiscard]] int slotAtY(int y) const
    {
        if (y < 1)
            return -1;
        return scrollOffset_ + (y - 1) / kSendRowHeight;
    }

    void syncHeight()
    {
        if (sends_.empty()) {
            setVisible(showEmptyBox_ && visibleBudget_ > 0);
            setFixedHeight(18);
            return;
        }
        setVisible(visibleBudget_ > 0);
        setFixedHeight(shownRowCount() * kSendRowHeight + 2);
    }

    std::vector<MixerSendDisplay> sends_;
    int visibleBudget_ = 1;
    bool showEmptyBox_ = true;
    int scrollOffset_ = 0;
    int draggingSlot_ = -1;
    double lastX_ = 0.0;
    double dragLevel_ = 0.8;
};

/// Studio One-style bottom-right channel type glyph (`Instrument` piano keys,
/// `Audio` waveform, `FX`, `BUS`, `AUX`, or `OUT`).
class StripTypeBadge : public QWidget {
    Q_OBJECT

public:
    enum class Mode { Master, Audio, Instrument, Bus, Fx, Aux };

    explicit StripTypeBadge(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(28, 14);
    }

    void setMode(Mode mode)
    {
        if (mode_ == mode)
            return;
        mode_ = mode;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (mode_ == Mode::Instrument) {
            // Orange Studio One piano keyboard badge (Screenshot 3).
            const QColor col(0xf9, 0x73, 0x16);
            const QRectF kb(6.0, 2.0, 18.0, 10.0);
            p.setPen(QPen(col, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(kb, 1.4, 1.4);
            p.fillRect(QRectF(kb.left() + 3.2, kb.top(), 2.2, 6.0), col);
            p.fillRect(QRectF(kb.left() + 8.0, kb.top(), 2.2, 6.0), col);
            p.fillRect(QRectF(kb.left() + 12.6, kb.top(), 2.2, 6.0), col);
            return;
        }
        if (mode_ == Mode::Audio) {
            // Crisp Studio One waveform badge (Screenshot 4).
            p.setPen(QPen(c.textDim.lighter(125), 1.4));
            const double cy = height() * 0.5;
            QPolygonF wave;
            wave << QPointF(5.0, cy) << QPointF(8.5, cy - 4.5) << QPointF(12.0, cy + 4.0)
                 << QPointF(15.5, cy - 5.5) << QPointF(19.0, cy + 4.5) << QPointF(23.0, cy);
            p.drawPolyline(wave);
            return;
        }

        QFont f = font();
        f.setPointSizeF(7.2);
        f.setBold(true);
        p.setFont(f);
        QString txt = QStringLiteral("OUT");
        QColor col = c.textDim;
        switch (mode_) {
        case Mode::Fx:
            txt = QStringLiteral("FX");
            col = QColor(0xa7, 0x8b, 0xfa);
            break;
        case Mode::Bus:
            txt = QStringLiteral("BUS");
            col = QColor(0x38, 0xbd, 0xf8);
            break;
        case Mode::Aux:
            txt = QStringLiteral("AUX");
            col = QColor(0x2d, 0xd4, 0xbf);
            break;
        case Mode::Master:
        case Mode::Audio:
        case Mode::Instrument:
            break;
        }
        p.setPen(col);
        p.drawText(rect(), Qt::AlignRight | Qt::AlignVCenter, txt);
    }

private:
    Mode mode_ = Mode::Audio;
};

/// Studio One-style Mono / Stereo channel mode toggle button (`OO Stereo` vs `O Mono`).
class StripStereoButton : public QWidget {
    Q_OBJECT

public:
    explicit StripStereoButton(bool isMaster, QWidget* parent = nullptr)
        : QWidget(parent)
        , isMaster_(isMaster)
    {
        setFixedHeight(20);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        updateTip();
    }

    [[nodiscard]] bool isMono() const { return mono_; }

    void setMono(bool mono)
    {
        if (mono_ == mono)
            return;
        mono_ = mono;
        updateTip();
        update();
    }

signals:
    void toggled(bool mono);

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor bg = mono_ ? QColor(0x28, 0x22, 0x16) : (hover_ ? c.raised : c.window);
        QColor border = mono_ ? QColor(0xf5, 0x9e, 0x0b) : (hover_ ? c.textDim : c.border);
        p.setPen(QPen(border, 1.0));
        p.setBrush(bg);
        p.drawRoundedRect(box, 3.0, 3.0);

        const QColor fg = mono_ ? QColor(0xfb, 0xbf, 0x24) : (hover_ ? c.text : c.textDim);
        QFont f = font();
        f.setPointSizeF(7.8);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);

        const QString label = mono_ ? tr("Mono") : tr("Stereo");
        const QFontMetrics fm(f);
        const int textW = fm.horizontalAdvance(label);
        const double iconW = mono_ ? 10.0 : 15.0;
        const double totalW = iconW + 5.0 + textW;
        const double startX = std::max(5.0, (width() - totalW) * 0.5);
        const double cy = height() * 0.5;

        p.setPen(QPen(fg, 1.35));
        p.setBrush(Qt::NoBrush);
        if (mono_) {
            // Single centered circle: Mono (L+R summed)
            p.drawEllipse(QPointF(startX + 5.0, cy), 4.0, 4.0);
        } else {
            // Interlocking dual circles: Stereo (L / R)
            p.drawEllipse(QPointF(startX + 4.5, cy), 3.8, 3.8);
            p.drawEllipse(QPointF(startX + 10.0, cy), 3.8, 3.8);
        }

        p.setPen(fg);
        const QRectF textRect(
            startX + iconW + 5.0, 0.0, std::max(10.0, width() - (startX + iconW + 7.0)), height());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            mono_ = !mono_;
            updateTip();
            update();
            emit toggled(mono_);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
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
        if (isMaster_) {
            setToolTip(mono_
                    ? tr("Master Bus Mode: Mono (L+R summed for mono compatibility check)\nClick to return "
                         "to Stereo")
                    : tr("Master Bus Mode: Stereo (L+R)\nClick to check mix in Mono"));
        } else {
            setToolTip(mono_ ? tr("Channel Mode: Mono (stereo audio summed L+R)\nClick to switch to Stereo")
                             : tr("Channel Mode: Stereo (L+R)\nClick to fold channel audio to Mono"));
        }
    }

    bool isMaster_ = false;
    bool mono_ = false;
    bool hover_ = false;
};

/// Studio One-style horizontal divider handle directly below the Inserts FX rack:
/// dragging up shrinks the FX slots to stretch the faders taller; dragging down
/// reveals up to all 9 FX slots (double-click toggles compact vs. full).
class StripRackHandle : public QWidget {
    Q_OBJECT

public:
    explicit StripRackHandle(std::function<int()> currentSlots, QWidget* parent = nullptr)
        : QWidget(parent)
        , currentSlots_(std::move(currentSlots))
    {
        setFixedHeight(7);
        setCursor(Qt::SplitVCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Drag up/down to resize Inserts FX slots and stretch faders\n"
                      "Double-click to toggle 4 / 9 FX slots"));
    }

signals:
    void rowsDragged(int rows, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const double cy = height() * 0.5;
        if (hover_ || dragging_) {
            QColor lineCol = c.accent;
            lineCol.setAlpha(dragging_ ? 210 : 135);
            p.setPen(QPen(lineCol, 1.0));
            p.drawLine(QPointF(2.0, cy), QPointF(width() - 2.0, cy));
        }

        const double pillW = std::min<double>(22.0, std::max(10.0, width() * 0.28));
        const QRectF grip((width() - pillW) * 0.5, cy - 1.3, pillW, 2.6);
        QColor pillCol = (hover_ || dragging_) ? c.accent : c.textDim;
        if (!hover_ && !dragging_)
            pillCol.setAlpha(115);
        p.setPen(Qt::NoPen);
        p.setBrush(pillCol);
        p.drawRoundedRect(grip, 1.3, 1.3);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragging_ = true;
            dragStartGlobalY_ = event->globalPosition().y();
            dragStartSlots_ = currentSlots_ ? currentSlots_() : 4;
            lastEmittedSlots_ = dragStartSlots_;
            update();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_)
            return;
        const double dy = event->globalPosition().y() - dragStartGlobalY_;
        const int deltaSlots = static_cast<int>(std::lround(dy / MixerSlotRack::kSlotHeight));
        const int target
            = std::clamp(dragStartSlots_ + deltaSlots, 0, static_cast<int>(MixerModel::kMaxEffects));
        if (target != lastEmittedSlots_) {
            lastEmittedSlots_ = target;
            emit rowsDragged(target, event->globalPosition().toPoint());
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (dragging_) {
            dragging_ = false;
            QToolTip::hideText();
            update();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragging_ = false;
            const int cur = currentSlots_ ? currentSlots_() : 4;
            const int next = (cur == 4) ? static_cast<int>(MixerModel::kMaxEffects) : 4;
            emit rowsDragged(next, event->globalPosition().toPoint());
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
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
    std::function<int()> currentSlots_;
    bool hover_ = false;
    bool dragging_ = false;
    double dragStartGlobalY_ = 0.0;
    int dragStartSlots_ = 4;
    int lastEmittedSlots_ = 4;
};

MixerStrip::MixerStrip(Kind kind, const QString& name, const QString& number, QWidget* parent)
    : QFrame(parent)
    , kind_(kind)
{
    setObjectName(QStringLiteral("mixerStrip"));
    setProperty("master", kind == Kind::Master);
    const int initialW = kind == Kind::Master ? kMasterWidth : kInsertWidth;
    setFixedWidth(initialW);
    setMinimumHeight(kMinStripHeight);
    setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 6, 4, 4);
    layout->setSpacing(2);

    const auto makeSectionHdr = [this](const QString& title, const QString& tip, auto onAddClicked) {
        auto* box = new QWidget(this);
        box->setFixedHeight(16);
        auto* row = new QHBoxLayout(box);
        row->setContentsMargins(2, 0, 1, 0);
        row->setSpacing(2);
        auto* lbl = new QLabel(title, box);
        lbl->setObjectName(QStringLiteral("stripRoute"));
        row->addWidget(lbl, 1);
        auto* plusBtn = new QToolButton(box);
        plusBtn->setObjectName(QStringLiteral("mixerMiniAddBtn"));
        plusBtn->setText(QStringLiteral("+"));
        plusBtn->setFixedSize(15, 15);
        plusBtn->setFocusPolicy(Qt::NoFocus);
        plusBtn->setToolTip(tip);
        connect(plusBtn, &QToolButton::clicked, this, [this, plusBtn, onAddClicked] {
            emit clicked();
            onAddClicked(plusBtn->mapToGlobal(QPoint(0, plusBtn->height())));
        });
        row->addWidget(plusBtn);
        return box;
    };

    // 1. Inserts header + FX rack
    insertsHdr_ = makeSectionHdr(tr("Inserts"), tr("Add Effect Plug-in…"),
        [this](const QPoint& globalPos) { emit effectsMenuRequested(globalPos); });
    layout->addWidget(insertsHdr_);

    fxRack_ = new MixerSlotRack(this);
    fxRack_->setFixedWidth(initialW - 8);
    fxRack_->installEventFilter(this);
    connect(fxRack_, &MixerSlotRack::bypassToggled, this, [this](std::size_t slot, bool bypassed) {
        emit clicked();
        emit effectBypassToggled(slot, bypassed);
    });
    connect(fxRack_, &MixerSlotRack::slotClicked, this, [this](std::size_t slot) {
        emit clicked();
        emit effectClicked(slot);
    });
    connect(fxRack_, &MixerSlotRack::slotMenuRequested, this, [this](std::size_t slot, const QPoint& at) {
        emit clicked();
        emit effectMenuRequested(slot, at);
    });
    connect(fxRack_, &MixerSlotRack::pluginDropped, this, &MixerStrip::pluginDroppedOnSlot);
    layout->addWidget(fxRack_, 0, Qt::AlignHCenter);

    fxResizeHandle_ = new StripRackHandle([this] { return shownFxSlots_; }, this);
    connect(fxResizeHandle_, &StripRackHandle::rowsDragged, this, [this](int rows, const QPoint& globalPos) {
        setUserFxSlotRows(rows);
        emit userFxSlotRowsChanged(userFxSlotRows_);
        const QString msg = userFxSlotRows_ == 0
            ? tr("Inserts: Collapsed (faders maximized)")
            : tr("Inserts: %1 / %2 slots").arg(userFxSlotRows_).arg(MixerModel::kMaxEffects);
        QToolTip::showText(globalPos, msg, fxResizeHandle_);
    });
    layout->addWidget(fxResizeHandle_);

    // 2. Sends header + Send rack (Insert / Bus / FX / Aux strips)
    if (kind == Kind::Insert) {
        sendsHdr_ = makeSectionHdr(tr("Sends"), tr("Add Send to FX / Bus / Aux…"),
            [this](const QPoint& globalPos) { emit addSendMenuRequested(globalPos); });
        layout->addWidget(sendsHdr_);

        sendRack_ = new MixerSendRack(this);
        sendRack_->setFixedWidth(initialW - 8);
        sendRack_->installEventFilter(this);
        connect(sendRack_, &MixerSendRack::addSendRequested, this, &MixerStrip::addSendMenuRequested);
        connect(sendRack_, &MixerSendRack::sendLevelChanged, this, &MixerStrip::sendLevelChanged);
        connect(sendRack_, &MixerSendRack::sendMuteToggled, this, &MixerStrip::sendMuteToggled);
        connect(sendRack_, &MixerSendRack::sendPreFaderToggled, this, &MixerStrip::sendPreFaderToggled);
        connect(sendRack_, &MixerSendRack::sendMenuRequested, this, &MixerStrip::sendMenuRequested);
        layout->addWidget(sendRack_, 0, Qt::AlignHCenter);
    }

    // 3. Studio One Source / Instrument Pill + Output Routing Pill
    sourceBtn_ = new QPushButton(this);
    sourceBtn_->setObjectName(QStringLiteral("mixerSourcePill"));
    sourceBtn_->setFixedHeight(20);
    sourceBtn_->setFocusPolicy(Qt::NoFocus);
    sourceBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    sourceBtn_->installEventFilter(this);
    connect(sourceBtn_, &QPushButton::clicked, this, [this] {
        emit clicked();
        if (kind_ == Kind::Master)
            return;
        if (hasInstrumentSource_)
            emit instrumentClicked();
        else
            emit sourceClicked(sourceBtn_->mapToGlobal(QPoint(0, sourceBtn_->height())));
    });
    connect(sourceBtn_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        emit clicked();
        if (kind_ == Kind::Master)
            return;
        const QPoint global = sourceBtn_->mapToGlobal(pos);
        if (hasInstrumentSource_)
            emit instrumentMenuRequested(global);
        else
            emit sourceClicked(global);
    });
    layout->addWidget(sourceBtn_);

    outputBtn_ = new QPushButton(this);
    outputBtn_->setObjectName(QStringLiteral("mixerOutputPill"));
    outputBtn_->setFixedHeight(20);
    outputBtn_->setFocusPolicy(Qt::NoFocus);
    outputBtn_->setContextMenuPolicy(Qt::CustomContextMenu);
    outputBtn_->installEventFilter(this);
    connect(outputBtn_, &QPushButton::clicked, this, [this] {
        emit clicked();
        if (kind_ == Kind::Insert)
            emit outputRoutingRequested(outputBtn_->mapToGlobal(QPoint(0, outputBtn_->height())));
    });
    connect(outputBtn_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        emit clicked();
        if (kind_ == Kind::Insert)
            emit outputRoutingRequested(outputBtn_->mapToGlobal(pos));
    });
    layout->addWidget(outputBtn_);

    // 4. Studio One Bipolar Pan Bar (<C>)
    panBar_ = new StripPanBar(this);
    panBar_->installEventFilter(this);
    connect(panBar_, &StripPanBar::valueChanged, this, &MixerStrip::panChanged);
    layout->addWidget(panBar_);

    // 5. [M] & [S] Buttons + Studio One Mono / Stereo Toggle
    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(3);
    const auto addToggle = [&](const QString& text, const QString& objectName, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setObjectName(objectName);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setFixedHeight(20);
        b->installEventFilter(this);
        buttons->addWidget(b);
        return b;
    };
    mute_ = addToggle(QStringLiteral("M"), QStringLiteral("muteButton"), QString());
    connect(mute_, &QToolButton::toggled, this, &MixerStrip::muteToggled);
    if (kind == Kind::Insert) {
        solo_ = addToggle(QStringLiteral("S"), QStringLiteral("soloButton"), QString());
        connect(solo_, &QToolButton::toggled, this, &MixerStrip::soloToggled);
    }
    layout->addLayout(buttons);

    monoBtn_ = new StripStereoButton(kind == Kind::Master, this);
    monoBtn_->installEventFilter(this);
    connect(monoBtn_, &StripStereoButton::toggled, this, [this](bool mono) {
        mono_ = mono;
        refreshRoutingPills();
        emit monoToggled(mono);
    });
    layout->addWidget(monoBtn_);

    // 6. Dedicated dB Readout ABOVE the Meter + Fader (never overlaps the fader!)
    db_ = new QLabel(this);
    db_->setObjectName(QStringLiteral("stripDb"));
    db_->setAlignment(Qt::AlignCenter);
    db_->setFixedHeight(15);
    layout->addWidget(db_);

    // 7. Side-by-side Stereo LevelMeter + Studio One Fader
    auto* levels = new QHBoxLayout;
    levels->setContentsMargins(2, 0, 2, 0);
    levels->setSpacing(5);
    meter_ = new LevelMeter(this);
    meter_->setMinimumHeight(kMinFaderHeight);
    meter_->installEventFilter(this);
    fader_ = new Fader(this);
    fader_->setMinimumHeight(kMinFaderHeight);
    fader_->installEventFilter(this);
    connect(fader_, &Fader::valueChanged, this, [this](double position) {
        showDb(position);
        emit faderChanged(position);
    });
    levels->addStretch(1);
    levels->addWidget(meter_);
    levels->addWidget(fader_);
    levels->addStretch(1);
    layout->addLayout(levels, 1);
    showDb(fader_->value());

    // 8. Channel Number + Studio One Type Badge (Piano / Waveform / FX / BUS / AUX)
    auto* bottomMeta = new QHBoxLayout;
    bottomMeta->setContentsMargins(2, 0, 2, 0);
    bottomMeta->setSpacing(2);
    number_ = new QLabel(this);
    number_->setObjectName(QStringLiteral("stripNumber"));
    number_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    number_->setFixedHeight(15);
    bottomMeta->addWidget(number_, 1);

    badge_ = new StripTypeBadge(this);
    bottomMeta->addWidget(badge_);
    layout->addLayout(bottomMeta);

    // 9. Studio One Full-Colour Bottom Name Plate (with inline rename on double-click)
    namePlate_ = new QWidget(this);
    namePlate_->setObjectName(QStringLiteral("mixerNamePlate"));
    namePlate_->setFixedHeight(21);
    namePlate_->installEventFilter(this);
    auto* plateLayout = new QHBoxLayout(namePlate_);
    plateLayout->setContentsMargins(2, 1, 2, 1);
    plateLayout->setSpacing(0);

    name_ = new QLabel(namePlate_);
    name_->setObjectName(QStringLiteral("stripName"));
    name_->setAlignment(Qt::AlignCenter);
    name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    name_->installEventFilter(this);
    plateLayout->addWidget(name_);

    nameEditor_ = new QLineEdit(namePlate_);
    nameEditor_->setObjectName(QStringLiteral("mixerStripNameEditor"));
    nameEditor_->setAlignment(Qt::AlignCenter);
    nameEditor_->setMaxLength(static_cast<int>(MixerModel::kMaxNameLength));
    nameEditor_->hide();
    nameEditor_->installEventFilter(this);
    connect(nameEditor_, &QLineEdit::editingFinished, this, &MixerStrip::commitInlineRename);
    plateLayout->addWidget(nameEditor_);

    layout->addWidget(namePlate_);

    setLabel(name, number);
    refreshRoutingPills();
    updateCapAndNamePlateStyle();
    syncRackHeights();
}

QSize MixerStrip::minimumSizeHint() const { return {width(), kMinStripHeight}; }

QSize MixerStrip::sizeHint() const { return {width(), 410}; }

void MixerStrip::syncRackHeights()
{
    if (fxRack_ == nullptr)
        return;
    if (!fxRackVisible_) {
        insertsHdr_->setVisible(false);
        fxRack_->setVisible(false);
        if (fxResizeHandle_ != nullptr)
            fxResizeHandle_->setVisible(false);
        if (sendsHdr_ != nullptr)
            sendsHdr_->setVisible(false);
        if (sendRack_ != nullptr)
            sendRack_->setVisible(false);
        return;
    }

    insertsHdr_->setVisible(true);
    if (fxResizeHandle_ != nullptr)
        fxResizeHandle_->setVisible(true);

    // Measure exact available vertical budget above the guaranteed minimum fader height.
    const bool wantSendsSection = sendsHdr_ != nullptr && (sendCount_ > 0 || height() >= 358);
    if (sendsHdr_ != nullptr)
        sendsHdr_->setVisible(wantSendsSection);

    const int fixedChrome = 10 /* margins */ + 16 /* insertsHdr */ + 7 /* fxResizeHandle */
        + (wantSendsSection ? 16 : 0)
        + 40 /* source + output pills */ + 30 /* panBar */ + 20 /* M/S */ + 20 /* Stereo/Mono */ + 15 /* db */
        + kMinFaderHeight + 15 /* number+badge */ + 21 /* namePlate */ + 26 /* layout gaps */;
    const int avail = std::max(18, height() - fixedChrome);

    int sendRows = 0;
    bool showEmptySend = false;
    int sendPx = 0;
    if (sendRack_ != nullptr) {
        if (!wantSendsSection) {
            sendRack_->setVisibleRowBudget(0, false);
        } else if (sendCount_ > 0) {
            const int maxWantedSends = std::min<int>(3, static_cast<int>(sendCount_));
            sendRows = avail >= 110 ? maxWantedSends : 1;
            sendPx = sendRows * kSendRowHeight + 2;
            sendRack_->setVisibleRowBudget(sendRows, false);
        } else if (avail >= 80) {
            sendRows = 1;
            showEmptySend = true;
            sendPx = 18;
            sendRack_->setVisibleRowBudget(sendRows, showEmptySend);
        } else {
            sendRack_->setVisibleRowBudget(0, false);
        }
    }

    const int availForFx = std::max(0, avail - sendPx);
    const int maxFittingSlots = std::clamp((availForFx - 2) / MixerSlotRack::kSlotHeight,
        MixerSlotRack::kMinVisibleSlots, static_cast<int>(MixerModel::kMaxEffects));
    shownFxSlots_ = userFxSlotRows_ <= 0 ? 0 : std::min(userFxSlotRows_, maxFittingSlots);
    if (shownFxSlots_ <= 0) {
        fxRack_->setVisible(false);
    } else {
        fxRack_->setVisible(true);
        fxRack_->setVisibleSlotCount(shownFxSlots_);
    }
}

void MixerStrip::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    syncRackHeights();
}

void MixerStrip::setSelected(bool selected)
{
    if (selected_ == selected)
        return;
    selected_ = selected;
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void MixerStrip::setRole(MixerRole role)
{
    if (role_ == role)
        return;
    role_ = role;
    refreshRoutingPills();
    updateCapAndNamePlateStyle();
}

void MixerStrip::setValues(double fader, double pan, bool muted, bool soloed)
{
    const QSignalBlocker blockFader(fader_);
    const QSignalBlocker blockPan(panBar_);
    const QSignalBlocker blockMute(mute_);
    fader_->setValue(fader);
    panBar_->setValue(pan);
    mute_->setChecked(muted);
    if (solo_ != nullptr) {
        const QSignalBlocker blockSolo(solo_);
        solo_->setChecked(soloed);
    }
    showDb(fader_->value());
}

void MixerStrip::setMono(bool mono)
{
    if (mono_ == mono)
        return;
    mono_ = mono;
    if (monoBtn_ != nullptr) {
        const QSignalBlocker blockMono(monoBtn_);
        monoBtn_->setMono(mono);
    }
    refreshRoutingPills();
}

void MixerStrip::setEffects(const std::vector<PluginSlot>& effects)
{
    effectCount_ = 0;
    for (const PluginSlot& s : effects) {
        if (!s.name.empty())
            ++effectCount_;
    }
    if (fxRack_ != nullptr)
        fxRack_->setEffects(effects);
}

void MixerStrip::setSends(const std::vector<MixerSendDisplay>& sends)
{
    sendCount_ = sends.size();
    if (sendRack_ != nullptr) {
        sendRack_->setSends(sends);
        syncRackHeights();
    }
}

void MixerStrip::setChannelRoutingInfo(bool hasAudio, bool hasInstrument, const QString& instrumentName,
    const QString& sourceLabel, int outputTarget, const QString& outputLabel)
{
    hasAudioSource_ = hasAudio;
    hasInstrumentSource_ = hasInstrument;
    instrumentName_ = instrumentName;
    sourceLabel_ = sourceLabel;
    outputTarget_ = outputTarget;
    outputLabel_ = outputLabel;
    refreshRoutingPills();
    updateCapAndNamePlateStyle();
}

void MixerStrip::setSourceFlags(bool hasAudio, bool hasInstrument)
{
    hasAudioSource_ = hasAudio;
    hasInstrumentSource_ = hasInstrument;
    refreshRoutingPills();
    updateCapAndNamePlateStyle();
}

void MixerStrip::refreshRoutingPills()
{
    if (sourceBtn_ == nullptr || outputBtn_ == nullptr || badge_ == nullptr)
        return;

    const int maxTextW = std::max(28, width() - 16);
    const QFontMetrics fm(sourceBtn_->font());

    if (kind_ == Kind::Master) {
        sourceBtn_->setText(mono_ ? tr("Master · Mono") : tr("Master Mix"));
        sourceBtn_->setToolTip(tr("Master bus summing all channels"));
        sourceBtn_->setProperty("pillKind", QStringLiteral("master"));
        outputBtn_->setText(tr("Main Out"));
        outputBtn_->setToolTip(tr("Hardware audio output"));
        outputBtn_->setProperty("submix", false);
        badge_->setMode(StripTypeBadge::Mode::Master);
    } else {
        switch (role_) {
        case MixerRole::Bus:
            sourceBtn_->setText(mono_ ? tr("Bus · Mono") : tr("Bus Input"));
            sourceBtn_->setToolTip(tr("Submix Bus Channel"));
            sourceBtn_->setProperty("pillKind", QStringLiteral("bus"));
            badge_->setMode(StripTypeBadge::Mode::Bus);
            break;
        case MixerRole::Fx:
            sourceBtn_->setText(mono_ ? tr("FX · Mono") : tr("FX Return"));
            sourceBtn_->setToolTip(tr("Effect Return Channel (feed via Sends)"));
            sourceBtn_->setProperty("pillKind", QStringLiteral("fx"));
            badge_->setMode(StripTypeBadge::Mode::Fx);
            break;
        case MixerRole::Aux:
            sourceBtn_->setText(mono_ ? tr("Aux · Mono") : tr("Aux Input"));
            sourceBtn_->setToolTip(tr("Auxiliary Channel"));
            sourceBtn_->setProperty("pillKind", QStringLiteral("aux"));
            badge_->setMode(StripTypeBadge::Mode::Aux);
            break;
        case MixerRole::Standard:
            if (hasInstrumentSource_) {
                const QString rawInst = instrumentName_.isEmpty() ? tr("Instrument") : instrumentName_;
                sourceBtn_->setText(fm.elidedText(rawInst, Qt::ElideMiddle, maxTextW));
                sourceBtn_->setToolTip(tr("Instrument: %1\nLeft-click to open plug-in editor\nRight-click to "
                                          "replace or open Piano Roll")
                        .arg(rawInst));
                sourceBtn_->setProperty("pillKind", QStringLiteral("instrument"));
                badge_->setMode(StripTypeBadge::Mode::Instrument);
            } else {
                const QString defaultSrc = mono_ ? tr("IN Mono") : tr("IN L+R");
                const QString rawSrc = sourceLabel_.isEmpty() ? defaultSrc : sourceLabel_;
                sourceBtn_->setText(fm.elidedText(rawSrc, Qt::ElideMiddle, maxTextW));
                sourceBtn_->setToolTip(tr("Audio Source: %1\nClick for channel options").arg(rawSrc));
                sourceBtn_->setProperty("pillKind", QStringLiteral("audio"));
                badge_->setMode(StripTypeBadge::Mode::Audio);
            }
            break;
        }

        const QString rawOut = outputLabel_.isEmpty() ? tr("Main") : outputLabel_;
        outputBtn_->setText(fm.elidedText(rawOut, Qt::ElideRight, maxTextW));
        outputBtn_->setToolTip(tr("Audio Output: %1\nClick to route to Main, Bus, or Aux").arg(rawOut));
        outputBtn_->setProperty("submix", outputTarget_ > 0);
    }

    sourceBtn_->style()->unpolish(sourceBtn_);
    sourceBtn_->style()->polish(sourceBtn_);
    outputBtn_->style()->unpolish(outputBtn_);
    outputBtn_->style()->polish(outputBtn_);
}

void MixerStrip::updateCapAndNamePlateStyle()
{
    // Studio One fader cap colours: cyan-blue for Main / Bus / FX / Aux, brushed silver for Audio /
    // Instrument.
    if (fader_ != nullptr) {
        if (kind_ == Kind::Master || role_ == MixerRole::Bus)
            fader_->setCapTint(QColor(0x38, 0xbd, 0xf8));
        else if (role_ == MixerRole::Fx)
            fader_->setCapTint(QColor(0x2d, 0xa8, 0xe8));
        else if (role_ == MixerRole::Aux)
            fader_->setCapTint(QColor(0x2d, 0xd4, 0xbf));
        else
            fader_->setCapTint(std::nullopt);
    }

    if (namePlate_ != nullptr && name_ != nullptr) {
        const Theme::Colors& c = Theme::colors();
        QColor bg = c.window;
        QColor fg = c.text;
        if (color_.has_value()) {
            bg = toQColor(*color_);
            const double lum = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) / 255.0;
            fg = lum > 0.54 ? QColor(0x0f, 0x13, 0x18) : QColor(0xf8, 0xfa, 0xfc);
        } else if (kind_ == Kind::Master) {
            bg = QColor(0x24, 0x29, 0x30);
            fg = c.text;
        }
        namePlate_->setStyleSheet(QStringLiteral("QWidget#mixerNamePlate {"
                                                 "  background-color: %1;"
                                                 "  border-radius: 3px;"
                                                 "}")
                .arg(bg.name()));
        name_->setStyleSheet(
            QStringLiteral("QLabel#stripName { color: %1; font-weight: 700; }").arg(fg.name()));
    }
}

void MixerStrip::setWideMode(bool wide)
{
    if (wideMode_ == wide)
        return;
    wideMode_ = wide;
    const int stripW = (kind_ == Kind::Master) ? (wide ? kMasterWideWidth : kMasterWidth)
                                               : (wide ? kInsertWideWidth : kInsertWidth);
    setFixedWidth(stripW);
    if (fxRack_ != nullptr)
        fxRack_->setFixedWidth(stripW - 8);
    if (sendRack_ != nullptr)
        sendRack_->setFixedWidth(stripW - 8);
    elideName();
    refreshRoutingPills();
    updateGeometry();
    update();
}

void MixerStrip::setFxRackVisible(bool visible)
{
    fxRackVisible_ = visible;
    syncRackHeights();
}

void MixerStrip::setUserFxSlotRows(int rows)
{
    const int clamped = std::clamp(rows, 0, static_cast<int>(MixerModel::kMaxEffects));
    if (userFxSlotRows_ == clamped)
        return;
    userFxSlotRows_ = clamped;
    syncRackHeights();
}

void MixerStrip::setEffectCount(std::size_t count) { effectCount_ = count; }

void MixerStrip::dragEnterEvent(QDragEnterEvent* event)
{
    if (droppedPlugin(event->mimeData()))
        event->acceptProposedAction();
}

void MixerStrip::dropEvent(QDropEvent* event)
{
    const auto plugin = droppedPlugin(event->mimeData());
    if (!plugin)
        return;
    event->acceptProposedAction();
    emit pluginDropped(*plugin);
}

void MixerStrip::setColor(std::optional<std::uint32_t> color)
{
    if (color == color_)
        return;
    color_ = color;
    updateCapAndNamePlateStyle();
    update();
}

void MixerStrip::mousePressEvent(QMouseEvent* event)
{
    emit clicked();
    if (event->button() == Qt::LeftButton && event->position().y() <= 7.0) {
        emit colorPickRequested(mapToGlobal(QPoint(0, 8)));
        return;
    }
    QFrame::mousePressEvent(event);
}

void MixerStrip::paintEvent(QPaintEvent* event)
{
    QFrame::paintEvent(event);
    const Theme::Colors& c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (selected_) {
        QColor tint = color_ ? toQColor(*color_) : c.accent;
        tint.setAlpha(20);
        p.setPen(Qt::NoPen);
        p.setBrush(tint);
        p.drawRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), 5.0, 5.0);

        QColor ring = color_ ? toQColor(*color_).lighter(120) : c.accent;
        ring.setAlpha(205);
        p.setPen(QPen(ring, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0), 5.0, 5.0);
    }

    // Studio One top colour accent bar.
    p.setPen(Qt::NoPen);
    QColor bar = color_ ? toQColor(*color_) : (selected_ ? c.accent : c.textDim);
    if (!color_ && !selected_)
        bar.setAlpha(75);
    p.setBrush(bar);
    p.drawRoundedRect(QRectF(3, 1.5, width() - 6, selected_ ? 3.5 : 3.0), 1.2, 1.2);
}

void MixerStrip::startInlineRename()
{
    if (kind_ == Kind::Master || nameEditor_ == nullptr || name_ == nullptr)
        return;
    const Theme::Colors& c = Theme::colors();
    nameEditor_->setStyleSheet(QStringLiteral("QLineEdit#mixerStripNameEditor {"
                                              "  background-color: %1;"
                                              "  color: %2;"
                                              "  border: 1px solid %3;"
                                              "  border-radius: 2px;"
                                              "  padding: 0px 2px;"
                                              "  font-weight: 700;"
                                              "  font-size: 11px;"
                                              "}")
            .arg(c.base.name(), c.text.name(), c.accent.name()));
    cancellingRename_ = false;
    committingRename_ = false;
    name_->hide();
    nameEditor_->setText(fullName_);
    nameEditor_->selectAll();
    nameEditor_->show();
    nameEditor_->setFocus();
}

void MixerStrip::commitInlineRename()
{
    if (nameEditor_ == nullptr || !nameEditor_->isVisible() || committingRename_ || cancellingRename_)
        return;
    committingRename_ = true;
    const QString edited = nameEditor_->text().trimmed();
    nameEditor_->hide();
    name_->show();
    committingRename_ = false;
    if (edited != fullName_)
        emit nameEdited(edited);
}

void MixerStrip::cancelInlineRename()
{
    if (nameEditor_ == nullptr || !nameEditor_->isVisible())
        return;
    cancellingRename_ = true;
    nameEditor_->hide();
    name_->show();
    cancellingRename_ = false;
}

bool MixerStrip::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == nameEditor_ && event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            cancelInlineRename();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            commitInlineRename();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress && watched != nameEditor_) {
        if (nameEditor_ != nullptr && nameEditor_->isVisible())
            commitInlineRename();
        emit clicked();
    }

    if (watched == name_ || watched == namePlate_) {
        switch (event->type()) {
        case QEvent::MouseButtonDblClick:
            if (solo_ != nullptr) {
                startInlineRename();
                return true;
            }
            break;
        case QEvent::Resize:
        case QEvent::FontChange:
        case QEvent::StyleChange:
        case QEvent::Polish:
            elideName();
            break;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void MixerStrip::elideName()
{
    if (name_ == nullptr)
        return;
    const int availW = std::max(20, width() - 14);
    const QString shown = name_->fontMetrics().elidedText(fullName_, Qt::ElideRight, availW);
    if (shown != name_->text())
        name_->setText(shown);
}

void MixerStrip::setLabel(const QString& name, const QString& number)
{
    fullName_ = name;
    elideName();
    name_->setToolTip(kind_ == Kind::Master ? name : tr("%1\nDouble-click to rename").arg(name));
    number_->setText(number);
    const QString label = solo_ == nullptr ? name : tr("%1 (%2)").arg(name, number);
    fader_->setAccessibleName(tr("%1 volume").arg(label));
    panBar_->setAccessibleName(tr("%1 pan").arg(label));
    const QString muteTip = tr("Mute %1").arg(label);
    mute_->setToolTip(muteTip);
    mute_->setAccessibleName(muteTip);
    if (solo_ != nullptr) {
        const QString soloTip = tr("Solo %1").arg(label);
        solo_->setToolTip(soloTip);
        solo_->setAccessibleName(soloTip);
    }
}

void MixerStrip::setSilenced(bool silenced)
{
    if (property("silenced").toBool() == silenced)
        return;
    setProperty("silenced", silenced);
    style()->unpolish(this);
    style()->polish(this);
    for (QLabel* label : {db_, number_}) {
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
}

void MixerStrip::showDb(double position)
{
    db_->setText(QString::fromStdString(formatDb(faderToGain(position))));
}

} // namespace vloomdaw

#include "MixerStrip.moc"
