// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

#include <vector>

namespace vloomdaw {

/// A column of thin upright meters beside a display, each with a short
/// label under it: "In", "GR", "Out" on the compressor. A level meter fills
/// from the bottom; a reduction meter hangs from the top.
class MeterBars : public QWidget {
    Q_OBJECT

public:
    struct Bar {
        QString label;
        bool fromTop = false; ///< a reduction: hangs from the top
        QColor colour; ///< invalid: the level colour (or the accent, from the top)
    };

    /// `bars` left to right; levels between `bottomDb` and 0 dB full scale,
    /// reductions between 0 and `reductionRangeDb`.
    MeterBars(std::vector<Bar> bars, double bottomDb, double reductionRangeDb, QWidget* parent = nullptr);

    /// Shows `db` on bar `index`: a level in dB full scale, or a reduction
    /// (>= 0). kNoLevel or -infinity: empty.
    void setValue(std::size_t index, double db);
    /// Empties every bar.
    void clear();

    [[nodiscard]] QSize sizeHint() const override { return {64, 200}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<Bar> bars_;
    std::vector<double> values_;
    double bottomDb_;
    double reductionRangeDb_;
};

} // namespace vloomdaw
