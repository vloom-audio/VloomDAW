// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>

#include <string_view>

namespace vloomdaw {

/// How a parameter value reads with its unit: "80.0 Hz", "2.50 kHz",
/// "+3.0 dB", "1.00".
[[nodiscard]] QString parameterValueText(std::string_view unit, double value);

} // namespace vloomdaw
