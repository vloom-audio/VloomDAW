// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/MeterBars.hpp"

#include "core/DynamicsMeter.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vloomdaw {

namespace {

namespace style = editor_style;

constexpr int kBarWidth = 10;
constexpr int kPaddingY = 12;
constexpr int kLabelHeight = 16;

} // namespace

MeterBars::MeterBars(std::vector<Bar> bars, double bottomDb, double reductionRangeDb, QWidget* parent)
    : QWidget(parent)
    , bars_(std::move(bars))
    , values_(bars_.size(), kNoLevel)
    , bottomDb_(bottomDb)
    , reductionRangeDb_(reductionRangeDb)
{
    setFixedWidth(sizeHint().width());
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void MeterBars::setValue(std::size_t index, double db)
{
    if (index >= values_.size() || (values_[index] == db))
        return;
    values_[index] = db;
    update();
}

void MeterBars::clear()
{
    std::fill(values_.begin(), values_.end(), kNoLevel);
    update();
}

void MeterBars::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), style::background());
    p.fillRect(QRect(0, 0, 1, height()), style::faint());
    if (bars_.empty())
        return;
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(style::font(font(), 10));

    const int count = static_cast<int>(bars_.size());
    const int inner = width() - 16;
    const int gap = count > 1 ? (inner - count * kBarWidth) / (count - 1) : 0;
    const int wellHeight = std::max(10, height() - 2 * kPaddingY - kLabelHeight);
    for (int i = 0; i < count; ++i) {
        const Bar& bar = bars_[static_cast<std::size_t>(i)];
        const double value = values_[static_cast<std::size_t>(i)];
        const QRectF well(8 + i * (kBarWidth + gap), kPaddingY, kBarWidth, wellHeight);
        QPainterPath clip;
        clip.addRoundedRect(well, 3, 3);
        p.save();
        p.setClipPath(clip);
        p.fillRect(well, style::faint());
        if (std::isfinite(value)) {
            double share = 0.0;
            if (bar.fromTop)
                share = std::clamp(value / reductionRangeDb_, 0.0, 1.0);
            else
                share = std::clamp((value - bottomDb_) / -bottomDb_, 0.0, 1.0);
            const double filled = well.height() * share;
            const QColor colour
                = bar.colour.isValid() ? bar.colour : (bar.fromTop ? style::accent() : style::level());
            p.fillRect(bar.fromTop ? QRectF(well.left(), well.top(), well.width(), filled)
                                   : QRectF(well.left(), well.bottom() - filled, well.width(), filled),
                colour);
        }
        p.restore();
        p.setPen(style::muted());
        p.drawText(
            QRectF(well.center().x() - 16, well.bottom() + 2, 32, kLabelHeight), Qt::AlignCenter, bar.label);
    }
}

} // namespace vloomdaw
