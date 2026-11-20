// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QWidget>

#include <functional>

namespace vloomdaw {

/// A small inset that draws a transfer curve, output against input, over
/// the same range on both axes: a compressor's static curve in dB, a
/// saturator's shaping curve. The diagonal (no change) is dashed; a dot can
/// mark where the input is now. Floats over a display in the panel's look.
class TransferCurve : public QWidget {
    Q_OBJECT

public:
    /// `curve` gives the output for an input; both run `minimum` .. `maximum`.
    TransferCurve(
        std::function<double(double)> curve, double minimum, double maximum, QWidget* parent = nullptr);

    /// The curve changed (a setting it depends on): draw it again.
    void refresh() { update(); }
    /// Marks `input` on the curve; NaN (or out of range): no dot.
    void setInput(double input);

    [[nodiscard]] QSize sizeHint() const override { return {124, 124}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::function<double(double)> curve_;
    double minimum_;
    double maximum_;
    double input_;
};

} // namespace vloomdaw
