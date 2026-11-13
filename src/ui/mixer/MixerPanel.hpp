// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

#include <cstddef>

class QHBoxLayout;
class QScrollArea;
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
    /// Appends a strip for a new insert (track trackCount()) and scrolls to it.
    void addInsert();
    /// Removes insert `track`'s strip; the strips after it keep their
    /// settings and take the numbers one lower.
    void removeInsert(std::size_t track);
    /// Moves insert `from`'s strip to position `to`; it keeps its settings.
    void moveInsert(std::size_t from, std::size_t to);
    /// Shows insert `track` under `name`.
    void setTrackName(std::size_t track, const QString& name);

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
    /// The user asked for one more insert ("+ Insert").
    void addInsertRequested();
    /// The user asked to delete insert `track` ("−" for the last one, or a
    /// strip's context menu).
    void removeInsertRequested(std::size_t track);
    /// The user asked to rename insert `track` (double-click on its name or
    /// its context menu).
    void renameRequested(std::size_t track);
    /// The user asked to move insert `from` to position `to` (context menu).
    void moveInsertRequested(std::size_t from, std::size_t to);

private:
    void connectStrip(MixerStrip* strip);
    [[nodiscard]] MixerStrip* makeInsertStrip(std::size_t track);

    QList<MixerStrip*> strips_;
    DetachButton* detach_ = nullptr;
    QToolButton* close_ = nullptr;
    QHBoxLayout* header_ = nullptr;
    QWidget* inserts_ = nullptr; ///< the scrolling row of insert strips
    QHBoxLayout* insertRow_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QStringList names_; ///< [i] the name of track i ("" = "Insert i")
};

} // namespace vloomdaw
