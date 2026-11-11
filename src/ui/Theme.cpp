// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/Theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

#include <utility>

namespace vloomdaw {

namespace {

/// `color` with an alpha, as "#AARRGGBB", for tinted backgrounds.
QString tint(const QColor& color, int alpha)
{
    QColor c = color;
    c.setAlpha(alpha);
    return c.name(QColor::HexArgb);
}

QString styleSheet(const Theme::Colors& c)
{
    // Selectors use object names set by the widgets; keep them in sync.
    // "@name" tokens are replaced with palette colours below.
    QString qss = QStringLiteral(R"(
QToolBar {
    background: @panel;
    border: none;
    border-bottom: 1px solid @border;
    padding: 7px 10px;
    spacing: 0;
}

/* Rounded groups of buttons on the tool bar. */
QFrame#toolGroup {
    background: @window;
    border: 1px solid @border;
    border-radius: 8px;
}
QFrame#toolGroup QToolButton {
    border: none;
    border-radius: 6px;
    padding: 5px;
    min-width: 22px;
}
QFrame#toolGroup QToolButton:hover { background: @raised; }
QFrame#toolGroup QToolButton:pressed { background: @border; }
QFrame#toolGroup QToolButton:checked { background: @accentTint; }
QFrame#toolGroup QToolButton#playButton:checked { background: @playingTint; }
QFrame#toolGroup QToolButton#tapButton {
    color: @textDim;
    font-weight: 700;
    padding: 5px 9px;
}
QFrame#toolGroup QToolButton#tapButton:pressed { color: @accent; }

QLineEdit#lcdEditor {
    background: @display;
    color: @displayText;
    border: 1px solid @accent;
    border-radius: 4px;
    padding: 0 4px;
    selection-background-color: @accentTint;
    selection-color: @displayText;
}

QComboBox {
    background: @base;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 5px 10px;
}
QComboBox:hover { border-color: @textDim; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url(:/icons/chevron-down.svg); width: 12px; height: 12px; }
QComboBox QAbstractItemView {
    background: @panel;
    border: 1px solid @border;
    selection-background-color: @accentTint;
    selection-color: @text;
    outline: none;
}

QPushButton {
    background: @raised;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 6px 16px;
}
QPushButton:hover { border-color: @textDim; }
QPushButton:pressed { background: @border; }

QMenu {
    background: @panel;
    border: 1px solid @border;
    padding: 4px;
}
QMenu::item { padding: 5px 20px; border-radius: 4px; }
QMenu::item:selected { background: @accentTint; color: @text; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }

QToolTip {
    background: @panel;
    color: @text;
    border: 1px solid @border;
    padding: 4px 8px;
}

QLabel#dialogHint { color: @textDim; }

/* Activity bar (icon strip) and side panels. */
QWidget#activityBar { background: @panel; border-right: 1px solid @border; }
QWidget#activityBar QToolButton { border: none; border-radius: 8px; }
QWidget#activityBar QToolButton:hover { background: @raised; }
QWidget#activityBar QToolButton:checked { background: @accentTint; }

QWidget#browserPanel { background: @panel; }
QLabel#panelTitle { color: @textDim; font-size: 9pt; font-weight: 700; letter-spacing: 1px; }
QLabel#panelHint { color: @textDim; }
QWidget#browserPanel QToolButton { border: none; border-radius: 6px; padding: 5px; }
QWidget#browserPanel QToolButton:hover { background: @raised; }
QLineEdit#browserSearch {
    background: @base;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 5px 8px;
}
QLineEdit#browserSearch:focus { border-color: @accent; }
QTreeView#browserTree { background: transparent; border: none; outline: none; }
QTreeView#browserTree::item { padding: 3px 2px; border-radius: 4px; }
QTreeView#browserTree::item:hover { background: @raised; }
QTreeView#browserTree::item:selected { background: @accentTint; color: @text; }
QSplitter#sidebarSplitter::handle { background: @border; }

/* Settings window: category list on the left. */
QListWidget#settingsNav {
    background: @panel;
    border: none;
    border-right: 1px solid @border;
    padding: 12px 8px;
    outline: none;
}
QListWidget#settingsNav::item { padding: 7px 10px; border-radius: 6px; margin: 1px 0; }
QListWidget#settingsNav::item:hover { background: @raised; }
QListWidget#settingsNav::item:selected { background: @accentTint; color: @text; }
QLabel#settingsHeading { font-size: 16pt; font-weight: 600; }

QWidget#emptyState QLabel#emptyTitle { font-size: 22pt; font-weight: 600; }
QWidget#emptyState QLabel#emptyHint { color: @textDim; }

QStatusBar { background: @panel; border-top: 1px solid @border; color: @textDim; }
QStatusBar::item { border: none; }
)");

    // Longest names first so "@displayText" is not eaten by "@display".
    const std::pair<const char*, QString> tokens[] = {
        {"@displayText", c.displayText.name()},
        {"@playingTint", tint(c.playing, 0x40)},
        {"@accentTint", tint(c.accent, 0x40)},
        {"@display", c.display.name()},
        {"@textDim", c.textDim.name()},
        {"@window", c.window.name()},
        {"@border", c.border.name()},
        {"@raised", c.raised.name()},
        {"@accent", c.accent.name()},
        {"@panel", c.panel.name()},
        {"@base", c.base.name()},
        {"@text", c.text.name()},
    };
    for (const auto& [token, value] : tokens)
        qss.replace(QLatin1String(token), value);
    return qss;
}

} // namespace

const Theme::Colors& Theme::colors()
{
    static const Colors kDark{
        .window = QColor(0x21, 0x21, 0x21),
        .base = QColor(0x18, 0x18, 0x18),
        .panel = QColor(0x2a, 0x2a, 0x2a),
        .raised = QColor(0x33, 0x33, 0x33),
        .border = QColor(0x3a, 0x3a, 0x3a),
        .text = QColor(0xe6, 0xe9, 0xee),
        .textDim = QColor(0x8b, 0x95, 0xa3),
        .accent = QColor(0xff, 0x8a, 0x3d),
        .playing = QColor(0x4a, 0xde, 0x80),
        .display = QColor(0x14, 0x14, 0x14),
        .displayText = QColor(0x5e, 0xea, 0xd4),
        .displayUnlit = QColor(0x5e, 0xea, 0xd4, 0x1c),
    };
    return kDark;
}

void Theme::apply(QApplication& app)
{
    const Colors& c = colors();

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Ask the OS for dark window chrome too: the Windows title bar, macOS menus
    // and system dialogs then match the app instead of following the system.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif

    // Fusion renders the same on every OS and honours the palette fully.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.panel);
    p.setColor(QPalette::ToolTipBase, c.panel);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.textDim);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.panel);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.base);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.base);
    p.setColor(QPalette::Light, c.raised);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.textDim);
    QApplication::setPalette(p);

    app.setStyleSheet(styleSheet(c));
}

} // namespace vloomdaw
