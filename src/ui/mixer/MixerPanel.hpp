// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/MixerModel.hpp"
#include "core/PluginSlot.hpp"
#include "ui/mixer/MixerStrip.hpp"

#include <QList>
#include <QStringList>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
class QScrollArea;
class QToolButton;

namespace vloomdaw {

class DetachButton;
class LevelMeter;

/// Studio One-inspired Mixer Console:
/// - Scrolling row of Audio, Instrument, Bus, FX and Aux channels on the left,
///   with the `Main` (Master) bus anchored on the right.
/// - Right-click anywhere on empty space or on a strip to add `Bus`, `FX`, or
///   `Aux` channels (or create a Bus/FX routed from the selected channel).
/// - Bottom console toolbar (`#mixerFooter`) with channel filters (`All`,
///   `Audio`, `Instruments`, `Bus / FX`, `With FX`), view toggles and batch actions.
class MixerPanel : public QWidget {
    Q_OBJECT

public:
    enum class StripFilter { All = 0, Audio = 1, Instrument = 2, BusFxAux = 3, Effects = 4 };

    MixerPanel(std::size_t insertCount, QWidget* parent = nullptr);

    [[nodiscard]] std::size_t trackCount() const { return static_cast<std::size_t>(strips_.size()); }
    [[nodiscard]] LevelMeter* meter(std::size_t track) const;
    void addHeaderWidget(QWidget* widget);
    void addInsert();
    void removeInsert(std::size_t track);
    void moveInsert(std::size_t from, std::size_t to);
    void setTrackName(std::size_t track, const QString& name);
    void setTrackColor(std::size_t track, std::optional<std::uint32_t> color);
    void setTrackRole(std::size_t track, MixerRole role);
    void setTrackMono(std::size_t track, bool mono);
    void setTrackRecordArmed(std::size_t track, bool armed);
    void setTrackInputMonitored(std::size_t track, bool monitored);
    void startTrackRename(std::size_t track);
    void setSelectedTrack(std::size_t track);
    [[nodiscard]] std::size_t selectedTrack() const { return selectedTrack_; }
    void setTrackEffects(std::size_t track, const std::vector<PluginSlot>& effects);
    void setTrackSends(std::size_t track, const std::vector<MixerSendDisplay>& sends);
    void setTrackChannelRouting(std::size_t track, bool hasAudio, bool hasInstrument,
        const QString& instrumentName, const QString& sourceLabel, int outputTarget,
        const QString& outputLabel);
    void setTrackSourceFlags(std::size_t track, bool hasAudio, bool hasInstrument);
    void setCanPasteChannelSettings(bool canPaste) { canPasteChannelSettings_ = canPaste; }
    void resetAllPeaks();
    void setAllEffectsBypassed(bool bypassed);

    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] QSize sizeHint() const override;

public slots:
    void setSilenced(std::size_t track, bool silenced);
    void setEffectCount(std::size_t track, std::size_t count);
    void setTrackValues(std::size_t track, double fader, double pan, bool muted, bool soloed);
    void setDetached(bool detached);

signals:
    void trackSelected(std::size_t track);
    void faderChanged(std::size_t track, double position);
    void panChanged(std::size_t track, double pan);
    void muteToggled(std::size_t track, bool muted);
    void soloToggled(std::size_t track, bool soloed);
    void monoToggled(std::size_t track, bool mono);
    void recordArmInsertToggled(std::size_t track, bool armed);
    void monitorInsertToggled(std::size_t track, bool monitored);
    void closeRequested();
    void detachRequested();
    void addInsertRequested();
    void addChannelRoleRequested(vloomdaw::MixerRole role);
    void addBusForTrackRequested(std::size_t track);
    void addFxSendForTrackRequested(std::size_t track);
    void addTracksDialogRequested();
    void addAudioTrackRequested();
    void addInstrumentTrackRequested();
    void copyChannelSettingsRequested(std::size_t track);
    void pasteChannelSettingsRequested(std::size_t track);
    void removeInsertRequested(std::size_t track);
    void renameRequested(std::size_t track);
    void trackNameEdited(std::size_t track, const QString& name);
    void moveInsertRequested(std::size_t from, std::size_t to);
    void colorChanged(std::size_t track, std::optional<std::uint32_t> color);
    void effectsMenuRequested(std::size_t track, const QPoint& globalPos);
    void effectBypassToggled(std::size_t track, std::size_t slot, bool bypassed);
    void effectClicked(std::size_t track, std::size_t slot);
    void effectMenuRequested(std::size_t track, std::size_t slot, const QPoint& globalPos);
    void pluginDroppedOnSlot(std::size_t track, std::size_t slot, std::size_t plugin);
    void pluginDropped(std::size_t track, std::size_t plugin);

    // Source / Instrument / Output / Sends signals
    void instrumentClicked(std::size_t track);
    void instrumentMenuRequested(std::size_t track, const QPoint& globalPos);
    void sourceClicked(std::size_t track, const QPoint& globalPos);
    void outputRoutingRequested(std::size_t track, const QPoint& globalPos);
    void addSendMenuRequested(std::size_t track, const QPoint& globalPos);
    void sendLevelChanged(std::size_t track, std::size_t sendSlot, double level);
    void sendMuteToggled(std::size_t track, std::size_t sendSlot, bool muted);
    void sendPreFaderToggled(std::size_t track, std::size_t sendSlot, bool preFader);
    void sendSidechainToggled(std::size_t track, std::size_t sendSlot, bool sidechain);
    void sendMenuRequested(std::size_t track, std::size_t sendSlot, const QPoint& globalPos);

    void clearAllSoloRequested();
    void clearAllMuteRequested();
    void bypassAllEffectsToggled(bool bypassed);
    void audioIoSetupRequested();

private:
    void connectStrip(MixerStrip* strip);
    [[nodiscard]] MixerStrip* makeInsertStrip(std::size_t track);
    void populateAddChannelActions(QMenu& menu);
    void showEmptySpaceContextMenu(const QPoint& globalPos);
    void showStripContextMenu(MixerStrip* strip, const QPoint& globalPos);
    void applyStripFilter();
    void updateSelectedLabel();

    QList<MixerStrip*> strips_;
    DetachButton* detach_ = nullptr;
    QToolButton* close_ = nullptr;
    QHBoxLayout* header_ = nullptr;
    QWidget* inserts_ = nullptr; ///< the scrolling row of insert / bus / fx / aux strips
    QWidget* dragIndicator_ = nullptr;
    int dragTargetSlot_ = -1;
    QHBoxLayout* insertRow_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QStringList names_; ///< [i] the name of track i ("" = "Insert i")
    std::size_t selectedTrack_ = 0;

    StripFilter filter_ = StripFilter::All;
    bool wideMode_ = false;
    bool showFxRack_ = true;
    int fxSlotRows_ = 5;
    bool canPasteChannelSettings_ = false;
    QLabel* selectedLabel_ = nullptr;
    QPushButton* bypassAllFxBtn_ = nullptr;
};

} // namespace vloomdaw
