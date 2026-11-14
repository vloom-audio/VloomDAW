// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/MixerModel.hpp"
#include "core/PluginSlot.hpp"

#include <QFrame>

#include <array>
#include <cstddef>
#include <vector>

namespace vloomdaw {

class MixerSlotRow;

/// An FX slot rack displaying 10 vertical plugin slots for one mixer strip,
/// matching FL Studio's insert slot rack. Each row features a left switch
/// button (green ▶ when active, dim ▶ when bypassed, ■ when empty) and
/// an effect slot button displaying the effect name (or empty dash).
class MixerSlotRack : public QFrame {
    Q_OBJECT

public:
    explicit MixerSlotRack(QWidget* parent = nullptr);

    /// Updates all 10 slots with the track's effects.
    void setEffects(const std::vector<PluginSlot>& effects);

signals:
    void bypassToggled(std::size_t slot, bool bypassed);
    void slotClicked(std::size_t slot);
    void slotMenuRequested(std::size_t slot, const QPoint& globalPos);
    void pluginDropped(std::size_t slot, std::size_t plugin);

private:
    std::array<MixerSlotRow*, MixerModel::kMaxEffects> rows_{};
};

} // namespace vloomdaw
