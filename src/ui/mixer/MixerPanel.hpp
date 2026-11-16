// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginSlot.hpp"

#include <QList>
#include <QStringList>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;

namespace vloomdaw {

class DetachButton;
class LevelMeter;
class MixerStrip;

/// The mixer panel: the master track, then the insert tracks in a row that
/// scrolls sideways, topped by a header bar and anchored at the bottom by a
/// Studio One-style console toolbar (`#mixerFooter`) with channel filters,
/// strip view options and batch console actions.
class MixerPanel : public QWidget {
    Q_OBJECT

public:
    enum class StripFilter { All = 0, Audio = 1, Instrument = 2, Effects = 3 };

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
    /// Shows track `track` (0 = master) in the user's colour (0xRRGGBB; nullopt: none).
    void setTrackColor(std::size_t track, std::optional<std::uint32_t> color);
    /// Marks track `track` (0 = master) as the currently selected mixer strip.
    void setSelectedTrack(std::size_t track);
    [[nodiscard]] std::size_t selectedTrack() const { return selectedTrack_; }
    /// Updates track `track`'s FX rack with its effects chain.
    void setTrackEffects(std::size_t track, const std::vector<PluginSlot>& effects);
    /// Updates what kind of channel routes into insert `track` for footer filtering.
    void setTrackSourceFlags(std::size_t track, bool hasAudio, bool hasInstrument);
    /// Resets peak holds and clip indicators across Master and all Insert strips.
    void resetAllPeaks();
    /// Reflects global FX bypass state on the footer button without emitting.
    void setAllEffectsBypassed(bool bypassed);

    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] QSize sizeHint() const override;

public slots:
    void setSilenced(std::size_t track, bool silenced);
    /// Shows insert `track`'s effect count on its FX button.
    void setEffectCount(std::size_t track, std::size_t count);
    /// Shows a track's saved fader, pan, mute and solo without signalling them.
    void setTrackValues(std::size_t track, double fader, double pan, bool muted, bool soloed);
    /// Shows "dock" instead of "detach" on the header button.
    void setDetached(bool detached);

signals:
    void trackSelected(std::size_t track);
    void faderChanged(std::size_t track, double position);
    void panChanged(std::size_t track, double pan);
    void muteToggled(std::size_t track, bool muted);
    void soloToggled(std::size_t track, bool soloed);
    void closeRequested();
    void detachRequested();
    void addInsertRequested();
    void removeInsertRequested(std::size_t track);
    void renameRequested(std::size_t track);
    void moveInsertRequested(std::size_t from, std::size_t to);
    void colorChanged(std::size_t track, std::optional<std::uint32_t> color);
    void effectsMenuRequested(std::size_t track, const QPoint& globalPos);
    void effectBypassToggled(std::size_t track, std::size_t slot, bool bypassed);
    void effectClicked(std::size_t track, std::size_t slot);
    void effectMenuRequested(std::size_t track, std::size_t slot, const QPoint& globalPos);
    void pluginDroppedOnSlot(std::size_t track, std::size_t slot, std::size_t plugin);
    void pluginDropped(std::size_t track, std::size_t plugin);
    void clearAllSoloRequested();
    void clearAllMuteRequested();
    void bypassAllEffectsToggled(bool bypassed);

private:
    void connectStrip(MixerStrip* strip);
    [[nodiscard]] MixerStrip* makeInsertStrip(std::size_t track);
    void applyStripFilter();
    void updateSelectedLabel();

    QList<MixerStrip*> strips_;
    DetachButton* detach_ = nullptr;
    QToolButton* close_ = nullptr;
    QHBoxLayout* header_ = nullptr;
    QWidget* inserts_ = nullptr; ///< the scrolling row of insert strips
    QHBoxLayout* insertRow_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QStringList names_; ///< [i] the name of track i ("" = "Insert i")
    std::size_t selectedTrack_ = 0;

    StripFilter filter_ = StripFilter::All;
    bool wideMode_ = false;
    bool showFxRack_ = true;
    QLabel* selectedLabel_ = nullptr;
    QPushButton* bypassAllFxBtn_ = nullptr;
};

} // namespace vloomdaw
