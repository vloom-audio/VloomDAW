// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginSlot.hpp"

#include <QFrame>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class QLabel;
class QResizeEvent;
class QToolButton;

namespace vloomdaw {

class Fader;
class Knob;
class LevelMeter;
class MixerSlotRack;

/// One mixer track: name, pan knob, FX rack, level meter, fader, dB readout,
/// mute and solo. Emits user interactions; MixerPanel manages strip ordering
/// and selection state.
class MixerStrip : public QFrame {
    Q_OBJECT

public:
    enum class Kind { Master, Insert };

    MixerStrip(Kind kind, const QString& name, const QString& number, QWidget* parent = nullptr);

    [[nodiscard]] LevelMeter* meter() const { return meter_; }
    /// New name and number, e.g. after an insert before it was deleted.
    void setLabel(const QString& name, const QString& number);
    /// The user's colour (0xRRGGBB), drawn as a bar along the top; nullopt: none.
    void setColor(std::optional<std::uint32_t> color);
    /// Marks this strip as the currently selected mixer channel (Fender Studio style).
    void setSelected(bool selected);
    [[nodiscard]] bool isSelected() const { return selected_; }
    /// Shows how many effects the track has (kept for compatibility).
    void setEffectCount(std::size_t count);
    /// Shows the track's effects in the FX rack.
    void setEffects(const std::vector<PluginSlot>& effects);
    [[nodiscard]] bool hasEffects() const { return effectCount_ > 0; }
    /// Toggles wider strip & FX slot layout (68 px vs 88 px).
    void setWideMode(bool wide);
    /// Shows or hides the FX slot rack so faders can use the full strip height.
    void setFxRackVisible(bool visible);
    /// Marks what kind of channel routes into this insert strip for footer filtering.
    void setSourceFlags(bool hasAudio, bool hasInstrument);
    [[nodiscard]] bool hasAudioSource() const { return hasAudioSource_; }
    [[nodiscard]] bool hasInstrumentSource() const { return hasInstrumentSource_; }
    /// Shows saved settings (a project was opened) without signalling them back.
    void setValues(double fader, double pan, bool muted, bool soloed);
    [[nodiscard]] std::optional<std::uint32_t> color() const { return color_; }
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] QSize sizeHint() const override;

public slots:
    /// Greys the strip out while another insert's solo silences it.
    void setSilenced(bool silenced);

signals:
    void clicked();
    void faderChanged(double position); ///< 0..1, see faderToGain()
    void panChanged(double pan); ///< -1 left .. +1 right
    void muteToggled(bool muted);
    void soloToggled(bool soloed);
    /// The name was double-clicked (inserts only): the user wants to rename.
    void renameRequested();
    /// The top colour bar was clicked: open the colour picker at `globalPos`.
    void colorPickRequested(const QPoint& globalPos);
    /// The FX button was clicked: show the track's effects at `globalPos`.
    void effectsMenuRequested(const QPoint& globalPos);
    /// An effect slot's bypass switch was clicked.
    void effectBypassToggled(std::size_t slot, bool bypassed);
    /// An effect slot body was clicked: open editor (if loaded) or insert menu (if empty).
    void effectClicked(std::size_t slot);
    /// Context menu requested for slot `slot` at `globalPos`.
    void effectMenuRequested(std::size_t slot, const QPoint& globalPos);
    /// Plugin `plugin` was dropped specifically onto slot `slot`.
    void pluginDroppedOnSlot(std::size_t slot, std::size_t plugin);
    /// Plugin `plugin` (an index into the plugin store) was dropped on the strip.
    void pluginDropped(std::size_t plugin);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void showDb(double position);
    /// Shows fullName_ in name_, cut with "…" to the room it has.
    void elideName();
    void syncFxSlotCount();

    LevelMeter* meter_ = nullptr;
    Fader* fader_ = nullptr;
    Knob* pan_ = nullptr;
    QLabel* db_ = nullptr;
    QLabel* name_ = nullptr;
    QString fullName_;
    QLabel* number_ = nullptr;
    QLabel* route_ = nullptr;
    QToolButton* mute_ = nullptr;
    QToolButton* solo_ = nullptr; ///< inserts only
    MixerSlotRack* fxRack_ = nullptr; ///< scrollable FX slots
    Kind kind_ = Kind::Insert;
    std::size_t effectCount_ = 0;
    bool wideMode_ = false;
    bool hasAudioSource_ = false;
    bool hasInstrumentSource_ = false;
    std::optional<std::uint32_t> color_;
    bool selected_ = false;
};

} // namespace vloomdaw
