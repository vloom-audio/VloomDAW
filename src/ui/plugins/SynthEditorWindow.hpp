// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <vloom/dsp/instruments/Synth.hpp>

#include <QColor>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QToolButton;

namespace vloom {
class PluginInstance;
}

namespace vloomdaw {

class PresetBar;

/// The editor of the built-in wavetable synthesizer ("vloom.synth").
///
/// The header has the power button, the pages, the presets (the ones that
/// come with the app and the user's own) and the volume.
///
/// The Oscillators page is the synth at a glance. Its top row is the sound:
/// the sub oscillator, the noise, the two wavetable oscillators (each with a
/// picture of its table and where in it the oscillator reads) and the filter
/// (with its response; drag the point for cutoff and resonance). Under it is
/// what moves the sound: four macro knobs, the three envelopes and the four
/// LFOs (a tab each) and the voicing. A keyboard along the bottom plays the
/// synth by hand.
///
/// The Matrix page lists every source that moves something (envelopes 2 and
/// 3, the LFOs, the macros) with its target and amount. Deletes itself when
/// closed.
class SynthEditorWindow : public QWidget {
    Q_OBJECT

public:
    SynthEditorWindow(std::shared_ptr<vloom::PluginInstance> instance, const QString& title, QWidget* parent);
    ~SynthEditorWindow() override;

    /// Whether `instance` is the synthesizer this window edits.
    [[nodiscard]] static bool edits(const vloom::PluginInstance& instance);

public slots:
    /// Shows the instrument as switched off (true) or on. Does not emit bypassToggled().
    void setBypassed(bool bypassed);

signals:
    /// A parameter was changed: the project has unsaved changes.
    void edited();
    /// The user switched the instrument off (true) or on with the power button.
    void bypassToggled(bool bypassed);

protected:
    void hideEvent(QHideEvent* event) override;

private:
    /// How a knob's turn maps to its value.
    enum class Scale {
        Linear,
        Octaves, ///< equal turns double the value: frequencies
        Cubic, ///< fine at the low end: times
    };

    [[nodiscard]] double value(std::uint32_t id) const { return values_[id]; }
    /// Sets parameter `id` in the plugin; every control and picture follows.
    void change(std::uint32_t id, double value);
    void syncControls();
    /// Ends the note the keyboard holds, if any.
    void releaseKey();

    /// `arc`: the lit arc's colour; invalid: the accent.
    QWidget* knob(std::uint32_t id, const QString& caption, Scale scale, int dial, int cell,
        const QColor& arc = QColor());
    QWidget* toggle(std::uint32_t id, const QString& what);
    /// `colour`: its fill while on; invalid: the theme's.
    QWidget* flag(
        std::uint32_t id, const QString& text, const QString& what, const QColor& colour = QColor());
    QWidget* choice(std::uint32_t id, const QStringList& names, const QString& what);
    /// A row of tabs over pages made by `page` (one per tab).
    QWidget* tabbed(const QStringList& names, const std::function<QWidget*(int)>& page);

    QWidget* mainPage();
    QWidget* matrixPage();
    QWidget* sourceCard(const QString& title, bool noise, std::uint32_t first, std::uint32_t second,
        const QString& firstCaption, const QString& secondCaption);
    QWidget* oscillatorCard(const QString& letter, std::uint32_t onId, std::size_t hue);
    QWidget* filterCard();
    QWidget* macroCard();
    QWidget* envelopePage(int index);
    QWidget* lfoPage(int index);
    QWidget* voicingCard();

    std::shared_ptr<vloom::PluginInstance> instance_;
    std::array<double, vloom::dsp::Synth::kParameterCount> values_{};
    std::vector<std::function<void()>> syncs_; ///< one per control: shows its value
    std::vector<QWidget*> pictures_; ///< redrawn when a value changes
    int heldKey_ = -1; ///< the key the keyboard is playing; -1: none
    QToolButton* power_ = nullptr;
    PresetBar* presets_ = nullptr;
};

} // namespace vloomdaw
