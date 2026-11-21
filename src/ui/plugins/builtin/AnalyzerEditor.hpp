// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/AnalyzerSpectrum.hpp"
#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"

#include <QElapsedTimer>

#include <vector>

class QToolButton;

namespace vloomdaw {

class AnalyzerDisplay;

/// The editor of the built-in spectrum analyzer (vloom.analyzer), after the
/// approved design: the spectrum from 20 Hz to 20 kHz on a log axis, filled,
/// with a dB grid, the peak-hold line and a cursor readout (frequency and
/// level under the mouse); Freeze in the header; Speed, Tilt, Range and Peak
/// hold in the panel; footer: Resolution, Channel and the peak.
///
/// The processor passes the sound through (vloom-dsp ADR 0005); the
/// spectrum is worked out here from the instance's scope (AnalyzerSpectrum:
/// dsp::Fft, averaged over Analyzer::averagingSeconds(Speed), tilted by
/// Analyzer::tiltDb()). The footer's peak is the processor's `peak_db`
/// readout. Double-click the display to clear the held line.
///
/// Channel picks the side the spectrum is of (the stereo scope,
/// PluginInstance::readScopeStereo(); Left + right: their average) and the
/// processor's peak readout follows it too.
///
/// Difference from the design, from what the engine taps: the scope keeps
/// PluginInstance::kScopeFrames (8192) frames, so Resolution 16384 is
/// analysed at 8192 (its tooltip says so).
class AnalyzerEditor : public BuiltinEditorWindow {
    Q_OBJECT

public:
    AnalyzerEditor(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent = nullptr);

protected:
    void parameterChanged(std::uint32_t id, double value) override;
    void meter(const Meter& reading) override;

private:
    void applyResolution(double resolution);

    AnalyzerSpectrum spectrum_;
    std::vector<float> left_; ///< the scope's newest frames
    std::vector<float> right_;
    std::vector<float> signal_; ///< the side Channel picks, analysed
    QElapsedTimer sinceAnalysis_;
    AnalyzerDisplay* display_ = nullptr;
    QToolButton* freeze_ = nullptr;
    Readout* resolution_ = nullptr;
    Readout* peak_ = nullptr;
    int peakIndex_ = -1; ///< the processor's "peak_db" readout; -1: none
};

} // namespace vloomdaw
