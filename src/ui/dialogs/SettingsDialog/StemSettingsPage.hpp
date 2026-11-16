// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QLabel;

namespace vloomdaw {

class ToggleSwitch;

/// Settings → Stem Extractor: the model in use (of those on this computer),
/// the way to the model store, whether the graphics card is used, and what
/// happens to the clip once its stems are placed.
/// A pure view: emits what the user picked.
class StemSettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit StemSettingsPage(QWidget* parent = nullptr);

    /// The models on this computer (ids of core/StemModels) and the one in
    /// use (may be empty). Never emits.
    void setModels(const QStringList& installed, const QString& chosen);
    void setUseGpu(bool on);
    /// 25, 50 or 75 percent (the nearest is shown).
    void setOverlap(int percent);
    void setMuteOriginal(bool on);

signals:
    void modelChosen(const QString& id);
    void useGpuChanged(bool on);
    void overlapChanged(int percent);
    void muteOriginalChanged(bool on);
    /// The user asked for the model store.
    void storeRequested();

private:
    void describeModel();

    QComboBox* model_ = nullptr;
    QLabel* modelAbout_ = nullptr;
    QLabel* storeAbout_ = nullptr;
    ToggleSwitch* gpu_ = nullptr; ///< null in builds without a graphics-card path
    QComboBox* overlap_ = nullptr; ///< null in builds without the models it applies to
    ToggleSwitch* mute_ = nullptr;
};

} // namespace vloomdaw
