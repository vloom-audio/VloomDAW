// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/TunerReading.hpp"
#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

#include <QElapsedTimer>

#include <optional>

class QTimer;

namespace vloomdaw {

class TunerDisplay;

/// The editor of the built-in tuner (vloom.tuner), after the approved
/// design: the note with its octave, the frequency, a ±50 cent meter with
/// the in-tune zone (kInTuneCents) and the cents off it; Reference and Mute
/// output in the panel; footer: the input channel (a choice) and its level.
///
/// It reads the tuner's readouts (AudioProcessor::readoutValue(): frequency,
/// note, cents, pitch flag, level) about 30 times a second while it shows,
/// and smooths the needle a little (NeedleSmoother). No pitch, or the
/// tuner bypassed: "—". The display animates with the readings (and the
/// tuner's input level counts as sound, since it mutes its output by
/// default). Differences from the design: the needle turns the
/// level colour while in tune.
class TunerEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    TunerEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    [[nodiscard]] std::optional<double> inputPeakDb() const override;

private:
    void poll();
    [[nodiscard]] float readoutAt(int index) const;

    TunerDisplay* display_ = nullptr;
    Readout* level_ = nullptr;
    QTimer* timer_ = nullptr;
    QElapsedTimer sincePoll_;
    NeedleSmoother needle_;
    float lastLevel_ = 0.0f; ///< the level readout last poll
    double unchanged_ = 0.0; ///< s it has not changed: the tuner stopped running
    // Readout indices, looked up by id once; -1: the tuner has none.
    int frequencyIndex_ = -1;
    int noteIndex_ = -1;
    int centsIndex_ = -1;
    int pitchIndex_ = -1;
    int levelIndex_ = -1;
};

} // namespace vloomdaw
