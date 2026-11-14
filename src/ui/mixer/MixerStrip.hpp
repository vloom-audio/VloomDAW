// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QFrame>

#include <cstddef>
#include <cstdint>
#include <optional>

class QLabel;
class QToolButton;

namespace vloomdaw {

class Fader;
class Knob;
class LevelMeter;

/// One mixer track: name, pan knob, level meter, fader, dB readout, mute
/// and solo, like a track in FL Studio's mixer. A pure view: it emits what
/// the user did; MixerPanel adds the track number.
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
    /// Shows how many effects the track has.
    void setEffectCount(std::size_t count);
    /// Shows saved settings (a project was opened) without signalling them back.
    void setValues(double fader, double pan, bool muted, bool soloed);
    [[nodiscard]] std::optional<std::uint32_t> color() const { return color_; }

public slots:
    /// Greys the strip out while another insert's solo silences it.
    void setSilenced(bool silenced);

signals:
    void faderChanged(double position); ///< 0..1, see faderToGain()
    void panChanged(double pan); ///< -1 left .. +1 right
    void muteToggled(bool muted);
    void soloToggled(bool soloed);
    /// The name was double-clicked (inserts only): the user wants to rename.
    void renameRequested();
    /// The FX button was clicked: show the track's effects at `globalPos`.
    void effectsMenuRequested(const QPoint& globalPos);
    /// Plugin `plugin` (an index into the plugin store) was dropped on the strip.
    void pluginDropped(std::size_t plugin);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void showDb(double position);
    /// Shows fullName_ in name_, cut with "…" to the room it has.
    void elideName();

    LevelMeter* meter_ = nullptr;
    Fader* fader_ = nullptr;
    Knob* pan_ = nullptr;
    QLabel* db_ = nullptr;
    QLabel* name_ = nullptr;
    QString fullName_;
    QLabel* number_ = nullptr;
    QToolButton* mute_ = nullptr;
    QToolButton* solo_ = nullptr; ///< inserts only
    QToolButton* effects_ = nullptr; ///< the FX button
    std::optional<std::uint32_t> color_;
};

} // namespace vloomdaw
