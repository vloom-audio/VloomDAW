// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/Theme.hpp"

#include "core/ThemePresets.hpp"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QIcon>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QSvgRenderer>

#include <iterator>
#include <utility>

namespace vloomdaw {

namespace {

/// Fusion with one change: no mnemonic underlines in menus ("&File" reads
/// "File"). Alt+letter shortcuts still work.
class AppStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
        QStyleHintReturn* returnData) const override
    {
        if (hint == SH_UnderlineShortcut)
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    // The "more" button a too-narrow tool bar shows: big enough to find and
    // hit, with an icon that shows on the dark theme.
    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
    {
        if (metric == PM_ToolBarExtensionExtent)
            return 26;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    QIcon standardIcon(StandardPixmap icon, const QStyleOption* option, const QWidget* widget) const override
    {
        if (icon == SP_ToolBarHorizontalExtensionButton)
            return QIcon(QStringLiteral(":/icons/chevrons-right.svg"));
        return QProxyStyle::standardIcon(icon, option, widget);
    }
};

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
/* "More" button a tool bar shows when it is too narrow for everything. */
QToolBarExtension { border: none; border-radius: 6px; padding: 4px; }
QToolBarExtension:hover { background: @raised; }
/* The transport's own » and the drop-down with the groups that do not fit. */
QToolButton#transportMore { border: none; border-radius: 6px; padding: 4px; }
QToolButton#transportMore:hover { background: @raised; }
QFrame#transportOverflow { background: @panel; border: 1px solid @border; border-radius: 8px; }

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
QFrame#toolGroup QToolButton#recordButton:checked { background: @dangerTint; }
QFrame#toolGroup QToolButton#tapButton {
    color: @textDim;
    font-weight: 700;
    padding: 5px 9px;
}
QFrame#toolGroup QToolButton#tapButton:pressed { color: @accent; }
QFrame#toolGroup QToolButton#modeButton {
    color: @textDim;
    font-weight: 700;
    padding: 5px 4px;
    min-width: 40px;
}
QFrame#toolGroup QToolButton#modeButton:checked { background: @accentTint; color: @accent; }

/* Pattern selector on the transport bar. */
QFrame#toolGroup QToolButton#patternButton { padding: 5px 24px 5px 9px; text-align: left; }
QFrame#toolGroup QToolButton#patternButton::menu-indicator {
    image: url(:/icons/chevron-down.svg);
    width: 10px;
    height: 10px;
    subcontrol-origin: padding;
    subcontrol-position: right center;
    right: 9px;
}

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

/* Menu bar: quiet text items that light up on hover, like tool bar buttons. */
QMenuBar {
    background: @window;
    border: none;
    padding: 0 4px;
    spacing: 2px;
}
QMenuBar::item {
    background: transparent;
    color: @textDim;
    padding: 4px 10px;
    margin: 4px 0;
    border-radius: 6px;
}
QMenuBar::item:selected { background: @raised; color: @text; }
QMenuBar::item:pressed { background: @accentTint; color: @text; }
QLabel#appName { color: @accent; font-weight: 700; letter-spacing: 1px; padding: 0 8px 0 10px; }

QMenu {
    background: @panel;
    border: 1px solid @border;
    padding: 4px;
}
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 4px; }
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
/* Darker than the workspace so the sidebar reads as its own zone: the rail
   darkest, the panel one step lighter. Inputs sit a step above the panel. */
QWidget#activityBar { background: @rail; border-right: 1px solid @border; }
QWidget#activityBar QToolButton { border: none; border-radius: 8px; }
QWidget#activityBar QToolButton:hover { background: @raised; }
QWidget#activityBar QToolButton:checked { background: @accentTint; }
QLabel#activityHint {
    background: @panel;
    color: @text;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 5px 10px;
}

QWidget#browserPanel, QWidget#pluginsPanel { background: @sidebar; }
QWidget#browserPanel QComboBox { background: @window; }
QLabel#panelTitle { color: @textDim; font-size: 9pt; font-weight: 700; letter-spacing: 1px; }
QLabel#panelAppName { color: @accent; font-size: 9pt; font-weight: 700; letter-spacing: 1px; }
QLabel#panelHint { color: @textDim; }
QWidget#browserPanel QToolButton, QWidget#pluginsPanel QToolButton { border: none; border-radius: 6px; padding: 5px; }
QWidget#browserPanel QToolButton:hover, QWidget#pluginsPanel QToolButton:hover { background: @raised; }
QPushButton#accentButton { background: @accent; border-color: @accent; color: @window; font-weight: 600; }
QPushButton#accentButton:hover { border-color: @text; }
QPushButton#accentButton:disabled { background: @raised; border-color: @border; color: @textDim; }
QDialog#pluginManager QTreeWidget { background: @panel; border: 1px solid @border; border-radius: 6px; outline: none; }
QDialog#pluginManager QTreeWidget::item { padding: 4px 2px; }
QDialog#pluginManager QTreeWidget::item:selected { background: @accentTint; color: @text; }
QDialog#pluginManager QHeaderView::section {
    background: @panel; color: @textDim; border: none; border-bottom: 1px solid @border; padding: 4px 6px;
}
QDialog#pluginManager QProgressBar { background: @raised; border: none; border-radius: 2px; }
QDialog#pluginManager QProgressBar::chunk { background: @accent; border-radius: 2px; }
QLineEdit#browserSearch {
    background: @window;
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
QSplitter#workspaceSplitter::handle { background: @border; }

/* Workspace editors: channel rack, piano roll, playlist. */
QWidget#panelFrame { background: @window; }
QWidget#floatingPanel { background: @window; }
QWidget#panelHeader { background: @panel; border-bottom: 1px solid @border; }
QLabel#panelContext { color: @textDim; }
QToolButton#panelButton {
    background: @window;
    border: 1px solid @border;
    border-radius: 6px;
    padding: 3px 10px 3px 6px;
}
QToolButton#panelButton:hover { border-color: @textDim; }
/* The browser's preview strip under its list (waveform, play / stop). */
QWidget#sampleWaveform { background: @panel; border: 1px solid @border; border-radius: 6px; }
QLabel#sampleWaveformName { font-weight: 600; }
QToolButton#panelButton[iconOnly="true"] { padding: 3px 7px; }
/* Panel header buttons side by side in one box (the playlist's tools, its
   "+ Track" and "−"): the box has a panelButton's border, so it is as tall. */
QFrame#panelButtonGroup {
    background: @window;
    border: 1px solid @border;
    border-radius: 6px;
}
QFrame#panelButtonGroup QToolButton#panelSegment {
    background: transparent;
    border: none;
    border-radius: 5px;
    padding: 3px 10px 3px 6px;
}
QFrame#panelButtonGroup QToolButton#panelSegment[iconOnly="true"] { padding: 3px 7px; }
QFrame#panelButtonGroup QToolButton#panelSegment:hover { background: @raised; }
QFrame#panelButtonGroup QToolButton#panelSegment:pressed { background: @border; }
QFrame#panelButtonGroup QToolButton#panelSegment:checked { background: @accentTint; }
QScrollArea#channelRackScroll, QWidget#channelRackView { background: @window; }

/* Mixer panel (F9): the master strip has an accent outline; strips silenced
   by another insert's solo are dimmed. */
QWidget#mixerPanel { background: @rail; }
QScrollArea#mixerScroll, QWidget#mixerInserts { background: transparent; }
QFrame#mixerStrip { background: @panel; border: 1px solid @border; border-radius: 6px; }
QFrame#mixerStrip[master="true"] { border-color: @accent; }
QFrame#mixerStrip[silenced="true"] { background: @sidebar; }
QFrame#mixerStrip[silenced="true"] QLabel { color: @border; }
QLabel#stripName { font-size: 8pt; font-weight: 600; }
QLabel#stripDb, QLabel#stripNumber { color: @textDim; font-size: 8pt; }
QLabel#stripNumber { font-weight: 700; }
QToolButton#muteButton, QToolButton#soloButton {
    background: @window;
    border: 1px solid @border;
    border-radius: 4px;
    color: @textDim;
    font-size: 8pt;
    font-weight: 700;
    min-width: 20px;
    padding: 1px 0;
}
QToolButton#muteButton:hover, QToolButton#soloButton:hover { border-color: @textDim; }
QToolButton#muteButton:checked { background: @accentTint; color: @accent; border-color: @accent; }
QToolButton#soloButton:checked { background: @playingTint; color: @playing; border-color: @playing; }
/* A strip's FX button (MixerStrip::setEffectCount): accented while the track has effects. */
QToolButton#stripEffects {
    background: @window;
    border: 1px solid @border;
    border-radius: 4px;
    color: @textDim;
    font-size: 8pt;
    font-weight: 700;
    padding: 1px 0;
}
QToolButton#stripEffects:hover { border-color: @textDim; }
QToolButton#stripEffects[active="true"] { background: @accentTint; color: @accent; border-color: @accent; }
QFrame#mixerStrip[silenced="true"] QToolButton#stripEffects[active="true"] { color: @textDim; }
QToolButton#panelClose { border: none; border-radius: 6px; padding: 4px; }
QToolButton#panelClose:hover { background: @raised; }

/* Settings window: categories with icons on the left, pages of cards. */
QWidget#settingsDialog { background: @window; }
QWidget#settingsNavBox { background: @sidebar; border-right: 1px solid @border; }
QListWidget#settingsNav { background: transparent; border: none; outline: none; }
QListWidget#settingsNav::item { padding: 0 10px; border-radius: 8px; margin: 2px 0; color: @textDim; }
QListWidget#settingsNav::item:hover { background: @raised; color: @text; }
QListWidget#settingsNav::item:selected { background: @accentTint; color: @text; }
QScrollArea#settingsScroll, QWidget#settingsPage { background: transparent; }
QLabel#settingsHeading { font-size: 18pt; font-weight: 600; }
QLabel#settingsSubtitle { color: @textDim; }
QFrame#settingsCard { background: @panel; border: 1px solid @border; border-radius: 10px; }
QLabel#settingsCardTitle { color: @textDim; font-size: 8pt; font-weight: 700; letter-spacing: 1px; }
QLabel#settingsRowName { font-weight: 600; }
QLabel#settingsRowDescription { color: @textDim; font-size: 8.5pt; }
QFrame#settingsSeparator { background: @border; border: none; }
QLabel#statusPill { border-radius: 10px; padding: 4px 12px; font-weight: 600; }
QLabel#statusPill[running="true"] { background: @playingTint; color: @playing; }
QLabel#statusPill[running="false"] { background: @dangerTint; color: @text; }
QLabel#themeWarning { background: @dangerTint; color: @text; border-radius: 6px; padding: 6px 10px; }

QWidget#emptyState QLabel#emptyTitle { font-size: 22pt; font-weight: 600; }
QWidget#emptyState QLabel#emptyHint { color: @textDim; }

/* Window buttons in the menu bar row (Windows, no native title bar). */
QWidget#windowButtons QToolButton { border: none; border-radius: 0; padding: 0; }
QWidget#windowButtons QToolButton:hover { background: @raised; }
QWidget#windowButtons QToolButton:pressed { background: @border; }
QWidget#windowButtons QToolButton#closeButton:hover,
QWidget#windowButtons QToolButton#closeButton:pressed { background: @danger; }

QStatusBar { background: @panel; border-top: 1px solid @border; color: @textDim; }
QStatusBar::item { border: none; }
QToolButton#deviceStatus { border: none; border-radius: 4px; color: @textDim; padding: 1px 8px; }
QToolButton#deviceStatus:hover { background: @raised; color: @text; }
QToolButton#deviceStatus::menu-indicator { image: none; width: 0; }
)");

    // Longest names first so "@displayText" is not eaten by "@display".
    const std::pair<const char*, QString> tokens[] = {
        {"@displayText", c.displayText.name()},
        {"@playingTint", tint(c.playing, 0x40)},
        {"@dangerTint", tint(c.danger, 0x50)},
        {"@accentTint", tint(c.accent, 0x40)},
        {"@display", c.display.name()},
        {"@danger", c.danger.name()},
        {"@sidebar", c.sidebar.name()},
        {"@rail", c.rail.name()},
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

namespace {

QColor toQColor(Rgb c) { return {c.r, c.g, c.b}; }

Theme::Colors colorsFrom(const ResolvedTheme& t)
{
    QColor unlit = toQColor(t.displayText);
    unlit.setAlpha(0x1c);
    return {
        .window = toQColor(t.window),
        .base = toQColor(t.base),
        .panel = toQColor(t.panel),
        .rail = toQColor(t.rail),
        .sidebar = toQColor(t.sidebar),
        .raised = toQColor(t.raised),
        .border = toQColor(t.border),
        .text = toQColor(t.text),
        .textDim = toQColor(t.textDim),
        .accent = toQColor(t.accent),
        .playing = toQColor(t.playing),
        .danger = toQColor(t.record),
        .display = toQColor(t.display),
        .displayText = toQColor(t.displayText),
        .displayUnlit = unlit,
    };
}

/// The colours in use; replaced by Theme::select().
Theme::Colors& current()
{
    static Theme::Colors colors = colorsFrom(resolveTheme(ThemeChoice{}));
    return colors;
}

} // namespace

const Theme::Colors& Theme::colors() { return current(); }

namespace {
unsigned& themeGeneration()
{
    static unsigned generation = 0;
    return generation;
}
} // namespace

void Theme::select(const ThemeChoice& choice)
{
    current() = colorsFrom(resolveTheme(choice));
    ++themeGeneration();
}

unsigned Theme::generation() { return themeGeneration(); }

// ---------------------------------------------------------------- themed icons

namespace {

/// Renders an SVG with the authoring colours replaced by the theme's, and
/// caches the pixmaps until the theme changes.
class ThemedIconEngine : public QIconEngine {
public:
    explicit ThemedIconEngine(QString path)
        : path_(std::move(path))
    {
        QFile file(path_);
        if (file.open(QIODevice::ReadOnly))
            source_ = file.readAll();
    }

    ThemedIconEngine* clone() const override { return new ThemedIconEngine(path_); }
    QString key() const override { return QStringLiteral("vloomdaw.themed"); }

    QSize actualSize(const QSize& size, QIcon::Mode /*mode*/, QIcon::State /*state*/) override
    {
        return size;
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State /*state*/, qreal scale) override
    {
        if (cachedGeneration_ != Theme::generation()) {
            cache_.clear();
            cachedGeneration_ = Theme::generation();
        }
        const QString cacheKey
            = QStringLiteral("%1x%2@%3/%4").arg(size.width()).arg(size.height()).arg(scale).arg(mode);
        if (const auto it = cache_.constFind(cacheKey); it != cache_.constEnd())
            return *it;

        const QSize pixels = (QSizeF(size) * scale).toSize();
        QPixmap pixmap(pixels);
        pixmap.fill(Qt::transparent);
        if (!pixels.isEmpty()) {
            QSvgRenderer renderer(recoloured());
            QPainter painter(&pixmap);
            if (mode == QIcon::Disabled)
                painter.setOpacity(0.35);
            renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(pixels)));
        }
        pixmap.setDevicePixelRatio(scale);
        cache_.insert(cacheKey, pixmap);
        return pixmap;
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal scale = painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }

private:
    QByteArray recoloured() const
    {
        const Theme::Colors& c = Theme::colors();
        QByteArray svg = source_;
        const std::pair<const char*, QColor> swaps[]
            = {{"#e6e9ee", c.text}, {"#8b95a3", c.textDim}, {"#ff8a3d", c.accent}, {"#e81123", c.danger}};
        for (const auto& [from, to] : swaps)
            svg.replace(from, to.name().toLatin1());
        return svg;
    }

    QString path_;
    QByteArray source_;
    QHash<QString, QPixmap> cache_;
    unsigned cachedGeneration_ = 0;
};

} // namespace

QIcon Theme::icon(const QString& name)
{
    return QIcon(new ThemedIconEngine(QStringLiteral(":/icons/%1.svg").arg(name)));
}

QColor Theme::patternColor(std::size_t index)
{
    static const QColor kHues[] = {
        QColor(0xff, 0x8a, 0x3d), // accent orange
        QColor(0x5e, 0xea, 0xd4), // teal
        QColor(0xa7, 0x8b, 0xfa), // violet
        QColor(0x4a, 0xde, 0x80), // green
        QColor(0xf4, 0x72, 0xb6), // pink
        QColor(0x60, 0xa5, 0xfa), // blue
        QColor(0xfa, 0xcc, 0x15), // yellow
    };
    return kHues[index % std::size(kHues)];
}

void Theme::apply(QApplication& app)
{
    const Colors& c = colors();

    // The interface font, bundled (resources/fonts): Noto Sans Thai, with
    // Noto Sans for Latin text, at the system's default size. Loaded once;
    // apply() runs again on theme changes.
    static const bool fontLoaded = [] {
        bool any = false;
        for (const char* family : {"NotoSans", "NotoSansThai"})
            for (const char* weight : {"Regular", "SemiBold", "Bold"})
                any |= QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/%1-%2.ttf")
                               .arg(QLatin1String(family), QLatin1String(weight)))
                    != -1;
        if (any) {
            QFont font = QApplication::font();
            // Noto Sans Thai first, for every widget's metrics and line height;
            // it has no Latin letters or digits, which come from Noto Sans.
            font.setFamilies({QStringLiteral("Noto Sans Thai"), QStringLiteral("Noto Sans")});
            QApplication::setFont(font);
        }
        return any;
    }();
    (void)fontLoaded;

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Ask the OS for dark window chrome too: the Windows title bar, macOS menus
    // and system dialogs then match the app instead of following the system.
    // Only when it differs: setting it sends a theme-change event to every widget.
    if (QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Dark)
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif

    // Fusion renders the same on every OS and honours the palette fully.
    // Set once: apply() runs again whenever the user picks another theme.
    if (dynamic_cast<AppStyle*>(QApplication::style()) == nullptr)
        QApplication::setStyle(new AppStyle(QStyleFactory::create(QStringLiteral("Fusion"))));

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

    // Restyling every widget is the slow part (about 150 ms here): skip it
    // when the style sheet did not change (e.g. only the LCD colour did).
    const QString qss = styleSheet(c);
    if (app.styleSheet() != qss)
        app.setStyleSheet(qss);
}

} // namespace vloomdaw
