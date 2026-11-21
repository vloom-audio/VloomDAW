// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "ui/plugins/builtin/Animation.hpp"
#include "ui/plugins/builtin/AnimationPaint.hpp"

#include <QWidget>

#include <array>
#include <cstddef>
#include <functional>

namespace vloomdaw {

/// A small inset that draws a transfer curve, output against input, over
/// the same range on both axes: a compressor's static curve in dB, a
/// saturator's shaping curve. The diagonal (no change) is dashed; a dot can
/// mark where the input is now. Floats over a display in the panel's look.
///
/// Animated (BuiltinEditorWindow::animate()): the dot glows with the level
/// and leaves a short trail fading behind it.
class TransferCurve : public QWidget, public Animated {
    Q_OBJECT

public:
    /// `curve` gives the output for an input; both run `minimum` .. `maximum`.
    TransferCurve(
        std::function<double(double)> curve, double minimum, double maximum, QWidget* parent = nullptr);

    /// The curve changed (a setting it depends on): draw it again.
    void refresh()
    {
        layer_.invalidate();
        update();
    }
    /// Marks `input` on the curve; NaN (or out of range): no dot.
    void setInput(double input);

    [[nodiscard]] QSize sizeHint() const override { return {124, 124}; }

    bool advance(const motion::AudioFeatures& audio, double dt) override;
    void rest() override;
    void animationSwitched(bool on) override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    /// The frame, the diagonal and the curve: kept in layer_.
    void paintCurve(QPainter& painter) const;

    std::function<double(double)> curve_;
    double minimum_;
    double maximum_;
    double input_;

    /// Where the dot was lately: the trail (animationOn()).
    struct Trace {
        double input = 0.0;
        double age = 0.0; ///< s
        bool used = false;
    };
    static constexpr std::size_t kTrail = 24;
    std::array<Trace, kTrail> trail_{};
    std::size_t trailNext_ = 0; ///< where the next goes
    double glow_ = 0.0;
    bool ticking_ = false; ///< frames come: they repaint, setInput() need not
    motion_paint::LayerCache layer_;
};

} // namespace vloomdaw
