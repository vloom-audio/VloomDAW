// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/AppSettings.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>

using namespace vloomdaw;

namespace {

/// Points QSettings at a fresh INI file in a temporary folder, so the tests
/// never read or write the user's real settings (the registry on Windows).
class ScratchSettings {
public:
    ScratchSettings()
    {
        REQUIRE(dir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("VloomAudioTests"));
        QCoreApplication::setApplicationName(QStringLiteral("AppSettingsTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir_.path());
        QSettings().clear();
    }

private:
    QTemporaryDir dir_;
};

} // namespace

TEST_CASE("Exclusive mode is off until the user turns it on", "[ui][settings]")
{
    const ScratchSettings scratch;
    CHECK_FALSE(AppSettings::load().audioExclusive);
}

TEST_CASE("Exclusive mode survives a save and load", "[ui][settings]")
{
    const ScratchSettings scratch;
    AppSettings saved;
    saved.audioExclusive = true;
    saved.sampleRate = 96000;
    saved.bufferFrames = 128;
    saved.save();

    AppSettings loaded = AppSettings::load();
    CHECK(loaded.audioExclusive);
    CHECK(loaded.sampleRate == 96000);
    CHECK(loaded.bufferFrames == 128);

    loaded.audioExclusive = false;
    loaded.save();
    CHECK_FALSE(AppSettings::load().audioExclusive);
}
