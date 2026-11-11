// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/SettingsDialog/ThemeSettingsPage.hpp"

#include "ui/Theme.hpp"
#include "ui/dialogs/SettingsDialog/SettingsCard.hpp"
#include "ui/widgets/Knob/Knob.hpp"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QColorDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace vloomdaw {

namespace {

QColor qcolor(Rgb c) { return {c.r, c.g, c.b}; }
Rgb rgb(const QColor& c)
{
    return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
        static_cast<std::uint8_t>(c.blue())};
}
QString qstring(std::string_view s) { return QString::fromLatin1(s.data(), static_cast<qsizetype>(s.size())); }

/// A colour role of the Colours card.
struct Role {
    const char* label;
    std::optional<Rgb> ThemeOverrides::*member;
    Rgb ResolvedTheme::*resolved;
};
const std::array<Role, 5> kRoles{{
    {QT_TRANSLATE_NOOP("vloomdaw::ThemeSettingsPage", "Selection"), &ThemeOverrides::selection,
        &ResolvedTheme::accent},
    {QT_TRANSLATE_NOOP("vloomdaw::ThemeSettingsPage", "Playing"), &ThemeOverrides::playing, &ResolvedTheme::playing},
    {QT_TRANSLATE_NOOP("vloomdaw::ThemeSettingsPage", "Record"), &ThemeOverrides::record, &ResolvedTheme::record},
    {QT_TRANSLATE_NOOP("vloomdaw::ThemeSettingsPage", "LCD"), &ThemeOverrides::lcd, &ResolvedTheme::displayText},
    {QT_TRANSLATE_NOOP("vloomdaw::ThemeSettingsPage", "Text"), &ThemeOverrides::text, &ResolvedTheme::text},
}};

/// Base for the clickable tiles and swatches: repaints on hover.
class HoverButton : public QAbstractButton {
public:
    using QAbstractButton::QAbstractButton;

protected:
    void enterEvent(QEnterEvent* event) override
    {
        update();
        QAbstractButton::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override
    {
        update();
        QAbstractButton::leaveEvent(event);
    }
};

/// A miniature of the app in one preset: tool bar with LCDs, side bar,
/// channel rack steps, mixer meters and a playhead, so presets can be told
/// apart at a glance.
class PresetTile : public HoverButton {
public:
    PresetTile(const PaletteSpec& palette, QWidget* parent)
        : HoverButton(parent)
        , palette_(palette)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(qstring(palette.name));
        setAccessibleName(QStringLiteral("%1 preset").arg(qstring(palette.name)));
        setFixedSize(160, 118);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& ui = Theme::colors();
        const PaletteSpec& pal = palette_;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF frame = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -22.5);
        QPainterPath clip;
        clip.addRoundedRect(frame, 6, 6);
        p.save();
        p.setClipPath(clip);
        const double l = frame.left();
        const double t = frame.top();
        const double w = frame.width();
        const double h = frame.height();
        p.fillRect(frame, qcolor(pal.window));
        // Tool bar with two LCDs and a lit button.
        p.fillRect(QRectF(l, t, w, 14), qcolor(pal.panel));
        for (double x : {w * 0.34, w * 0.52}) {
            p.fillRect(QRectF(l + x, t + 3, w * 0.15, 8), qcolor(pal.display));
            p.fillRect(QRectF(l + x + 3, t + 6, w * 0.15 - 6, 2), qcolor(pal.displayText));
        }
        p.fillRect(QRectF(l + 4, t + 4, 6, 6), ui.accent);
        // Side bar.
        p.fillRect(QRectF(l, t + 14, w * 0.06, h - 14), qcolor(pal.rail));
        p.fillRect(QRectF(l + w * 0.06, t + 14, w * 0.22, h - 14), qcolor(pal.sidebar));
        for (int i = 0; i < 4; ++i)
            p.fillRect(QRectF(l + w * 0.09, t + 20 + i * 8, w * 0.14, 3), qcolor(pal.textDim));
        // Channel rack: four rows of steps, some lit.
        const double rackX = l + w * 0.32;
        for (int row = 0; row < 4; ++row)
            for (int s = 0; s < 8; ++s) {
                const bool on = (row + s) % 3 == 0;
                p.fillRect(QRectF(rackX + s * (w * 0.052), t + 20 + row * 9, w * 0.042, 6),
                    on ? ui.accent : qcolor(pal.raised));
            }
        // Mixer with meters.
        const double mixY = t + h * 0.68;
        p.fillRect(QRectF(rackX, mixY, w * 0.64, h - (mixY - t) - 4), qcolor(pal.panel));
        for (int m = 0; m < 9; ++m) {
            const double level = 0.25 + 0.7 * ((m * 37) % 10) / 10.0;
            const double meterH = (h - (mixY - t) - 10) * level;
            p.fillRect(QRectF(rackX + 4 + m * (w * 0.068), t + h - 7 - meterH, w * 0.03, meterH),
                qcolor(pal.displayText));
        }
        // Playhead.
        p.fillRect(QRectF(rackX + w * 0.27, t + 16, 1.5, h * 0.5), ui.playing);
        p.restore();

        p.setPen(QPen(isChecked() ? ui.accent : (underMouse() ? ui.textDim : ui.border), isChecked() ? 2.0 : 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(frame, 6, 6);

        p.setPen(isChecked() ? ui.text : ui.textDim);
        QFont f = font();
        f.setBold(isChecked());
        p.setFont(f);
        p.drawText(QRectF(0, height() - 20, width(), 18), Qt::AlignCenter, qstring(pal.name));
    }

private:
    const PaletteSpec& palette_;
};

/// A round colour swatch; `label` (if any) is drawn under it. A small dot
/// marks a colour the user picked.
class ColorSwatch : public HoverButton {
public:
    ColorSwatch(QString label, QWidget* parent)
        : HoverButton(parent)
        , label_(std::move(label))
    {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(label_.isEmpty() ? QSize(32, 32) : QSize(64, 62));
    }

    void setColor(const QColor& color, bool custom)
    {
        color_ = color;
        custom_ = custom;
        update();
    }

    /// Right-click: back to the preset's colour.
    std::function<void()> onReset;

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        const Theme::Colors& ui = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double d = label_.isEmpty() ? width() : 38.0;
        const QRectF circle((width() - d) / 2.0, 0, d, d);
        if (isChecked() || underMouse()) {
            p.setPen(QPen(isChecked() ? ui.text : ui.textDim, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(circle.adjusted(1, 1, -1, -1));
        }
        p.setPen(QPen(ui.border, 1));
        p.setBrush(color_);
        p.drawEllipse(circle.adjusted(5, 5, -5, -5));
        if (custom_) {
            p.setPen(Qt::NoPen);
            p.setBrush(ui.text);
            p.drawEllipse(QRectF(circle.right() - 9, circle.top() + 1, 7, 7));
        }
        if (!label_.isEmpty()) {
            p.setPen(ui.textDim);
            p.drawText(QRectF(0, d + 2, width(), height() - d - 2), Qt::AlignHCenter | Qt::AlignTop, label_);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::RightButton && onReset) {
            onReset();
            return;
        }
        HoverButton::mousePressEvent(event);
    }

private:
    QString label_;
    QColor color_;
    bool custom_ = false;
};

} // namespace

ThemeSettingsPage::ThemeSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    // Presets.
    auto* presetCard = new SettingsCard(tr("Presets"), this);
    auto* tiles = new QWidget;
    auto* grid = new QGridLayout(tiles);
    grid->setContentsMargins(0, 8, 0, 4);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    presets_ = new QButtonGroup(this);
    presets_->setExclusive(true);
    int index = 0;
    for (const PaletteSpec& palette : palettes()) {
        auto* tile = new PresetTile(palette, tiles);
        presets_->addButton(tile, index);
        presetTiles_.append(tile);
        grid->addWidget(tile, index / 3, index % 3);
        ++index;
    }
    grid->setColumnStretch(3, 1);
    presetCard->addWide(tiles);
    connect(presets_, &QButtonGroup::idClicked, this, [this](int id) {
        choice_.paletteId = std::string(palettes()[static_cast<std::size_t>(id)].id);
        edit();
    });

    // Adjustments.
    auto* adjustCard = new SettingsCard(tr("Adjustments"), this);
    auto* knobs = new QWidget;
    auto* knobRow = new QHBoxLayout(knobs);
    knobRow->setContentsMargins(0, 6, 0, 2);
    knobRow->setSpacing(18);
    const auto percent = [](double v) { return QStringLiteral("%1%").arg(QString::number(v, 'f', 0)); };
    const auto signedValue = [](double v) {
        return QStringLiteral("%1%2").arg(v > 0 ? QStringLiteral("+") : QString(), QString::number(v, 'f', 0));
    };
    const auto addKnob = [&](const QString& caption, const QString& description, double min, double max,
                             double def, std::function<QString(double)> text, std::function<void(double)> apply) {
        auto* knob = new Knob(caption, min, max, def, knobs);
        knob->setDescription(description);
        knob->setValueText(std::move(text));
        connect(knob, &Knob::valueChanged, this, [this, apply = std::move(apply)](double v) {
            apply(v);
            edit();
        });
        knobRow->addWidget(knob);
        return knob;
    };
    hue_ = addKnob(tr("HUE"), tr("Turn the colour of every surface"), -180, 180, 0,
        [](double v) { return QStringLiteral("%1°").arg(QString::number(v, 'f', 0)); },
        [this](double v) { choice_.adjustments.hue = v; });
    saturation_ = addKnob(tr("SAT"), tr("Saturation: 0% is grey"), 0, 200, 100, percent,
        [this](double v) { choice_.adjustments.saturation = v / 100.0; });
    brightness_ = addKnob(tr("BRIGHT"), tr("Lighter or darker surfaces"), -30, 30, 0, signedValue,
        [this](double v) { choice_.adjustments.brightness = v / 100.0; });
    contrast_ = addKnob(tr("CONTRAST"), tr("How far panels stand apart from each other"), 50, 200, 100, percent,
        [this](double v) { choice_.adjustments.contrast = v / 100.0; });
    text_ = addKnob(tr("TEXT"), tr("Lighter or darker text"), -30, 30, 0, signedValue,
        [this](double v) { choice_.adjustments.text = v / 100.0; });
    knobRow->addStretch();
    auto* reset = new QPushButton(tr("Reset"), knobs);
    reset->setToolTip(tr("Put every knob back (double-click a knob to reset just that one)"));
    connect(reset, &QPushButton::clicked, this, [this] {
        choice_.adjustments = {};
        edit();
    });
    knobRow->addWidget(reset, 0, Qt::AlignVCenter);
    adjustCard->addWide(knobs);

    warning_ = new QLabel(tr("Some text is now hard to read (below WCAG AA contrast). Raise TEXT or lower BRIGHT."));
    warning_->setObjectName(QStringLiteral("themeWarning"));
    warning_->setWordWrap(true);
    warning_->hide();
    adjustCard->addWide(warning_);

    // Colours.
    auto* colourCard = new SettingsCard(tr("Colours"), this);
    auto* roles = new QWidget;
    auto* roleRow = new QHBoxLayout(roles);
    roleRow->setContentsMargins(0, 6, 0, 0);
    roleRow->setSpacing(6);
    for (int r = 0; r < static_cast<int>(kRoles.size()); ++r) {
        const Role& role = kRoles[static_cast<std::size_t>(r)];
        auto* swatch = new ColorSwatch(tr(role.label), roles);
        swatch->setToolTip(tr("Click to pick a colour, right-click for the preset's"));
        swatch->setAccessibleName(tr("%1 colour").arg(tr(role.label)));
        swatch->onReset = [this, r] {
            choice_.overrides.*kRoles[static_cast<std::size_t>(r)].member = std::nullopt;
            edit();
        };
        connect(swatch, &QAbstractButton::clicked, this, [this, r] { pickRoleColor(r); });
        roleSwatches_.append(swatch);
        roleRow->addWidget(swatch);
    }
    roleRow->addStretch();
    auto* resetColours = new QPushButton(tr("Reset colours"), roles);
    connect(resetColours, &QPushButton::clicked, this, [this] {
        choice_.overrides = {};
        edit();
    });
    roleRow->addWidget(resetColours, 0, Qt::AlignVCenter);
    colourCard->addWide(roles);

    auto* quick = new QWidget;
    auto* quickRow = new QHBoxLayout(quick);
    quickRow->setContentsMargins(0, 0, 0, 0);
    quickRow->setSpacing(2);
    for (const AccentSpec& accent : accents()) {
        auto* swatch = new ColorSwatch(QString(), quick);
        swatch->setCheckable(true);
        swatch->setToolTip(qstring(accent.name));
        swatch->setAccessibleName(QStringLiteral("%1 accent").arg(qstring(accent.name)));
        swatch->setColor(qcolor(accent.color), false);
        const std::string id(accent.id);
        connect(swatch, &QAbstractButton::clicked, this, [this, id] {
            choice_.accentId = id;
            choice_.overrides.selection = std::nullopt; // the pick replaces a custom one
            edit();
        });
        accentSwatches_.append(swatch);
        quickRow->addWidget(swatch);
    }
    colourCard->addRow(tr("Quick accents"), tr("Selections, lit buttons, knobs and faders."), quick);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    layout->addWidget(presetCard);
    layout->addWidget(adjustCard);
    layout->addWidget(colourCard);
    layout->addStretch();

    refresh();
}

void ThemeSettingsPage::setChoice(const ThemeChoice& choice)
{
    choice_ = choice;
    refresh();
}

void ThemeSettingsPage::edit()
{
    refresh();
    emit choiceChanged(choice_);
}

void ThemeSettingsPage::refresh()
{
    const QSignalBlocker block(presets_);
    const std::string palette(paletteById(choice_.paletteId).id);
    const std::string accent(accentById(choice_.accentId).id);
    for (std::size_t i = 0; i < palettes().size(); ++i)
        presetTiles_[static_cast<qsizetype>(i)]->setChecked(palettes()[i].id == palette);
    for (std::size_t i = 0; i < accents().size(); ++i)
        accentSwatches_[static_cast<qsizetype>(i)]->setChecked(
            !choice_.overrides.selection && accents()[i].id == accent);

    // Knobs show the choice without echoing it back.
    for (Knob* knob : {hue_, saturation_, brightness_, contrast_, text_})
        knob->blockSignals(true);
    hue_->setValue(choice_.adjustments.hue);
    saturation_->setValue(choice_.adjustments.saturation * 100.0);
    brightness_->setValue(choice_.adjustments.brightness * 100.0);
    contrast_->setValue(choice_.adjustments.contrast * 100.0);
    text_->setValue(choice_.adjustments.text * 100.0);
    for (Knob* knob : {hue_, saturation_, brightness_, contrast_, text_})
        knob->blockSignals(false);

    const ResolvedTheme resolved = resolveTheme(choice_);
    for (std::size_t r = 0; r < kRoles.size(); ++r)
        static_cast<ColorSwatch*>(roleSwatches_[static_cast<qsizetype>(r)])
            ->setColor(qcolor(resolved.*kRoles[r].resolved), (choice_.overrides.*kRoles[r].member).has_value());
    warning_->setVisible(hasLowContrast(resolved));

    for (QAbstractButton* tile : presetTiles_)
        tile->update(); // their accent follows the theme
}

void ThemeSettingsPage::pickRoleColor(int role)
{
    const Role& r = kRoles[static_cast<std::size_t>(role)];
    const QColor current = qcolor(resolveTheme(choice_).*r.resolved);
    const QColor picked = QColorDialog::getColor(current, this, tr("%1 colour").arg(tr(r.label)));
    if (!picked.isValid())
        return; // cancelled
    choice_.overrides.*r.member = rgb(picked);
    edit();
}

} // namespace vloomdaw
