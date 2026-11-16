// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QDialog>
#include <QString>

class QCloseEvent;
class QLabel;
class QProgressBar;
class QPushButton;

namespace vloomdaw {

/// Studio One-style modal loading progress dialog shown while opening a `.vloom`
/// project: displays the active stage ("Reading project file…",
/// "Loading Device: <PluginName>", "Checking audio samples…",
/// "Building mixer & arrangement…"), a smooth accent progress bar, and a
/// responsive Cancel button.
class ProjectLoadDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProjectLoadDialog(const QString& projectName, QWidget* parent = nullptr);

    /// Updates the displayed stage, sub-detail and progress fraction (`step / totalSteps`),
    /// repaints immediately, and pumps user input so Cancel stays responsive.
    /// Returns `false` when the user has cancelled.
    bool advance(int step, int totalSteps, const QString& stepText, const QString& detailText = {});

    [[nodiscard]] bool wasCanceled() const { return canceled_; }

    void finishAndClose();

    void reject() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QLabel* stepLabel_ = nullptr;
    QLabel* detailLabel_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    bool canceled_ = false;
    bool finishing_ = false;
};

} // namespace vloomdaw
