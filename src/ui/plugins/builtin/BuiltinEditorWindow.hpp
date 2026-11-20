// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/DynamicsMeter.hpp"
#include "core/ParameterScale.hpp"
#include "ui/plugins/builtin/PillToggle.hpp"

#include <vloom/engine/plugin/AudioProcessor.hpp>

#include <QElapsedTimer>
#include <QString>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QTimer;
class QToolButton;

namespace vloom {
class PluginInstance;
}

namespace vloomdaw {

class ArcKnob;
class ControlPanel;
class FooterBar;
class PresetBar;
class Readout;

/// The shell every dedicated editor of a built-in effect is built on, after
/// the approved designs (docs/builtin-editors.md says how to add one):
///
/// - a 40 px header: the bypass button, the plugin's name, the presets
///   (‹ name › over PresetBar: the ones the editor brings, after "Default",
///   and the user's own) and two settings to compare, A and B;
/// - the display, which the subclass gives (setDisplay()) and which fills
///   the window: the plugin's own picture;
/// - a floating panel of knobs and switches (panel()), centred at the bottom
///   of the display, as wide as they need;
/// - a 32 px footer of readouts (footer()).
///
/// The shell owns the parameter values. Controls made with knob(), toggle()
/// and readout() are bound to a parameter: they show its value and every
/// change, from any of them, from a preset or from A/B, goes through
/// change(), which sets the plugin (PluginInstance::setParameter(), as
/// ParameterEditorWindow does), updates every control bound to it, calls
/// parameterChanged() and emits edited() for the project's unsaved changes.
///
/// startMetering() gives the subclass meter() about 30 times a second while
/// the window shows: the level of what the plugin puts out (its scope tap)
/// and the gain reduction it reports. Deletes itself when closed.
class BuiltinEditorWindow : public QWidget {
    Q_OBJECT

public:
    ~BuiltinEditorWindow() override;

    /// The plugin's parameter `id`; null for an id it does not have.
    [[nodiscard]] const vloom::ParameterInfo* parameter(std::uint32_t id) const;
    /// The value of parameter `id` now (0 for an unknown id).
    [[nodiscard]] double value(std::uint32_t id) const;
    [[nodiscard]] bool isBypassed() const { return bypassed_; }

public slots:
    /// Shows the effect as bypassed or on, without emitting bypassToggled().
    void setBypassed(bool bypassed);

signals:
    /// A parameter was changed: the project has unsaved changes.
    void edited();
    /// The user switched the effect off (true) or on with the power button.
    void bypassToggled(bool bypassed);

protected:
    /// A preset the editor brings: the values that differ from the defaults.
    struct Preset {
        QString name;
        std::vector<std::pair<std::uint32_t, double>> values;
    };

    /// A reading of the plugin for the meters (startMetering()).
    struct Meter {
        /// Audio went through since the last reading. False: the levels below
        /// are kNoLevel (nothing is playing, or the plugin is not in use).
        bool running = false;
        /// The peak of what the plugin put out since the last reading (left
        /// and right averaged, the dry sound while bypassed), dB full scale.
        double outputDb = kNoLevel;
        /// How far it turned the sound down (AudioProcessor::gainReductionDb(),
        /// >= 0): 0 while bypassed, kNoLevel for a plugin that reports none.
        double reductionDb = kNoLevel;
    };

    /// `presets` follow "Default" (every parameter at its default value).
    /// `sampleRate`: the engine's. The plugin's parameter ids must run 0 .. n - 1
    /// (as every built-in's do). Sized 900 x 480 unless the subclass resizes it.
    BuiltinEditorWindow(std::shared_ptr<vloom::PluginInstance> instance, const QString& title,
        double sampleRate, std::vector<Preset> presets, QWidget* parent);

    [[nodiscard]] vloom::PluginInstance& instance() const { return *instance_; }
    [[nodiscard]] double sampleRate() const { return sampleRate_; }

    // --- Layout ---------------------------------------------------------
    /// Puts `display` in the window, behind the panel; it stretches with it.
    void setDisplay(QWidget* display);
    /// The floating panel: add knobs and switches with ControlPanel::add().
    [[nodiscard]] ControlPanel* panel() const { return panel_; }
    /// The footer: FooterBar::addLeft() / addRight().
    [[nodiscard]] FooterBar* footer() const { return footer_; }

    // --- Controls bound to a parameter ------------------------------------
    /// A knob for parameter `id`. `label`: its name unless given. `scale`:
    /// as its unit suggests (defaultScale()) unless given. A choice steps
    /// through its names.
    ArcKnob* knob(
        std::uint32_t id, const QString& label = {}, std::optional<ParameterScale> scale = std::nullopt);
    /// A pill switch for parameter `id` (a switch: 0 off, 1 on).
    PillToggle* toggle(
        std::uint32_t id, const QString& label = {}, PillToggle::Layout layout = PillToggle::Layout::Stacked);
    /// A footer reading of parameter `id` that turns like a knob (a choice:
    /// click to step).
    Readout* readout(
        std::uint32_t id, const QString& label = {}, std::optional<ParameterScale> scale = std::nullopt);
    /// A footer reading of something else (a meter, a state): set its text
    /// with Readout::setText().
    Readout* readout(const QString& label);
    /// Calls `show` with parameter `id`'s value now and after every change:
    /// for a display (a marker line, a curve) that follows it.
    void bind(std::uint32_t id, std::function<void(double)> show);

    // --- Changes -----------------------------------------------------------
    /// Sets parameter `id` to `value` (kept in its range and steps): the
    /// plugin, the bound controls, the preset's star, edited(). Nothing if
    /// it does not change. For displays that edit (a dragged threshold).
    void change(std::uint32_t id, double value);
    /// Parameter `id` changed to `value` (by any control, a preset or A/B),
    /// after the bound controls were updated.
    virtual void parameterChanged(std::uint32_t id, double value);

    // --- Meters --------------------------------------------------------------
    /// Starts the readings: meter() runs about 30 times a second while the
    /// window shows, and the plugin's scope tap is on only then.
    void startMetering();
    /// A reading (startMetering()).
    virtual void meter(const Meter& reading);

    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// What A or B holds while the other one is heard.
    struct Snapshot {
        std::vector<double> values;
        int preset = -1; ///< as PresetBar::current(); -1: none ("Custom")
        bool modified = false; ///< changed since the preset was chosen
    };

    void buildHeader(std::vector<Preset> presets);
    void applyAll(const std::vector<double>& values);
    void chooseSlot(int slot);
    void placePanel();
    void readMeters();

    std::shared_ptr<vloom::PluginInstance> instance_;
    double sampleRate_;
    std::vector<vloom::ParameterInfo> parameters_; ///< by id
    std::vector<double> values_; ///< by id
    std::vector<std::pair<std::uint32_t, std::function<void(double)>>> shows_;
    bool bypassed_ = false;

    std::array<Snapshot, 2> slots_{}; ///< A and B; the one heard is stale until left
    int slot_ = 0;

    QToolButton* power_ = nullptr;
    PresetBar* presets_ = nullptr;
    std::array<QToolButton*, 2> slotButtons_{};
    QWidget* body_ = nullptr; ///< holds the display, with the panel over it
    QWidget* display_ = nullptr;
    ControlPanel* panel_ = nullptr;
    FooterBar* footer_ = nullptr;

    bool metering_ = false;
    QTimer* meterTimer_ = nullptr;
    QElapsedTimer sinceReading_;
    std::vector<float> scope_; ///< the newest output, read each tick
    std::vector<float> lastTail_; ///< the end of the last read: the same again means no audio is running
};

} // namespace vloomdaw
