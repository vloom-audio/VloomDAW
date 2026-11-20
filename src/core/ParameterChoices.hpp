// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <vloom/engine/plugin/AudioProcessor.hpp>

#include <string>

namespace vloomdaw {

/// A stepped parameter whose values have names (vloom::ParameterInfo::
/// valueLabels: a mode, a curve, a note length), shown as a list to pick
/// from rather than a knob or a switch.
[[nodiscard]] bool isChoice(const vloom::ParameterInfo& parameter) noexcept;

/// The index into valueLabels of `value` (rounded and kept in range); 0 for
/// a parameter that is not a choice.
[[nodiscard]] int choiceIndex(const vloom::ParameterInfo& parameter, double value) noexcept;

/// The value of the choice at `index` (kept in range).
[[nodiscard]] double choiceValue(const vloom::ParameterInfo& parameter, int index) noexcept;

/// The name of `value`, e.g. "1/8 D"; empty for a parameter that is not a choice.
[[nodiscard]] std::string choiceLabel(const vloom::ParameterInfo& parameter, double value);

} // namespace vloomdaw
