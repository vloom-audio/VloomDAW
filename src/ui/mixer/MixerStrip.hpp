// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/MixerModel.hpp"
#include "core/PluginSlot.hpp"

#include <QFrame>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QToolButton;
class QWidget;

namespace vloomdaw {

class Fader;
class LevelMeter;
class MixerSendRack;
class MixerSlotRack;
class StripPanBar;
class StripRackHandle;
class StripStereoButton;
class StripTypeBadge;

/// Visual representation of one active Send row on a `MixerStrip`.
struct MixerSendDisplay {
    int target = 0;
    QString targetName{};
    double level = 0.8;
    bool muted = false;
    bool preFader = false;
    bool sidechain = false;
};

/// One Studio One-style console strip:
/// - Top colour bar, `Inserts` header + FX slot rack, `Sends` header + send bars
/// - Instrument / Audio source button (clicking an Instrument's name opens its plugin editor)
/// - Output routing button (`Main`, `→ Bus 1`, `→ Aux 1`, etc.)
/// - Bipolar horizontal pan bar (`<C>`), `[M]`/`[S]`, `0.0 dB` readout above the fader
/// - Stereo `LevelMeter` + sculpted `Fader`
/// - Channel number + type icon (`Instrument` keys, `Audio` wave, `FX`, `BUS`, `AUX`)
/// - Full-colour bottom name plate
class MixerStrip : public QFrame {
    Q_OBJECT

public:
    enum class Kind { Master, Insert };

    MixerStrip(Kind kind, const QString& name, const QString& number, QWidget* parent = nullptr);

    [[nodiscard]] LevelMeter* meter() const { return meter_; }
    void setLabel(const QString& name, const QString& number);
    void setColor(std::optional<std::uint32_t> color);
    void setSelected(bool selected);
    [[nodiscard]] bool isSelected() const { return selected_; }

    void setRole(MixerRole role);
    [[nodiscard]] MixerRole role() const { return role_; }

    void setEffectCount(std::size_t count);
    void setEffects(const std::vector<PluginSlot>& effects);
    [[nodiscard]] bool hasEffects() const { return effectCount_ > 0; }

    void setSends(const std::vector<MixerSendDisplay>& sends);

    /// Updates channel source metadata & output target shown on the routing pills:
    /// - Instrument strip: shows `instrumentName` in gold (click opens instrument editor).
    /// - Audio strip: shows `sourceLabel` (e.g. "Audio · Stereo") and `outputLabel` ("Main", "Bus 1", ...).
    void setChannelRoutingInfo(bool hasAudio, bool hasInstrument, const QString& instrumentName,
        const QString& sourceLabel, int outputTarget, const QString& outputLabel);

    void setSourceFlags(bool hasAudio, bool hasInstrument);
    [[nodiscard]] bool hasAudioSource() const { return hasAudioSource_; }
    [[nodiscard]] bool hasInstrumentSource() const { return hasInstrumentSource_; }
    [[nodiscard]] bool isBusOrFxRole() const { return role_ != MixerRole::Standard; }

    void setWideMode(bool wide);
    void setFxRackVisible(bool visible);
    void setUserFxSlotRows(int rows);
    [[nodiscard]] int userFxSlotRows() const { return userFxSlotRows_; }
    void setValues(double fader, double pan, bool muted, bool soloed);
    void setMono(bool mono);
    [[nodiscard]] bool isMono() const { return mono_; }
    void setRecordArmed(bool armed);
    [[nodiscard]] bool isRecordArmed() const { return recordArmed_; }
    void setInputMonitored(bool monitored);
    [[nodiscard]] bool isInputMonitored() const { return inputMonitored_; }
    void startInlineRename();
    [[nodiscard]] std::optional<std::uint32_t> color() const { return color_; }
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] QSize sizeHint() const override;

public slots:
    void setSilenced(bool silenced);

signals:
    void clicked();
    void faderChanged(double position); ///< 0..1, see faderToGain()
    void panChanged(double pan); ///< -1 left .. +1 right
    void muteToggled(bool muted);
    void soloToggled(bool soloed);
    void monoToggled(bool mono);
    void recordArmToggled(bool armed);
    void monitorToggled(bool monitored);
    void userFxSlotRowsChanged(int rows);
    void renameRequested();
    void nameEdited(const QString& name);
    void stripDragMoved(const QPoint& globalPos);
    void stripDragReleased(const QPoint& globalPos);
    void colorPickRequested(const QPoint& globalPos);
    void effectsMenuRequested(const QPoint& globalPos);
    void effectBypassToggled(std::size_t slot, bool bypassed);
    void effectClicked(std::size_t slot);
    void effectMenuRequested(std::size_t slot, const QPoint& globalPos);
    void pluginDroppedOnSlot(std::size_t slot, std::size_t plugin);
    void pluginDropped(std::size_t plugin);

    /// Studio One source / instrument / output / send interactions:
    void instrumentClicked();
    void instrumentMenuRequested(const QPoint& globalPos);
    void sourceClicked(const QPoint& globalPos);
    void outputRoutingRequested(const QPoint& globalPos);
    void addSendMenuRequested(const QPoint& globalPos);
    void sendLevelChanged(std::size_t sendSlot, double level);
    void sendMuteToggled(std::size_t sendSlot, bool muted);
    void sendPreFaderToggled(std::size_t sendSlot, bool preFader);
    void sendSidechainToggled(std::size_t sendSlot, bool sidechain);
    void sendMenuRequested(std::size_t sendSlot, const QPoint& globalPos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void showDb(double position);
    void elideName();
    void commitInlineRename();
    void cancelInlineRename();
    void refreshRoutingPills();
    void updateCapAndNamePlateStyle();
    void syncRackHeights();

    QWidget* insertsHdr_ = nullptr;
    MixerSlotRack* fxRack_ = nullptr;
    StripRackHandle* fxResizeHandle_ = nullptr;
    QWidget* sendsHdr_ = nullptr;
    MixerSendRack* sendRack_ = nullptr;

    QPushButton* sourceBtn_ = nullptr;
    QPushButton* outputBtn_ = nullptr;
    StripPanBar* panBar_ = nullptr;

    QToolButton* mute_ = nullptr;
    QToolButton* solo_ = nullptr;
    QToolButton* arm_ = nullptr;
    QToolButton* monitor_ = nullptr;
    StripStereoButton* monoBtn_ = nullptr;
    QLabel* db_ = nullptr;

    LevelMeter* meter_ = nullptr;
    Fader* fader_ = nullptr;

    QLabel* number_ = nullptr;
    StripTypeBadge* badge_ = nullptr;
    QWidget* namePlate_ = nullptr;
    QLabel* name_ = nullptr;
    QLineEdit* nameEditor_ = nullptr;
    QString fullName_;
    bool committingRename_ = false;
    bool cancellingRename_ = false;
    bool pendingStripDrag_ = false;
    bool draggingStripOrder_ = false;
    QPoint stripDragStartGlobal_;

    Kind kind_ = Kind::Insert;
    MixerRole role_ = MixerRole::Standard;
    std::size_t effectCount_ = 0;
    std::size_t sendCount_ = 0;
    int userFxSlotRows_ = 5;
    int shownFxSlots_ = 5;
    bool fxRackVisible_ = true;
    bool wideMode_ = false;
    bool mono_ = false;
    bool recordArmed_ = false;
    bool inputMonitored_ = false;
    bool hasAudioSource_ = false;
    bool hasInstrumentSource_ = false;
    QString instrumentName_;
    QString sourceLabel_;
    int outputTarget_ = 0;
    QString outputLabel_;
    std::optional<std::uint32_t> color_;
    bool selected_ = false;
};

} // namespace vloomdaw
