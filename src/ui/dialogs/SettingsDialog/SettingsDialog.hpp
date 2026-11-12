// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;

namespace vloomdaw {

class AudioSettingsPage;
class GeneralSettingsPage;
class ThemeSettingsPage;

/// The Settings window: categories with icons on the left (General, Audio,
/// Theme), one page of settings cards per category.
/// Changes apply as they are made, so there is only a Close button.
///
/// To add a category: write a page class next to the others and register it
/// with addPage() in the constructor.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    enum class Page { General, Audio, Theme };

    explicit SettingsDialog(QWidget* parent = nullptr);

    [[nodiscard]] GeneralSettingsPage* generalPage() const { return general_; }
    [[nodiscard]] AudioSettingsPage* audioPage() const { return audio_; }
    [[nodiscard]] ThemeSettingsPage* themePage() const { return theme_; }

    /// Shows the window on `page`.
    void showPage(Page page);

private:
    /// `icon` is a name in resources/icons ("sliders" for :/icons/sliders.svg).
    void addPage(const QString& icon, const QString& title, const QString& subtitle, QWidget* page);

    QListWidget* nav_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    GeneralSettingsPage* general_ = nullptr;
    AudioSettingsPage* audio_ = nullptr;
    ThemeSettingsPage* theme_ = nullptr;
};

} // namespace vloomdaw
