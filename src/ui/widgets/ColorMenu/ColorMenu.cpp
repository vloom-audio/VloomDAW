// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/ColorMenu/ColorMenu.hpp"

#include "ui/Theme.hpp"

#include <QApplication>
#include <QColorDialog>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPolygonF>
#include <QScreen>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

#include <algorithm>
#include <array>

namespace vloomdaw {

namespace {

constexpr int kCols = 16;
constexpr int kRows = 6;
constexpr int kCellW = 20;
constexpr int kCellH = 17;
constexpr int kGap = 2;
constexpr int kPad = 8;
constexpr int kFooterH = 22;

/// 16 base hues around the spectrum (Studio One order: Red -> Magenta -> Blue -> Cyan -> Green -> Yellow ->
/// Orange).
constexpr std::array<int, kCols> kHues{{
    0,
    342,
    324,
    302,
    280,
    262,
    242,
    222,
    202,
    186,
    168,
    148,
    122,
    88,
    52,
    26,
}};

/// Saturation and lightness per row (top = bright pastel-vivid, middle = full saturation, bottom = deep).
constexpr std::array<std::pair<int, int>, kRows> kRowSL{{
    {235, 172},
    {240, 148},
    {230, 126},
    {210, 104},
    {188, 84},
    {165, 66},
}};

bool isResetCell(int row, int col) { return row == kRows - 1 && col == kCols - 1; }

std::uint32_t gridColor(int row, int col)
{
    const auto [sat, light] = kRowSL[static_cast<std::size_t>(row)];
    const QColor c = QColor::fromHsl(kHues[static_cast<std::size_t>(col)], sat, light);
    return toRgb(c);
}

QIcon swatchIcon(const QColor& color)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color.darker(160), 1));
    p.setBrush(color);
    p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
    return QIcon(pixmap);
}

class ColorSwatchGrid final : public QWidget {
public:
    ColorSwatchGrid(std::optional<std::uint32_t> current,
        std::function<void(std::optional<std::uint32_t>)> onPick, std::function<void()> onCustom,
        QWidget* parent = nullptr)
        : QWidget(parent)
        , current_(current)
        , onPick_(std::move(onPick))
        , onCustom_(std::move(onCustom))
    {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        const int w = kPad * 2 + kCols * kCellW + (kCols - 1) * kGap;
        const int h = kPad * 2 + kRows * kCellH + (kRows - 1) * kGap + 4 + kFooterH;
        setFixedSize(w, h);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& tc = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        for (int r = 0; r < kRows; ++r) {
            for (int col = 0; col < kCols; ++col) {
                const QRect rect = cellRect(r, col);
                const bool hovered = hoverRow_ == r && hoverCol_ == col;
                if (isResetCell(r, col)) {
                    // Studio One style diagonal split cell for Default / Reset colour.
                    p.setPen(QPen(tc.border, 1));
                    p.setBrush(tc.window);
                    p.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
                    QPolygonF tri;
                    tri << QPointF(rect.left() + 1, rect.bottom()) << QPointF(rect.right(), rect.top() + 1)
                        << QPointF(rect.right(), rect.bottom());
                    p.setPen(Qt::NoPen);
                    p.setBrush(tc.text);
                    p.drawPolygon(tri);
                    if (!current_ || hovered) {
                        p.setPen(QPen(tc.text, !current_ ? 1.8 : 1.2));
                        p.setBrush(Qt::NoBrush);
                        p.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
                    }
                    continue;
                }

                const std::uint32_t rgb = gridColor(r, col);
                const QColor fill = toQColor(rgb);
                p.fillRect(rect, fill);

                const bool active = current_.has_value() && (*current_ & 0xffffffu) == rgb;
                if (active) {
                    p.setPen(QPen(tc.window, 3.0));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(QRectF(rect).adjusted(1.0, 1.0, -1.0, -1.0));
                    p.setPen(QPen(Qt::white, 1.5));
                    p.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
                } else if (hovered) {
                    p.setPen(QPen(Qt::white, 1.4));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5));
                }
            }
        }

        // Footer button: "Custom Color…"
        const QRect foot = footerRect();
        p.setPen(QPen(hoverCustom_ ? tc.textDim : tc.border, 1));
        p.setBrush(hoverCustom_ ? tc.raised : tc.window);
        p.drawRoundedRect(QRectF(foot).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
        p.setPen(hoverCustom_ ? tc.text : tc.textDim);
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() > 0 ? f.pointSizeF() * 0.85 : 8.5);
        p.setFont(f);
        p.drawText(foot, Qt::AlignCenter, QCoreApplication::translate("ColorMenu", "Custom Color…"));
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QPoint pos = event->position().toPoint();
        int newR = -1;
        int newC = -1;
        for (int r = 0; r < kRows && newR < 0; ++r) {
            for (int col = 0; col < kCols; ++col) {
                if (cellRect(r, col).contains(pos)) {
                    newR = r;
                    newC = col;
                    break;
                }
            }
        }
        const bool newCustom = newR < 0 && footerRect().contains(pos);
        if (newR != hoverRow_ || newC != hoverCol_ || newCustom != hoverCustom_) {
            hoverRow_ = newR;
            hoverCol_ = newC;
            hoverCustom_ = newCustom;
            if (newR >= 0 && isResetCell(newR, newC))
                QToolTip::showText(event->globalPosition().toPoint(),
                    QCoreApplication::translate("ColorMenu", "Default color"), this);
            else
                QToolTip::hideText();
            update();
        }
    }

    void leaveEvent(QEvent* /*event*/) override
    {
        hoverRow_ = -1;
        hoverCol_ = -1;
        hoverCustom_ = false;
        update();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        const QPoint pos = event->position().toPoint();
        for (int r = 0; r < kRows; ++r) {
            for (int col = 0; col < kCols; ++col) {
                if (!cellRect(r, col).contains(pos))
                    continue;
                if (isResetCell(r, col))
                    onPick_(std::nullopt);
                else
                    onPick_(gridColor(r, col));
                return;
            }
        }
        if (footerRect().contains(pos) && onCustom_)
            onCustom_();
    }

private:
    [[nodiscard]] static QRect cellRect(int row, int col)
    {
        return {kPad + col * (kCellW + kGap), kPad + row * (kCellH + kGap), kCellW, kCellH};
    }

    [[nodiscard]] QRect footerRect() const
    {
        const int top = kPad + kRows * kCellH + (kRows - 1) * kGap + 5;
        return {kPad, top, width() - 2 * kPad, kFooterH - 2};
    }

    std::optional<std::uint32_t> current_;
    std::function<void(std::optional<std::uint32_t>)> onPick_;
    std::function<void()> onCustom_;
    int hoverRow_ = -1;
    int hoverCol_ = -1;
    bool hoverCustom_ = false;
};

class ColorPickerPopup final : public QWidget {
public:
    ColorPickerPopup(std::optional<std::uint32_t> current,
        std::function<void(std::optional<std::uint32_t>)> chosen, QWidget* dialogParent)
        : QWidget(dialogParent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(1, 1, 1, 1);
        layout->setSpacing(0);

        auto* grid = new ColorSwatchGrid(
            current,
            [this, chosen](std::optional<std::uint32_t> picked) {
                close();
                if (chosen)
                    chosen(picked);
            },
            [this, current, chosen, dialogParent] {
                close();
                QTimer::singleShot(0, dialogParent, [current, chosen, dialogParent] {
                    const QColor initial = current ? toQColor(*current) : QColor(Qt::white);
                    const QColor picked = QColorDialog::getColor(
                        initial, dialogParent, QCoreApplication::translate("ColorMenu", "Choose a Color"));
                    if (picked.isValid() && chosen)
                        chosen(toRgb(picked));
                });
            },
            this);
        layout->addWidget(grid);
        adjustSize();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& c = Theme::colors();
        QPainter p(this);
        p.fillRect(rect(), c.panel);
        p.setPen(QPen(c.border, 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
};

} // namespace

QColor toQColor(std::uint32_t rgb) { return QColor::fromRgb(static_cast<QRgb>(rgb & 0xffffffu)); }

std::uint32_t toRgb(const QColor& color) { return static_cast<std::uint32_t>(color.rgb()) & 0xffffffu; }

void addColorMenu(QMenu& menu, std::optional<std::uint32_t> current,
    std::function<void(std::optional<std::uint32_t>)> chosen)
{
    QMenu* sub = menu.addMenu(QCoreApplication::translate("ColorMenu", "Color"));
    if (current)
        sub->menuAction()->setIcon(swatchIcon(toQColor(*current)));

    QWidget* dialogParent
        = menu.parentWidget() != nullptr ? menu.parentWidget() : QApplication::activeWindow();
    const QPointer<QMenu> menuPtr(&menu);
    const QPointer<QMenu> subPtr(sub);
    auto* widgetAction = new QWidgetAction(sub);
    auto* grid = new ColorSwatchGrid(
        current,
        [menuPtr, subPtr, chosen](std::optional<std::uint32_t> picked) {
            if (subPtr != nullptr)
                subPtr->close();
            if (menuPtr != nullptr)
                menuPtr->close();
            if (chosen)
                chosen(picked);
        },
        [menuPtr, subPtr, current, chosen, dialogParent] {
            if (subPtr != nullptr)
                subPtr->close();
            if (menuPtr != nullptr)
                menuPtr->close();
            QTimer::singleShot(0, dialogParent, [current, chosen, dialogParent] {
                const QColor initial = current ? toQColor(*current) : QColor(Qt::white);
                const QColor picked = QColorDialog::getColor(
                    initial, dialogParent, QCoreApplication::translate("ColorMenu", "Choose a Color"));
                if (picked.isValid() && chosen)
                    chosen(toRgb(picked));
            });
        },
        sub);
    widgetAction->setDefaultWidget(grid);
    sub->addAction(widgetAction);
}

void showColorPicker(QWidget* parent, const QPoint& globalPos, std::optional<std::uint32_t> current,
    std::function<void(std::optional<std::uint32_t>)> chosen)
{
    QWidget* dialogParent = parent != nullptr ? parent->window() : QApplication::activeWindow();
    auto* popup = new ColorPickerPopup(current, std::move(chosen), dialogParent);
    QPoint pos = globalPos;
    if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
        const QRect avail = screen->availableGeometry();
        const int maxX = std::max(avail.left() + 4, avail.right() - popup->width() - 4);
        const int maxY = std::max(avail.top() + 4, avail.bottom() - popup->height() - 4);
        pos.setX(std::clamp(pos.x(), avail.left() + 4, maxX));
        pos.setY(std::clamp(pos.y(), avail.top() + 4, maxY));
    }
    popup->move(pos);
    popup->show();
}

} // namespace vloomdaw
