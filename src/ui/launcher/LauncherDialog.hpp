// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QDialog>
#include <QString>

#include <vector>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QGridLayout;
class QLineEdit;
class QWidget;

namespace vloomdaw {

/// The start window: begin a new project or get back to a recent one
/// (File > Start Window, and at startup unless turned off).
///
/// It only chooses; MainWindow does the work after exec() returns.
class LauncherDialog : public QDialog {
    Q_OBJECT

public:
    /// What the status line says about the audio output.
    struct AudioStatus {
        QString device; ///< empty: no device open
        int sampleRate = 0;
        int bufferFrames = 0;
    };

    enum class Choice {
        None, ///< closed: carry on with what is open
        NewProject, ///< an empty project at tempo()
        OpenFile, ///< path()
        BrowseOpen, ///< File > Open's dialog
        AudioSettings,
    };

    /// `tempo`: the new project's tempo until the user has chosen one (the
    /// last one is remembered).
    LauncherDialog(const AudioStatus& audio, double tempo, QWidget* parent = nullptr);

    [[nodiscard]] Choice choice() const { return choice_; }
    [[nodiscard]] QString path() const { return path_; }
    [[nodiscard]] double tempo() const;

    /// Whether to show the launcher when VloomDAW starts (QSettings).
    [[nodiscard]] static bool showAtStartup();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Filter { All, Pinned, ThisWeek };

    /// Places the cards in as many columns as the view has room for.
    void layoutCards();

    [[nodiscard]] QWidget* createSidePanel(double tempo);
    [[nodiscard]] QWidget* createRecentPanel();
    [[nodiscard]] QWidget* createStatusBar(const AudioStatus& audio);
    /// Lays out the cards for the projects the filter and search let through.
    void rebuildCards();
    void choose(Choice choice, const QString& path = {});
    void showCardMenu(const QString& path, const QPoint& globalPos);

    Choice choice_ = Choice::None;
    QString path_;
    QDoubleSpinBox* tempo_ = nullptr;
    QLineEdit* search_ = nullptr;
    QButtonGroup* filters_ = nullptr;
    QGridLayout* cards_ = nullptr;
    QWidget* cardsHost_ = nullptr;
    QWidget* viewport_ = nullptr; ///< the cards' scroll area's, watched for resizes
    std::vector<QWidget*> cardWidgets_; ///< owned by cardsHost_
    int columns_ = 0;
    QCheckBox* startup_ = nullptr;
};

} // namespace vloomdaw
