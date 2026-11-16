// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/MixerModel.hpp"
#include "core/PluginSlot.hpp"

#include <QFrame>

#include <array>
#include <cstddef>
#include <vector>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class QWidget;

namespace vloomdaw {

class MixerSlotRow;

/// An FX slot rack displaying one plugin slot per effect an insert takes (MixerModel::kMaxEffects) for one
/// mixer strip. When the mixer panel is shrunk vertically, the rack compacts down to `kMinVisibleSlots` (4)
/// visible slots and scrolls smoothly with the mouse wheel so the strip's fader never gets crushed.
class MixerSlotRack : public QFrame {
    Q_OBJECT

public:
    static constexpr int kSlotHeight = 16;
    static constexpr int kMinVisibleSlots = 1;

    explicit MixerSlotRack(QWidget* parent = nullptr);

    /// Updates every slot with the track's effects.
    void setEffects(const std::vector<PluginSlot>& effects);

    /// Number of simultaneously visible FX rows (`kMinVisibleSlots .. MixerModel::kMaxEffects`).
    void setVisibleSlotCount(int count);
    [[nodiscard]] int visibleSlotCount() const { return visibleSlots_; }

signals:
    void bypassToggled(std::size_t slot, bool bypassed);
    void slotClicked(std::size_t slot);
    void slotMenuRequested(std::size_t slot, const QPoint& globalPos);
    void pluginDropped(std::size_t slot, std::size_t plugin);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateRowGeometry();
    void setScrollSlot(int slot);
    [[nodiscard]] int maxScrollSlot() const;

    QWidget* clipHost_ = nullptr;
    std::array<MixerSlotRow*, MixerModel::kMaxEffects> rows_{};
    int visibleSlots_ = static_cast<int>(MixerModel::kMaxEffects);
    int scrollSlot_ = 0;
    int wheelDeltaRemainder_ = 0;
    bool draggingScroll_ = false;
    int dragStartY_ = 0;
    int dragStartSlot_ = 0;
};

} // namespace vloomdaw
