// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

#include <array>

class QTimer;

namespace vloomdaw {

class SampleWaveforms;
class SamplerDisplay;

/// The editor of the built-in sampler ("vloom.sampler"), after the design
/// (Sampler.dc.html).
///
/// The display is the sample: its waveform (read from the file the slot
/// plays), the time along it, Start, the loop (Loop Start to Loop End,
/// filled while Loop Mode is on) as markers to drag, the sample's end, and
/// the amp envelope over it at the root key. Drop a file on it, or click it
/// while it is empty, to load one.
///
/// The panel has three pages. Amp is the design's row: Pitch (Transpose),
/// Fine, Attack, Decay, Sustain, Release, Cutoff and Gain. Filter: type,
/// cutoff, resonance, envelope amount, key tracking and the filter's
/// envelope. Play: Start, velocity, loop crossfade, Mono, Legato, Glide,
/// Voices and Pan. The footer: the sample (click to replace it, × to
/// remove it), Root Key, the key range, Loop Mode and Voices.
///
/// Differences from the design: the dsp has no end point (the End marker
/// is the sample's end and does not move); "Pitch" is Transpose in whole
/// semitones; Loop reads Off / Forward / Ping-pong; the parameters the
/// design has no room for are on the Filter and Play pages and in the footer.
class SamplerEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    SamplerEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildPanel();
    void showPage(int page);
    /// Loads `path` into the sample slot (the shared loading of
    /// ui/plugins/SampleFiles.hpp).
    void load(const QString& path);
    void chooseFile();
    void clearSample();
    /// Reads what the slot holds: the display, the footer, the waveform.
    void refreshSlot();

    SamplerDisplay* display_ = nullptr;
    SampleWaveforms* waveforms_ = nullptr;
    Readout* sample_ = nullptr;
    QWidget* clear_ = nullptr;
    std::array<QWidget*, 3> pages_{};
    std::array<QWidget*, 3> tabs_{};
    QTimer* watch_ = nullptr; ///< a sample loaded elsewhere (the channel rack, an undo) shows too
};

} // namespace vloomdaw
