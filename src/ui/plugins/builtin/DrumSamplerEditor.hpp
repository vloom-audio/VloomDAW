// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

#include <vloom/dsp/instruments/DrumSampler.hpp>

#include <QStringList>

#include <array>

class QLabel;
class QStackedWidget;
class QTimer;

namespace vloomdaw {

class PadGrid;
class PadWaveform;
class SampleWaveforms;
class SmallButton;

/// The editor of the built-in drum sampler ("vloom.drums"), after the design
/// (DrumSampler.dc.html).
///
/// Left, the 16 pads, four by four from pad 1 at the top left: each shows
/// its key and its sample's name. Click a pad to choose it (an empty one
/// asks for a file too); drop files on a pad to load them into it and the
/// pads after it. Right, the chosen pad: its name and key, Load and Clear,
/// its waveform (reversed when it plays reversed, dimmed before Start) with
/// the decay drawn over it (drag the point), its knobs (Pitch for Tune,
/// Fine, Decay, Pan, Gain) and its readouts (Key, Choke group, Mode,
/// Reverse, Start, Release, Velocity). Footer: the chosen pad's file, how
/// many pads have one, and the master gain.
///
/// Differences from the design: the dsp has no per-pad mute, solo, filter
/// (the design's Cutoff) or output, so there are no M and S buttons, no
/// Cutoff and no Output readout; Fine, Key, Mode, Reverse, Start, Release
/// and Master are added. The pads are named after their files (the design's
/// "Kick", "Snare"... are made up). The presets set parameters only, never
/// samples, so there is no "Default kit" of sounds.
class DrumSamplerEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    using Drums = vloom::dsp::DrumSampler;

    DrumSamplerEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title,
        double sampleRate, QWidget* parent);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    /// Shows pad `pad` on the right.
    void select(int pad);
    /// The knobs and readouts of `pad`, made the first time it is chosen.
    void makeControls(int pad);
    /// Loads `paths` into `pad` and the pads after it (the shared loading of
    /// ui/plugins/SampleFiles.hpp).
    void load(int pad, const QStringList& paths);
    void chooseFile(int pad);
    void clearPad(int pad);
    /// Reads what every slot holds: the pads, the waveform, the footer.
    void refreshSlots();
    void refreshSelected();

    std::array<vloom::SampleSlotInfo, Drums::kPads> slots_{};
    int selected_ = 0;
    SampleWaveforms* waveforms_ = nullptr;
    PadGrid* grid_ = nullptr;
    PadWaveform* wave_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* where_ = nullptr;
    SmallButton* loadButton_ = nullptr;
    SmallButton* clearButton_ = nullptr;
    QStackedWidget* knobs_ = nullptr;
    QStackedWidget* readouts_ = nullptr;
    std::array<int, Drums::kPads> page_{}; ///< pad -> its page in knobs_ / readouts_; -1: not made yet
    Readout* sample_ = nullptr;
    Readout* loaded_ = nullptr;
    QTimer* watch_ = nullptr; ///< samples loaded elsewhere (the channel rack, an undo) show too
};

} // namespace vloomdaw
