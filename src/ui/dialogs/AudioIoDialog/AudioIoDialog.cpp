// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/AudioIoDialog/AudioIoDialog.hpp"

#include "core/Decibels.hpp"
#include "ui/Theme.hpp"
#include "ui/dialogs/SettingsDialog/AudioSettingsPage.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kRowHeight = 28;
constexpr int kHeaderHeight = 82;
constexpr int kSelectStripW = 14;
constexpr int kNameColW = 168;
constexpr int kUsedColW = 48;
constexpr int kMeterColW = 16;
constexpr int kModeColW = 86;
constexpr int kIndexColW = 28;
constexpr int kCellW = 30;
constexpr int kLeftFixedW
    = kSelectStripW + kNameColW + 1 + kUsedColW + 4 + kMeterColW + 4 + kModeColW + 4 + kIndexColW + 4;

double peakToMeterNorm(float peak)
{
    if (!(peak > 0.0f))
        return 0.0;
    const double db = gainToDb(peak);
    if (!std::isfinite(db))
        return 0.0;
    return std::clamp((db - (-60.0)) / (6.0 - (-60.0)), 0.0, 1.0);
}

QString outputChannelTitle(int ch)
{
    switch (ch) {
    case 0:
        return QObject::tr("0 FL (Front Left)");
    case 1:
        return QObject::tr("1 FR (Front Right)");
    case 2:
        return QObject::tr("2 Out 3");
    case 3:
        return QObject::tr("3 Out 4");
    default:
        return QObject::tr("%1 Out %2").arg(ch).arg(ch + 1);
    }
}

QString inputChannelTitle(int ch) { return QObject::tr("IN %1").arg(ch); }

} // namespace

/// Custom-painted Vloom Studio Rack Audio Interface illustration + active device title.
class IoInterfaceBanner final : public QWidget {
public:
    explicit IoInterfaceBanner(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(230, 84);
    }

    void setDeviceTitle(const QString& title, bool active)
    {
        if (title_ == title && active_ == active)
            return;
        title_ = title;
        active_ = active;
        update();
    }

    void setMeterLevels(double left, double right)
    {
        if (std::abs(left - meterL_) < 0.01 && std::abs(right - meterR_) < 0.01)
            return;
        meterL_ = left;
        meterR_ = right;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const double cx = width() * 0.5;
        const QRectF face(cx - 66.0, 16.0, 132.0, 30.0);

        // 3D perspective top lid of the rack audio interface
        QPolygonF topLid;
        topLid << QPointF(face.left() + 10.0, 6.0) << QPointF(face.right() - 10.0, 6.0)
               << QPointF(face.right() - 1.5, face.top() + 1.0)
               << QPointF(face.left() + 1.5, face.top() + 1.0);
        p.setPen(Qt::NoPen);
        QColor lidCol = c.text;
        lidCol.setAlpha(active_ ? 210 : 115);
        p.setBrush(lidCol);
        p.drawPolygon(topLid);

        // Brushed dark studio faceplate with crisp light bezel
        QColor bezelCol = c.text;
        bezelCol.setAlpha(active_ ? 235 : 130);
        p.setPen(QPen(bezelCol, 1.6));
        p.setBrush(c.base);
        p.drawRoundedRect(face, 4.0, 4.0);

        // Left rack ears / status pips
        for (int row = 0; row < 3; ++row) {
            const double py = face.top() + 7.5 + static_cast<double>(row) * 7.0;
            QColor pip = (row == 0 && active_) ? c.playing : c.textDim;
            p.setPen(Qt::NoPen);
            p.setBrush(pip);
            p.drawEllipse(QPointF(face.left() + 9.0, py), 1.5, 1.5);
        }

        // Dual vertical 5-segment LED meters on the faceplate
        for (int ch = 0; ch < 2; ++ch) {
            const double lv = ch == 0 ? meterL_ : meterR_;
            const double mx = face.left() + 22.0 + static_cast<double>(ch) * 12.0;
            for (int seg = 0; seg < 5; ++seg) {
                const double thresh = static_cast<double>(4 - seg) * 0.2;
                const double sy = face.top() + 6.0 + static_cast<double>(seg) * 4.2;
                QColor segCol = c.raised;
                if (active_ && lv > thresh)
                    segCol = (seg == 0) ? c.danger : (seg == 1 ? c.accent : c.displayText);
                else
                    segCol = c.border;
                p.fillRect(QRectF(mx, sy, 8.0, 2.6), segCol);
            }
        }

        // Dual Vloom gain knobs
        for (int k = 0; k < 2; ++k) {
            const QPointF kc(face.left() + 62.0 + static_cast<double>(k) * 24.0, face.center().y() + 1.0);
            p.setPen(QPen(c.border, 1.0));
            p.setBrush(active_ ? c.text : c.textDim);
            p.drawEllipse(kc, 6.2, 6.2);
            p.setPen(QPen(c.accent, 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(kc, QPointF(kc.x() + 2.6, kc.y() - 3.6));
        }

        // Headphone & XLR/TRS jack sockets on the right
        const QPointF hp(face.right() - 15.0, face.top() + 10.5);
        p.setPen(QPen(bezelCol, 1.3));
        p.setBrush(c.display);
        p.drawEllipse(hp, 3.6, 3.6);
        p.setPen(Qt::NoPen);
        p.setBrush(c.textDim);
        p.drawRoundedRect(QRectF(face.right() - 22.0, face.bottom() - 10.0, 5.0, 5.0), 1.0, 1.0);
        p.drawRoundedRect(QRectF(face.right() - 14.0, face.bottom() - 10.0, 5.0, 5.0), 1.0, 1.0);

        // Device label centered below the rack unit
        QFont f = font();
        f.setPointSizeF(10.0);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.setPen(active_ ? c.text : c.textDim);
        const QFontMetrics fm(f);
        const QRect textRect(4, 52, width() - 8, 26);
        p.drawText(textRect, Qt::AlignCenter, fm.elidedText(title_, Qt::ElideMiddle, textRect.width()));
    }

private:
    QString title_;
    bool active_ = false;
    double meterL_ = 0.0;
    double meterR_ = 0.0;
};

/// Interactive Studio Audio I/O Matrix Canvas.
class IoMatrixCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit IoMatrixCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        nameEdit_ = new QLineEdit(this);
        nameEdit_->hide();
        nameEdit_->setMaxLength(32);
        nameEdit_->installEventFilter(this);
        connect(nameEdit_, &QLineEdit::editingFinished, this, &IoMatrixCanvas::commitInlineRename);
    }

    void setState(AudioIoDialog::Tab tab, const std::vector<AudioIoBus>& buses, int activeBus, int hwChannels)
    {
        cancelInlineRename();
        tab_ = tab;
        buses_ = buses;
        activeBus_ = activeBus;
        int maxMapped = 1;
        for (const AudioIoBus& b : buses_) {
            maxMapped = std::max(maxMapped, b.leftChannel);
            if (b.stereo)
                maxMapped = std::max(maxMapped, b.rightChannel);
        }
        colCount_ = std::clamp(
            std::max(std::max(2, hwChannels), maxMapped + 1), 2, AudioIoConfig::kMaxHardwareChannels);
        if (selectedRow_ < 0 || static_cast<std::size_t>(selectedRow_) >= buses_.size())
            selectedRow_ = std::clamp(activeBus_, 0, std::max(0, static_cast<int>(buses_.size()) - 1));
        syncCanvasSize();
        update();
    }

    [[nodiscard]] int selectedRow() const { return selectedRow_; }
    [[nodiscard]] int columnCount() const { return colCount_; }

    void updateMeters(
        const std::array<double, AudioIoConfig::kMaxHardwareChannels>& inLevels, double outL, double outR)
    {
        inLevels_ = inLevels;
        outL_ = outL;
        outR_ = outR;
        update();
    }

signals:
    void busesEdited(const std::vector<vloomdaw::AudioIoBus>& buses, int activeBus);
    void selectionChanged(int row);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == nameEdit_ && event->type() == QEvent::KeyPress) {
            const auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                cancelInlineRename();
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const int gridStartX = 16 + kLeftFixedW;

        // 1. Column headers ("Used" + 45-degree rotated hardware channel labels + 1..N boxes)
        QFont smallBold = font();
        smallBold.setPointSizeF(8.2);
        smallBold.setWeight(QFont::DemiBold);
        p.setFont(smallBold);
        p.setPen(c.textDim);

        const int usedX = 16 + kSelectStripW + kNameColW + 1;
        p.drawText(QRect(usedX, kHeaderHeight - 24, kUsedColW, 20), Qt::AlignCenter, tr("Used"));

        for (int col = 0; col < colCount_; ++col) {
            const int cx = gridStartX + col * (kCellW + 2);
            const QString chTitle
                = (tab_ == AudioIoDialog::Tab::Inputs) ? inputChannelTitle(col) : outputChannelTitle(col);

            p.save();
            p.translate(
                static_cast<double>(cx) + kCellW * 0.5 + 3.0, static_cast<double>(kHeaderHeight - 28));
            p.rotate(-45.0);
            p.setPen(c.text);
            p.drawText(QRect(0, -10, 140, 18), Qt::AlignLeft | Qt::AlignVCenter, chTitle);
            p.restore();

            const QRect numBox(cx, kHeaderHeight - 24, kCellW, 22);
            p.fillRect(numBox, c.raised);
            p.setPen(c.border);
            p.drawRect(numBox.adjusted(0, 0, -1, -1));
            p.setPen(c.text);
            p.drawText(numBox, Qt::AlignCenter, QString::number(col + 1));
        }

        // 2. Bus rows
        for (int r = 0; r < static_cast<int>(buses_.size()); ++r) {
            const AudioIoBus& bus = buses_[static_cast<std::size_t>(r)];
            const int y = kHeaderHeight + r * (kRowHeight + 2);
            const bool isSelected = (r == selectedRow_);
            const bool isActive = (r == activeBus_);
            const QColor rowBg = isSelected ? c.raised : c.window;

            int x = 16;

            // Left selection handle
            const QRect selRect(x, y, kSelectStripW, kRowHeight);
            p.fillRect(selRect, isSelected ? c.accent : rowBg);
            p.setPen(c.border);
            p.drawRect(selRect.adjusted(0, 0, -1, -1));
            x += kSelectStripW;

            // Bus Name cell
            const QRect nameRect(x, y, kNameColW, kRowHeight);
            p.fillRect(nameRect, rowBg);
            p.setPen(isSelected ? c.textDim : c.border);
            p.drawRect(nameRect.adjusted(0, 0, -1, -1));
            p.setPen(c.text);
            p.setFont(smallBold);
            const QFontMetrics fm(smallBold);
            p.drawText(nameRect.adjusted(8, 0, -8, 0), Qt::AlignCenter,
                fm.elidedText(QString::fromStdString(bus.name), Qt::ElideMiddle, nameRect.width() - 16));
            x += kNameColW + 1;

            // Used indicator cell (clickable to activate this bus!)
            const QRect usedRect(x, y, kUsedColW, kRowHeight);
            p.fillRect(usedRect, rowBg);
            p.setPen(c.border);
            p.drawRect(usedRect.adjusted(0, 0, -1, -1));
            if (isActive) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.text);
                p.drawEllipse(QRectF(usedRect).center(), 4.2, 4.2);
            } else if (hoverRow_ == r && hoverZone_ == Zone::Used) {
                QColor ghost = c.textDim;
                ghost.setAlpha(110);
                p.setPen(Qt::NoPen);
                p.setBrush(ghost);
                p.drawEllipse(QRectF(usedRect).center(), 3.6, 3.6);
            }
            x += kUsedColW + 4;

            // Live VU Meter bar(s)
            const QRect meterRect(x, y + 2, kMeterColW, kRowHeight - 4);
            double lvL = 0.0;
            double lvR = 0.0;
            if (tab_ == AudioIoDialog::Tab::Inputs) {
                const int lc = std::clamp(bus.leftChannel, 0, AudioIoConfig::kMaxHardwareChannels - 1);
                const int rc = std::clamp(bus.rightChannel, 0, AudioIoConfig::kMaxHardwareChannels - 1);
                lvL = inLevels_[static_cast<std::size_t>(lc)];
                lvR = bus.stereo ? inLevels_[static_cast<std::size_t>(rc)] : lvL;
            } else if (isActive) {
                lvL = outL_;
                lvR = bus.stereo ? outR_ : std::max(outL_, outR_);
            }
            const QColor meterFill(0x38, 0xbd, 0xf8);
            if (!bus.stereo) {
                const QRect bar(meterRect.center().x() - 3, meterRect.top(), 6, meterRect.height());
                p.fillRect(bar, c.display);
                const int litH = static_cast<int>(std::lround(lvL * bar.height()));
                if (litH > 0)
                    p.fillRect(QRect(bar.left(), bar.bottom() - litH + 1, bar.width(), litH), meterFill);
            } else {
                for (int ch = 0; ch < 2; ++ch) {
                    const QRect bar(meterRect.left() + 3 + ch * 5, meterRect.top(), 4, meterRect.height());
                    p.fillRect(bar, c.display);
                    const double lv = ch == 0 ? lvL : lvR;
                    const int litH = static_cast<int>(std::lround(lv * bar.height()));
                    if (litH > 0)
                        p.fillRect(QRect(bar.left(), bar.bottom() - litH + 1, bar.width(), litH), meterFill);
                }
            }
            x += kMeterColW + 4;

            // Mono / Stereo combo pill
            const QRect modeRect(x, y, kModeColW, kRowHeight);
            const bool modeHover = (hoverRow_ == r && hoverZone_ == Zone::Mode);
            p.fillRect(modeRect, modeHover ? c.raised.lighter(112) : rowBg);
            p.setPen(modeHover ? c.textDim : c.border);
            p.drawRect(modeRect.adjusted(0, 0, -1, -1));
            p.setPen(c.text);
            p.drawText(
                modeRect.adjusted(8, 0, -18, 0), Qt::AlignCenter, bus.stereo ? tr("Stereo") : tr("Mono"));
            QPolygonF chev;
            const double chx = modeRect.right() - 10.0;
            const double chy = modeRect.center().y() + 0.5;
            chev << QPointF(chx - 3.2, chy - 1.6) << QPointF(chx + 3.2, chy - 1.6) << QPointF(chx, chy + 2.2);
            p.setPen(Qt::NoPen);
            p.setBrush(c.textDim);
            p.drawPolygon(chev);
            x += kModeColW + 4;

            // Bus index cell (1..N)
            const QRect idxRect(x, y, kIndexColW, kRowHeight);
            p.fillRect(idxRect, rowBg);
            p.setPen(c.border);
            p.drawRect(idxRect.adjusted(0, 0, -1, -1));
            p.setPen(c.text);
            p.drawText(idxRect, Qt::AlignCenter, QString::number(r + 1));

            // Matrix cells [M] or [L][R]
            for (int col = 0; col < colCount_; ++col) {
                const int cx = gridStartX + col * (kCellW + 2);
                const QRect cellRect(cx, y, kCellW, kRowHeight);
                const bool isL = (bus.leftChannel == col);
                const bool isR = (bus.stereo && bus.rightChannel == col);
                const bool assigned = isL || isR;
                const bool cellHover = (hoverRow_ == r && hoverZone_ == Zone::Cell && hoverCol_ == col);

                QColor cellBg = c.base;
                if (assigned) {
                    cellBg = isActive ? c.accent : c.raised.lighter(128);
                    if (isActive)
                        cellBg.setAlpha(215);
                } else if (cellHover) {
                    cellBg = c.raised;
                }
                p.fillRect(cellRect, cellBg);
                p.setPen(assigned && isActive ? c.accent : c.border);
                p.drawRect(cellRect.adjusted(0, 0, -1, -1));

                if (assigned) {
                    const QString badge = !bus.stereo ? QStringLiteral("M")
                        : (isL && isR)                ? QStringLiteral("LR")
                        : isL                         ? QStringLiteral("L")
                                                      : QStringLiteral("R");
                    p.setPen(isActive ? c.window : c.text);
                    p.drawText(cellRect, Qt::AlignCenter, badge);
                }
            }
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const Hit h = hitTest(event->position().toPoint());
        if (h.row != hoverRow_ || h.zone != hoverZone_ || h.col != hoverCol_) {
            hoverRow_ = h.row;
            hoverZone_ = h.zone;
            hoverCol_ = h.col;
            setCursor(h.zone == Zone::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
            update();
        }
    }

    void leaveEvent(QEvent* /*event*/) override
    {
        hoverRow_ = -1;
        hoverZone_ = Zone::None;
        hoverCol_ = -1;
        unsetCursor();
        update();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        commitInlineRename();
        const Hit h = hitTest(event->position().toPoint());
        if (h.row < 0 || static_cast<std::size_t>(h.row) >= buses_.size())
            return;

        if (selectedRow_ != h.row) {
            selectedRow_ = h.row;
            emit selectionChanged(selectedRow_);
            update();
        }

        auto& bus = buses_[static_cast<std::size_t>(h.row)];
        if (h.zone == Zone::Used && event->button() == Qt::LeftButton) {
            activeBus_ = h.row;
            emit busesEdited(buses_, activeBus_);
            update();
            return;
        }

        if (h.zone == Zone::Mode && event->button() == Qt::LeftButton) {
            QMenu menu(this);
            QAction* monoAct = menu.addAction(tr("Mono (M)"));
            monoAct->setCheckable(true);
            monoAct->setChecked(!bus.stereo);
            QAction* stereoAct = menu.addAction(tr("Stereo (L / R)"));
            stereoAct->setCheckable(true);
            stereoAct->setChecked(bus.stereo);
            const QAction* picked = menu.exec(event->globalPosition().toPoint());
            if (picked == monoAct && bus.stereo) {
                bus.stereo = false;
                bus.rightChannel = bus.leftChannel;
                emit busesEdited(buses_, activeBus_);
                update();
            } else if (picked == stereoAct && !bus.stereo) {
                bus.stereo = true;
                bus.rightChannel = (bus.leftChannel + 1 < colCount_) ? (bus.leftChannel + 1)
                                                                     : std::max(0, bus.leftChannel - 1);
                emit busesEdited(buses_, activeBus_);
                update();
            }
            return;
        }

        if (h.zone == Zone::Cell && h.col >= 0 && h.col < colCount_) {
            if (!bus.stereo) {
                bus.leftChannel = h.col;
                bus.rightChannel = h.col;
            } else if (event->button() == Qt::RightButton || event->modifiers().testFlag(Qt::ShiftModifier)) {
                bus.rightChannel = h.col;
            } else {
                bus.leftChannel = h.col;
                bus.rightChannel = (h.col + 1 < colCount_) ? (h.col + 1) : std::max(0, h.col - 1);
            }
            activeBus_ = h.row;
            emit busesEdited(buses_, activeBus_);
            update();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const Hit h = hitTest(event->position().toPoint());
        if (h.row >= 0 && static_cast<std::size_t>(h.row) < buses_.size() && h.zone == Zone::Name
            && event->button() == Qt::LeftButton) {
            startInlineRename(h.row);
        }
    }

private:
    enum class Zone { None, Select, Name, Used, Mode, Cell };

    struct Hit {
        int row = -1;
        Zone zone = Zone::None;
        int col = -1;
    };

    [[nodiscard]] Hit hitTest(QPoint pt) const
    {
        Hit out;
        if (pt.y() < kHeaderHeight)
            return out;
        const int relY = pt.y() - kHeaderHeight;
        const int r = relY / (kRowHeight + 2);
        if (r < 0 || static_cast<std::size_t>(r) >= buses_.size() || (relY % (kRowHeight + 2)) >= kRowHeight)
            return out;
        out.row = r;

        int x = 16;
        if (pt.x() >= x && pt.x() < x + kSelectStripW) {
            out.zone = Zone::Select;
            return out;
        }
        x += kSelectStripW;
        if (pt.x() >= x && pt.x() < x + kNameColW) {
            out.zone = Zone::Name;
            return out;
        }
        x += kNameColW + 1;
        if (pt.x() >= x && pt.x() < x + kUsedColW) {
            out.zone = Zone::Used;
            return out;
        }
        x += kUsedColW + 4 + kMeterColW + 4;
        if (pt.x() >= x && pt.x() < x + kModeColW) {
            out.zone = Zone::Mode;
            return out;
        }

        const int gridStartX = 16 + kLeftFixedW;
        if (pt.x() >= gridStartX) {
            const int relX = pt.x() - gridStartX;
            const int col = relX / (kCellW + 2);
            if (col >= 0 && col < colCount_ && (relX % (kCellW + 2)) < kCellW) {
                out.zone = Zone::Cell;
                out.col = col;
            }
        }
        return out;
    }

    void startInlineRename(int row)
    {
        if (row < 0 || static_cast<std::size_t>(row) >= buses_.size())
            return;
        editingRow_ = row;
        const int y = kHeaderHeight + row * (kRowHeight + 2);
        const QRect nameRect(16 + kSelectStripW, y, kNameColW, kRowHeight);
        const Theme::Colors& c = Theme::colors();
        nameEdit_->setStyleSheet(
            QStringLiteral("QLineEdit { background: %1; color: %2; border: 1px solid %3; padding: 0 6px; "
                           "font-weight: 600; }")
                .arg(c.base.name(), c.text.name(), c.accent.name()));
        nameEdit_->setGeometry(nameRect);
        nameEdit_->setText(QString::fromStdString(buses_[static_cast<std::size_t>(row)].name));
        nameEdit_->selectAll();
        nameEdit_->show();
        nameEdit_->setFocus();
    }

    void commitInlineRename()
    {
        if (editingRow_ < 0 || !nameEdit_->isVisible())
            return;
        const int row = editingRow_;
        editingRow_ = -1;
        const QString trimmed = nameEdit_->text().trimmed();
        nameEdit_->hide();
        if (row >= 0 && static_cast<std::size_t>(row) < buses_.size() && !trimmed.isEmpty()) {
            buses_[static_cast<std::size_t>(row)].name = trimmed.toStdString();
            emit busesEdited(buses_, activeBus_);
            update();
        }
    }

    void cancelInlineRename()
    {
        editingRow_ = -1;
        if (nameEdit_ != nullptr)
            nameEdit_->hide();
    }

    void syncCanvasSize()
    {
        const int w = 16 + kLeftFixedW + colCount_ * (kCellW + 2) + 80;
        const int h = kHeaderHeight + static_cast<int>(buses_.size()) * (kRowHeight + 2) + 28;
        setMinimumSize(w, h);
    }

    AudioIoDialog::Tab tab_ = AudioIoDialog::Tab::Inputs;
    std::vector<AudioIoBus> buses_;
    int activeBus_ = 0;
    int selectedRow_ = 0;
    int colCount_ = 2;
    int hoverRow_ = -1;
    Zone hoverZone_ = Zone::None;
    int hoverCol_ = -1;
    int editingRow_ = -1;
    QLineEdit* nameEdit_ = nullptr;
    std::array<double, AudioIoConfig::kMaxHardwareChannels> inLevels_{};
    double outL_ = 0.0;
    double outR_ = 0.0;
};

AudioIoDialog::AudioIoDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("audioIoDialog"));
    setWindowTitle(tr("Audio I/O Setup"));
    setMinimumSize(680, 480);
    resize(740, 540);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(0);

    // 1. Top Segmented Tab Row (Inputs | Outputs) + Active Driver Pill
    auto* tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->setSpacing(2);

    inputsTabBtn_ = new QPushButton(tr("Inputs"), this);
    inputsTabBtn_->setObjectName(QStringLiteral("ioTabButton"));
    inputsTabBtn_->setCheckable(true);
    inputsTabBtn_->setChecked(true);
    inputsTabBtn_->setFocusPolicy(Qt::NoFocus);

    outputsTabBtn_ = new QPushButton(tr("Outputs"), this);
    outputsTabBtn_->setObjectName(QStringLiteral("ioTabButton"));
    outputsTabBtn_->setCheckable(true);
    outputsTabBtn_->setFocusPolicy(Qt::NoFocus);

    auto* tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);
    tabGroup->addButton(inputsTabBtn_, static_cast<int>(Tab::Inputs));
    tabGroup->addButton(outputsTabBtn_, static_cast<int>(Tab::Outputs));
    connect(tabGroup, &QButtonGroup::idClicked, this, [this](int id) {
        currentTab_ = static_cast<Tab>(id);
        syncUiFromTab();
    });

    tabRow->addWidget(inputsTabBtn_);
    tabRow->addWidget(outputsTabBtn_);
    tabRow->addStretch(1);

    driverBadge_ = new QLabel(QString::fromLatin1(nativeAudioDriverName()), this);
    driverBadge_->setObjectName(QStringLiteral("panelHint"));
    tabRow->addWidget(driverBadge_);
    root->addLayout(tabRow);

    // 2. Main Studio I/O Matrix Panel
    auto* matrixFrame = new QFrame(this);
    matrixFrame->setObjectName(QStringLiteral("ioMatrixPanel"));
    auto* matrixLayout = new QVBoxLayout(matrixFrame);
    matrixLayout->setContentsMargins(18, 14, 18, 14);
    matrixLayout->setSpacing(8);

    auto* topBannerRow = new QHBoxLayout;
    topBannerRow->setContentsMargins(8, 0, 0, 0);
    topBannerRow->setSpacing(16);

    banner_ = new IoInterfaceBanner(matrixFrame);
    topBannerRow->addWidget(banner_);

    auto* devPickerCol = new QVBoxLayout;
    devPickerCol->setContentsMargins(0, 12, 0, 0);
    devPickerCol->setSpacing(4);
    auto* devTitle = new QLabel(tr("Active Hardware Endpoint"), matrixFrame);
    devTitle->setObjectName(QStringLiteral("panelHint"));
    deviceCombo_ = new QComboBox(matrixFrame);
    deviceCombo_->setMinimumWidth(260);
    connect(deviceCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (currentTab_ == Tab::Inputs)
            emit inputDeviceSelected(deviceCombo_->currentData().toInt());
        else
            emit outputDeviceSelected(deviceCombo_->currentData().toInt());
    });
    auto* hintLbl = new QLabel(
        tr("Click [M] / [L][R] cells to route channels, click Used (●) to select active bus, double-click "
           "name to rename."),
        matrixFrame);
    hintLbl->setObjectName(QStringLiteral("settingsRowDescription"));
    hintLbl->setWordWrap(true);
    devPickerCol->addWidget(devTitle);
    devPickerCol->addWidget(deviceCombo_);
    devPickerCol->addWidget(hintLbl);
    devPickerCol->addStretch(1);
    topBannerRow->addLayout(devPickerCol, 1);

    matrixLayout->addLayout(topBannerRow);

    canvas_ = new IoMatrixCanvas(matrixFrame);
    connect(canvas_, &IoMatrixCanvas::busesEdited, this,
        [this](const std::vector<AudioIoBus>& buses, int activeBus) {
            AudioIoConfig next = config_;
            if (currentTab_ == Tab::Inputs) {
                next.inputs = buses;
                next.activeInputBus = activeBus;
            } else {
                next.outputs = buses;
                next.activeOutputBus = activeBus;
            }
            commitConfigEdit(std::move(next));
        });
    connect(canvas_, &IoMatrixCanvas::selectionChanged, this, [this](int /*row*/) {
        const auto& list = (currentTab_ == Tab::Inputs) ? config_.inputs : config_.outputs;
        removeBtn_->setEnabled(list.size() > 1);
    });

    matrixScroll_ = new QScrollArea(matrixFrame);
    matrixScroll_->setWidget(canvas_);
    matrixScroll_->setWidgetResizable(true);
    matrixScroll_->setFrameShape(QFrame::NoFrame);
    matrixLayout->addWidget(matrixScroll_, 1);

    root->addWidget(matrixFrame, 1);
    root->addSpacing(10);

    // 3. Bottom Action Bar: Add (Mono), Add (Stereo), Add…, Remove | Audio Settings… | Close
    auto* actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);

    addMonoBtn_ = new QPushButton(tr("Add (Mono)"), this);
    addMonoBtn_->setAutoDefault(false);
    connect(addMonoBtn_, &QPushButton::clicked, this, [this] { addBus(false); });

    addStereoBtn_ = new QPushButton(tr("Add (Stereo)"), this);
    addStereoBtn_->setAutoDefault(false);
    connect(addStereoBtn_, &QPushButton::clicked, this, [this] { addBus(true); });

    addMenuBtn_ = new QPushButton(tr("Add…"), this);
    addMenuBtn_->setAutoDefault(false);
    connect(addMenuBtn_, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        menu.addAction(tr("Add Mono Bus"), this, [this] { addBus(false); });
        menu.addAction(tr("Add Stereo Bus"), this, [this] { addBus(true); });
        menu.addSeparator();
        menu.addAction(tr("Reset I/O Matrix to Default"), this, [this] {
            AudioIoConfig def = defaultAudioIoConfig();
            def.unifiedDevice = config_.unifiedDevice;
            commitConfigEdit(std::move(def));
        });
        menu.exec(addMenuBtn_->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });

    removeBtn_ = new QPushButton(tr("Remove"), this);
    removeBtn_->setAutoDefault(false);
    connect(removeBtn_, &QPushButton::clicked, this, &AudioIoDialog::removeSelectedBus);

    auto* audioPrefsBtn = new QPushButton(tr("Audio Settings…"), this);
    audioPrefsBtn->setAutoDefault(false);
    connect(audioPrefsBtn, &QPushButton::clicked, this, &AudioIoDialog::audioSettingsRequested);

    auto* closeBtn = new QPushButton(tr("OK"), this);
    closeBtn->setObjectName(QStringLiteral("accentButton"));
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    actionRow->addWidget(addMonoBtn_);
    actionRow->addWidget(addStereoBtn_);
    actionRow->addWidget(addMenuBtn_);
    actionRow->addWidget(removeBtn_);
    actionRow->addStretch(1);
    actionRow->addWidget(audioPrefsBtn);
    actionRow->addWidget(closeBtn);
    root->addLayout(actionRow);

    syncUiFromTab();
}

void AudioIoDialog::setConfig(const AudioIoConfig& config)
{
    config_ = clampedAudioIoConfig(config);
    syncUiFromTab();
}

void AudioIoDialog::setHardwareState(const QString& outDeviceName, int outChannels,
    const QStringList& outDeviceList, int outSelectedIdx, const QString& inDeviceName, int inChannels,
    const QStringList& inDeviceList, int inSelectedCode)
{
    outDeviceName_ = outDeviceName;
    outChannels_ = std::max(1, outChannels);
    outDeviceList_ = outDeviceList;
    outSelectedIdx_ = outSelectedIdx;

    inDeviceName_ = inDeviceName;
    inChannels_ = std::max(1, inChannels);
    inDeviceList_ = inDeviceList;
    inSelectedCode_ = inSelectedCode;

    syncUiFromTab();
}

void AudioIoDialog::setHardwarePeaks(const std::array<float, AudioIoConfig::kMaxHardwareChannels>& inPeaks,
    float outPeakL, float outPeakR, double dt)
{
    const double decay = std::max(0.0, dt) * 1.8;

    for (std::size_t c = 0; c < AudioIoConfig::kMaxHardwareChannels; ++c) {
        const double inst = peakToMeterNorm(inPeaks[c]);
        smoothIn_[c] = std::max(inst, std::max(0.0, smoothIn_[c] - decay));
    }
    smoothOutL_ = std::max(peakToMeterNorm(outPeakL), std::max(0.0, smoothOutL_ - decay));
    smoothOutR_ = std::max(peakToMeterNorm(outPeakR), std::max(0.0, smoothOutR_ - decay));

    canvas_->updateMeters(smoothIn_, smoothOutL_, smoothOutR_);
    if (currentTab_ == Tab::Inputs)
        banner_->setMeterLevels(smoothIn_[0], smoothIn_[1]);
    else
        banner_->setMeterLevels(smoothOutL_, smoothOutR_);
}

void AudioIoDialog::showTab(Tab tab)
{
    currentTab_ = tab;
    {
        const QSignalBlocker b1(inputsTabBtn_);
        const QSignalBlocker b2(outputsTabBtn_);
        inputsTabBtn_->setChecked(tab == Tab::Inputs);
        outputsTabBtn_->setChecked(tab == Tab::Outputs);
    }
    syncUiFromTab();
    show();
    raise();
    activateWindow();
}

void AudioIoDialog::syncUiFromTab()
{
    const QSignalBlocker blockCombo(deviceCombo_);
    deviceCombo_->clear();

    if (currentTab_ == Tab::Inputs) {
        const bool running = !inDeviceName_.isEmpty();
        banner_->setDeviceTitle(running ? inDeviceName_ : tr("No Input Device"), running);
        deviceCombo_->addItem(tr("None (Disabled)"), AudioSettingsPage::kInputDisabled);
        if (!inDeviceList_.isEmpty()) {
            deviceCombo_->addItem(tr("System Default"), AudioSettingsPage::kInputSystemDefault);
            for (int i = 0; i < inDeviceList_.size(); ++i)
                deviceCombo_->addItem(inDeviceList_.at(i), i);
        }
        const int idx = deviceCombo_->findData(inSelectedCode_);
        deviceCombo_->setCurrentIndex(std::max(0, idx));

        canvas_->setState(Tab::Inputs, config_.inputs, config_.activeInputBus, inChannels_);
        const bool canAdd = config_.inputs.size() < AudioIoConfig::kMaxBuses;
        addMonoBtn_->setEnabled(canAdd);
        addStereoBtn_->setEnabled(canAdd);
        removeBtn_->setEnabled(config_.inputs.size() > 1);
    } else {
        const bool running = !outDeviceName_.isEmpty();
        banner_->setDeviceTitle(running ? outDeviceName_ : tr("No Output Device"), running);
        for (int i = 0; i < outDeviceList_.size(); ++i)
            deviceCombo_->addItem(outDeviceList_.at(i), i);
        if (outDeviceList_.isEmpty())
            deviceCombo_->addItem(tr("System Default"), -1);
        const int idx = deviceCombo_->findData(outSelectedIdx_);
        deviceCombo_->setCurrentIndex(std::max(0, idx));

        canvas_->setState(Tab::Outputs, config_.outputs, config_.activeOutputBus, outChannels_);
        const bool canAdd = config_.outputs.size() < AudioIoConfig::kMaxBuses;
        addMonoBtn_->setEnabled(canAdd);
        addStereoBtn_->setEnabled(canAdd);
        removeBtn_->setEnabled(config_.outputs.size() > 1);
    }
}

void AudioIoDialog::commitConfigEdit(AudioIoConfig next)
{
    config_ = clampedAudioIoConfig(std::move(next));
    syncUiFromTab();
    emit configChanged(config_);
}

void AudioIoDialog::addBus(bool stereo)
{
    AudioIoConfig next = config_;
    auto& list = (currentTab_ == Tab::Inputs) ? next.inputs : next.outputs;
    if (list.size() >= AudioIoConfig::kMaxBuses)
        return;

    const int cols = std::max(2, canvas_->columnCount());
    const int n = static_cast<int>(list.size());
    const int chL = stereo ? ((n * 2) % std::max(2, cols)) : (n % cols);
    const int chR = stereo ? ((chL + 1 < cols) ? (chL + 1) : 0) : chL;

    AudioIoBus bus;
    bus.stereo = stereo;
    bus.leftChannel = chL;
    bus.rightChannel = chR;
    if (currentTab_ == Tab::Inputs) {
        bus.name
            = stereo ? tr("IN %1 + %2").arg(chL).arg(chR).toStdString() : tr("IN %1").arg(chL).toStdString();
    } else {
        bus.name = stereo ? tr("Sub %1").arg(list.size()).toStdString()
                          : tr("Mono Out %1").arg(list.size()).toStdString();
    }
    list.push_back(std::move(bus));
    commitConfigEdit(std::move(next));
}

void AudioIoDialog::removeSelectedBus()
{
    AudioIoConfig next = config_;
    auto& list = (currentTab_ == Tab::Inputs) ? next.inputs : next.outputs;
    if (list.size() <= 1)
        return;
    const int row = std::clamp(canvas_->selectedRow(), 0, static_cast<int>(list.size()) - 1);
    list.erase(list.begin() + row);
    int& activeIdx = (currentTab_ == Tab::Inputs) ? next.activeInputBus : next.activeOutputBus;
    if (activeIdx == row)
        activeIdx = std::max(0, row - 1);
    else if (activeIdx > row)
        --activeIdx;
    commitConfigEdit(std::move(next));
}

} // namespace vloomdaw

#include "AudioIoDialog.moc"
