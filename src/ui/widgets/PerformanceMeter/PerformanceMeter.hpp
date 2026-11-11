// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/widgets/LcdDisplay/LcdDisplay.hpp"

#include <cstdint>
#include <optional>

namespace vloomdaw {

/// LCD panel showing how hard the audio engine works (CPU %) and how much
/// memory the app uses (RAM MB), like the meters on FL Studio's tool bar.
class PerformanceMeter : public LcdDisplay {
    Q_OBJECT

public:
    explicit PerformanceMeter(QWidget* parent = nullptr);

public slots:
    /// Share of the audio block budget in use, 0..1 (may exceed 1 on overload).
    void setCpuLoad(float load);
    /// Memory in use; empty shows dashes.
    void setMemoryBytes(std::optional<std::uint64_t> bytes);
};

} // namespace vloomdaw
