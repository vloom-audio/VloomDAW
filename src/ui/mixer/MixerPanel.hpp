// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QList>
#include <QWidget>

#include <cstddef>

class QHBoxLayout;
class QToolButton;

namespace vloomdaw {

class DetachButton;
class LevelMeter;
class MixerStrip;

/// The mixer panel: the master track, then the insert tracks in a row that
/// scrolls sideways, as in FL Studio. Track 0 is the master (matching
/// MixerModel). A pure view: MainWindow connects it to the model and engine.
class MixerPanel : public QWidget {
    Q_OBJECT

public:
    MixerPanel(std::size_t insertCount, QWidget* parent = nullptr);

    [[nodiscard]] std::size_t trackCount() const { return static_cast<std::size_t>(strips_.size()); }
    [[nodiscard]] LevelMeter* meter(std::size_t track) const;
    /// Adds `widget` at the right end of the header (e.g. window buttons).
    void addHeaderWidget(QWidget* widget);

public slots:
    void setSilenced(std::size_t track, bool silenced);
    /// Shows "dock" instead of "detach" on the header button.
    void setDetached(bool detached);

signals:
    void faderChanged(std::size_t track, double position);
    void panChanged(std::size_t track, double pan);
    void muteToggled(std::size_t track, bool muted);
    void soloToggled(std::size_t track, bool soloed);
    void closeRequested();
    /// The user asked to detach the mixer into its own window, or to dock it.
    void detachRequested();

private:
    void connectStrip(MixerStrip* strip, std::size_t track);

    QList<MixerStrip*> strips_;
    DetachButton* detach_ = nullptr;
    QToolButton* close_ = nullptr;
    QHBoxLayout* header_ = nullptr;
};

} // namespace vloomdaw
