// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <vloom/dsp/effects/Eq.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vloomdaw {

/// The six sections of the built-in equalizer ("vloom.eq"), in the order
/// they sit along the spectrum.
enum class EqSection { LowCut, LowShelf, LowMid, HighMid, HighShelf, HighCut };
inline constexpr std::size_t kEqSections = 6;

/// The parameters of one section. A cut has a switch and no gain; a shelf
/// has no Q.
struct EqSectionParameters {
    std::uint32_t frequency = 0;
    std::optional<std::uint32_t> gain;
    std::optional<std::uint32_t> q;
    std::optional<std::uint32_t> on;
};

[[nodiscard]] EqSectionParameters eqSectionParameters(EqSection section) noexcept;
/// The section parameter `id` belongs to.
[[nodiscard]] EqSection eqSectionOf(std::uint32_t id) noexcept;
/// The range and default of parameter `id` (an id of vloom::dsp::Eq).
[[nodiscard]] const vloom::dsp::ParameterSpec& eqParameter(std::uint32_t id) noexcept;

/// What the equalizer's editor draws: the gain of the whole equalizer and of
/// each section alone at a frequency, for a set of parameter values.
///
/// Works the curves out with the equalizer's own code (vloom::dsp::Eq), so
/// the picture is what the plugin does. Not for the audio thread.
class EqResponse {
public:
    explicit EqResponse(double sampleRate);

    /// Sets parameter `id` (clamped to its range); other ids are ignored.
    void set(std::uint32_t id, double value);
    [[nodiscard]] double value(std::uint32_t id) const noexcept;

    /// Gain of the whole equalizer at `frequency` (Hz), in dB.
    [[nodiscard]] double totalDb(double frequency) const;
    /// Gain of `section` alone; 0 for a cut that is off.
    [[nodiscard]] double sectionDb(EqSection section, double frequency) const;

private:
    std::array<double, vloom::dsp::Eq::kParameterCount> values_{};
    vloom::dsp::Eq total_;
    std::array<vloom::dsp::Eq, kEqSections> sections_; ///< each with one section set, the rest flat
};

} // namespace vloomdaw
