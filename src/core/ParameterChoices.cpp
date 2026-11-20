// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ParameterChoices.hpp"

#include <algorithm>
#include <cmath>

namespace vloomdaw {

bool isChoice(const vloom::ParameterInfo& parameter) noexcept
{
    return parameter.stepped && !parameter.valueLabels.empty() && parameter.maxValue >= parameter.minValue
        && static_cast<double>(parameter.valueLabels.size()) == parameter.maxValue - parameter.minValue + 1.0;
}

int choiceIndex(const vloom::ParameterInfo& parameter, double value) noexcept
{
    if (!isChoice(parameter) || !std::isfinite(value))
        return 0;
    const double index
        = std::round(std::clamp(value, parameter.minValue, parameter.maxValue) - parameter.minValue);
    return std::clamp(static_cast<int>(index), 0, static_cast<int>(parameter.valueLabels.size()) - 1);
}

double choiceValue(const vloom::ParameterInfo& parameter, int index) noexcept
{
    const int last = isChoice(parameter) ? static_cast<int>(parameter.valueLabels.size()) - 1 : 0;
    return parameter.minValue + static_cast<double>(std::clamp(index, 0, last));
}

std::string choiceLabel(const vloom::ParameterInfo& parameter, double value)
{
    if (!isChoice(parameter))
        return {};
    return parameter.valueLabels[static_cast<std::size_t>(choiceIndex(parameter, value))];
}

} // namespace vloomdaw
