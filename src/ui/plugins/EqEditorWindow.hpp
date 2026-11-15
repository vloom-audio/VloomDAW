// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/EqResponse.hpp"
#include "core/SpectrumAnalyzer.hpp"

#include <QString>
#include <QWidget>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class QLabel;
class QTimer;
class QToolButton;

namespace vloom {
class PluginInstance;
}

namespace vloomdaw {

class EqGraph;
class Knob;
class PresetBar;
class ToggleSwitch;

/// The editor of the built-in equalizer ("vloom.eq").
///
/// The window is the response curve, over the spectrum of what the
/// equalizer puts out: a point per section, dragged sideways for frequency
/// and up or down for gain (wheel: Q, double-click: reset, or switch a cut).
/// A panel over the bottom of the curve has the selected section's knobs.
///
/// The header has the presets (the ones that come with the app and the
/// user's own, kept in the app's data folder) and two settings to compare,
/// A and B, which last as long as the window. Deletes itself when closed.
class EqEditorWindow : public QWidget {
    Q_OBJECT

public:
    /// `sampleRate`: the engine's, so the curve is right up to its top.
    EqEditorWindow(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate,
        QWidget* parent);
    ~EqEditorWindow() override;

    /// Whether `instance` is the equalizer this window edits.
    [[nodiscard]] static bool edits(const vloom::PluginInstance& instance);

public slots:
    /// Shows the effect as bypassed or on, without emitting bypassToggled().
    void setBypassed(bool bypassed);

signals:
    /// A parameter was changed: the project has unsaved changes.
    void edited();
    /// The user switched the effect off (true) or on with the power button.
    void bypassToggled(bool bypassed);

protected:
    /// Keeps the panel the size its controls need, centred on the graph.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    using Values = std::array<double, vloom::dsp::Eq::kParameterCount>;

    /// What A or B holds while the other one is heard.
    struct Snapshot {
        Values values{};
        int preset = -1; ///< as PresetBar::current(); -1: none ("Custom")
        bool modified = false; ///< changed since the preset was chosen
    };

    void select(EqSection section);
    /// Sets parameter `id` in the plugin and the curve; then the picture follows.
    void change(std::uint32_t id, double value);
    void rebuildPanel();
    void syncPanel();
    void placePanel();

    [[nodiscard]] Values values() const;
    void apply(const Values& values);
    void chooseSlot(int slot);
    /// Reads what the plugin put out and moves the spectrum.
    void updateSpectrum();

    std::shared_ptr<vloom::PluginInstance> instance_;
    EqResponse response_;
    EqSection selected_ = EqSection::LowMid;

    std::array<Snapshot, 2> slots_{}; ///< A and B; the one heard is stale until left
    int slot_ = 0;

    SpectrumAnalyzer analyzer_;
    std::vector<float> scope_; ///< the newest output, read each tick
    std::vector<float> lastScope_; ///< the tick before: the same again means no audio is running
    QTimer* spectrumTimer_ = nullptr;

    QToolButton* power_ = nullptr;
    PresetBar* presets_ = nullptr;
    std::array<QToolButton*, 2> slotButtons_{};
    EqGraph* graph_ = nullptr;
    QWidget* panel_ = nullptr;
    QWidget* badge_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* shape_ = nullptr;
    ToggleSwitch* on_ = nullptr;
    QWidget* knobs_ = nullptr; ///< holds the selected section's knobs
    Knob* frequency_ = nullptr;
    Knob* gain_ = nullptr; ///< null: the section has none
    Knob* q_ = nullptr; ///< null: the section has none
    QLabel* frequencyText_ = nullptr;
    QLabel* gainText_ = nullptr;
    QLabel* qText_ = nullptr;
};

} // namespace vloomdaw
