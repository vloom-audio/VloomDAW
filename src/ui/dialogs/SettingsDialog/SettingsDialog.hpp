// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;

namespace vloomdaw {

class AudioSettingsPage;
class GeneralSettingsPage;

/// The Settings window: a category list on the left, one page per category.
/// Changes apply as they are made, so there is only a Close button.
///
/// To add a category: write a page class next to the others and register it
/// with addPage() in the constructor.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    enum class Page { General, Audio };

    explicit SettingsDialog(QWidget* parent = nullptr);

    [[nodiscard]] GeneralSettingsPage* generalPage() const { return general_; }
    [[nodiscard]] AudioSettingsPage* audioPage() const { return audio_; }

    /// Shows the window on `page`.
    void showPage(Page page);

private:
    void addPage(const QString& title, QWidget* page);

    QListWidget* nav_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    GeneralSettingsPage* general_ = nullptr;
    AudioSettingsPage* audio_ = nullptr;
};

} // namespace vloomdaw
