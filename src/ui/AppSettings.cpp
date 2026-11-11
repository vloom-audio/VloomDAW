// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/AppSettings.hpp"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <optional>

namespace vloomdaw {

namespace {
constexpr QLatin1StringView kOutputDevice{"audio/outputDevice"};
constexpr QLatin1StringView kOutputDeviceOccurrence{"audio/outputDeviceOccurrence"};
constexpr QLatin1StringView kSampleRate{"audio/sampleRate"};
constexpr QLatin1StringView kBufferFrames{"audio/bufferFrames"};
constexpr QLatin1StringView kStartPositionMode{"general/startPositionMode"};
constexpr QLatin1StringView kMetronomeOnStartup{"general/metronomeOnStartup"};
constexpr QLatin1StringView kBrowserFolders{"browser/folders"};
constexpr QLatin1StringView kBrowserFolder{"browser/current"};
constexpr QLatin1StringView kThemePalette{"theme/palette"};
constexpr QLatin1StringView kThemeAccent{"theme/accent"};
constexpr QLatin1StringView kThemeHue{"theme/hue"};
constexpr QLatin1StringView kThemeSaturation{"theme/saturation"};
constexpr QLatin1StringView kThemeBrightness{"theme/brightness"};
constexpr QLatin1StringView kThemeContrast{"theme/contrast"};
constexpr QLatin1StringView kThemeText{"theme/text"};

/// Per-role colour overrides, stored as "#rrggbb" (absent = the preset's).
struct RoleKey {
    QLatin1StringView key;
    std::optional<Rgb> ThemeOverrides::*member;
};
const RoleKey kRoleKeys[] = {
    {QLatin1StringView("theme/color/selection"), &ThemeOverrides::selection},
    {QLatin1StringView("theme/color/playing"), &ThemeOverrides::playing},
    {QLatin1StringView("theme/color/record"), &ThemeOverrides::record},
    {QLatin1StringView("theme/color/lcd"), &ThemeOverrides::lcd},
    {QLatin1StringView("theme/color/text"), &ThemeOverrides::text},
};
} // namespace

AppSettings AppSettings::load()
{
    const QSettings s;
    AppSettings a;
    a.outputDevice = s.value(kOutputDevice).toString();
    a.outputDeviceOccurrence = std::max(1, s.value(kOutputDeviceOccurrence, 1).toInt());
    a.sampleRate = validSampleRate(s.value(kSampleRate, a.sampleRate).toInt());
    a.bufferFrames = validBufferFrames(s.value(kBufferFrames, a.bufferFrames).toInt());
    a.startPositionMode
        = s.value(kStartPositionMode, 0).toInt() == 1 ? PositionMode::Time : PositionMode::BarsBeats;
    a.metronomeOnStartup = s.value(kMetronomeOnStartup, a.metronomeOnStartup).toBool();
    ThemeChoice& t = a.theme;
    t.paletteId = s.value(kThemePalette, QString::fromStdString(t.paletteId)).toString().toStdString();
    t.accentId = s.value(kThemeAccent, QString::fromStdString(t.accentId)).toString().toStdString();
    t.adjustments.hue = s.value(kThemeHue, t.adjustments.hue).toDouble();
    t.adjustments.saturation = s.value(kThemeSaturation, t.adjustments.saturation).toDouble();
    t.adjustments.brightness = s.value(kThemeBrightness, t.adjustments.brightness).toDouble();
    t.adjustments.contrast = s.value(kThemeContrast, t.adjustments.contrast).toDouble();
    t.adjustments.text = s.value(kThemeText, t.adjustments.text).toDouble();
    for (const RoleKey& role : kRoleKeys)
        t.overrides.*role.member = parseHex(s.value(role.key).toString().toStdString());

    if (s.contains(kBrowserFolders)) {
        a.browserFolders = s.value(kBrowserFolders).toStringList();
    } else {
        // First run: start with the user's Music folder if there is one.
        const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (!music.isEmpty() && QDir(music).exists())
            a.browserFolders << music;
    }
    a.browserFolder = s.value(kBrowserFolder, a.browserFolders.value(0)).toString();
    return a;
}

void AppSettings::save() const
{
    QSettings s;
    s.setValue(kOutputDevice, outputDevice);
    s.setValue(kOutputDeviceOccurrence, outputDeviceOccurrence);
    s.setValue(kSampleRate, sampleRate);
    s.setValue(kBufferFrames, bufferFrames);
    s.setValue(kStartPositionMode, static_cast<int>(startPositionMode));
    s.setValue(kMetronomeOnStartup, metronomeOnStartup);
    s.setValue(kBrowserFolders, browserFolders);
    s.setValue(kBrowserFolder, browserFolder);
    s.setValue(kThemePalette, QString::fromStdString(theme.paletteId));
    s.setValue(kThemeAccent, QString::fromStdString(theme.accentId));
    s.setValue(kThemeHue, theme.adjustments.hue);
    s.setValue(kThemeSaturation, theme.adjustments.saturation);
    s.setValue(kThemeBrightness, theme.adjustments.brightness);
    s.setValue(kThemeContrast, theme.adjustments.contrast);
    s.setValue(kThemeText, theme.adjustments.text);
    for (const RoleKey& role : kRoleKeys) {
        if (const auto& color = theme.overrides.*role.member)
            s.setValue(role.key, QString::fromStdString(toHex(*color)));
        else
            s.remove(role.key);
    }
}

} // namespace vloomdaw
