// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/widgets/ColorMenu/ColorMenu.hpp"

#include <QAction>
#include <QColorDialog>
#include <QCoreApplication>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>

#include <array>

namespace vloomdaw {

namespace {

struct Preset {
    const char* name;
    std::uint32_t rgb;
};

// Hues that read well on the dark themes, with names for the menu.
constexpr std::array<Preset, 12> kPresets{{
    {QT_TRANSLATE_NOOP("ColorMenu", "Red"), 0xef4444},
    {QT_TRANSLATE_NOOP("ColorMenu", "Orange"), 0xff8a3d},
    {QT_TRANSLATE_NOOP("ColorMenu", "Yellow"), 0xfacc15},
    {QT_TRANSLATE_NOOP("ColorMenu", "Lime"), 0xa3e635},
    {QT_TRANSLATE_NOOP("ColorMenu", "Green"), 0x4ade80},
    {QT_TRANSLATE_NOOP("ColorMenu", "Teal"), 0x5eead4},
    {QT_TRANSLATE_NOOP("ColorMenu", "Sky"), 0x38bdf8},
    {QT_TRANSLATE_NOOP("ColorMenu", "Blue"), 0x60a5fa},
    {QT_TRANSLATE_NOOP("ColorMenu", "Violet"), 0xa78bfa},
    {QT_TRANSLATE_NOOP("ColorMenu", "Pink"), 0xf472b6},
    {QT_TRANSLATE_NOOP("ColorMenu", "Brown"), 0xb08968},
    {QT_TRANSLATE_NOOP("ColorMenu", "Grey"), 0x9ca3af},
}};

QIcon swatch(const QColor& color)
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

} // namespace

QColor toQColor(std::uint32_t rgb) { return QColor::fromRgb(static_cast<QRgb>(rgb & 0xffffffu)); }

std::uint32_t toRgb(const QColor& color) { return static_cast<std::uint32_t>(color.rgb()) & 0xffffffu; }

void addColorMenu(QMenu& menu, std::optional<std::uint32_t> current,
    std::function<void(std::optional<std::uint32_t>)> chosen)
{
    QMenu* sub = menu.addMenu(QCoreApplication::translate("ColorMenu", "Color"));
    if (current)
        sub->menuAction()->setIcon(swatch(toQColor(*current)));
    bool matched = false;
    for (const Preset& preset : kPresets) {
        QAction* action = sub->addAction(swatch(toQColor(preset.rgb)),
            QCoreApplication::translate("ColorMenu", preset.name), &menu,
            [chosen, rgb = preset.rgb] { chosen(rgb); });
        action->setCheckable(true);
        action->setChecked(current == preset.rgb);
        matched = matched || current == preset.rgb;
    }
    sub->addSeparator();
    QAction* custom = sub->addAction(QCoreApplication::translate("ColorMenu", "Custom…"), &menu,
        [chosen, current, parent = menu.parentWidget()] {
            const QColor initial = current ? toQColor(*current) : QColor(Qt::white);
            const QColor picked = QColorDialog::getColor(
                initial, parent, QCoreApplication::translate("ColorMenu", "Choose a Color"));
            if (picked.isValid())
                chosen(toRgb(picked));
        });
    custom->setCheckable(true);
    custom->setChecked(current && !matched);
    if (current && !matched)
        custom->setIcon(swatch(toQColor(*current)));
    QAction* none = sub->addAction(
        QCoreApplication::translate("ColorMenu", "Default"), &menu, [chosen] { chosen(std::nullopt); });
    none->setCheckable(true);
    none->setChecked(!current);
}

} // namespace vloomdaw
