// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/DisplayHelpers.hpp"

#include "ui/plugins/builtin/BuiltinEditorWindow.hpp"
#include "ui/plugins/builtin/EditorStyle.hpp"

#include <vloom/engine/plugin/AudioProcessor.hpp>

#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace vloomdaw::display {

namespace {

namespace style = editor_style;

} // namespace

std::vector<double> settingsOf(const BuiltinEditorWindow& editor, std::size_t count)
{
    std::vector<double> values(count);
    for (std::size_t id = 0; id < count; ++id)
        values[id] = editor.value(static_cast<std::uint32_t>(id));
    return values;
}

QStringList choiceLabels(const vloom::ParameterInfo* parameter)
{
    QStringList labels;
    if (parameter != nullptr)
        for (const std::string& label : parameter->valueLabels)
            labels << QString::fromStdString(label);
    return labels;
}

double xOfHz(double hz, double left, double width)
{
    const double clamped = std::clamp(std::isfinite(hz) ? hz : kLowHz, kLowHz, kHighHz);
    return left + width * std::log(clamped / kLowHz) / std::log(kHighHz / kLowHz);
}

double hzAtX(double x, double left, double width)
{
    const double share = width > 0.0 ? std::clamp((x - left) / width, 0.0, 1.0) : 0.0;
    return kLowHz * std::pow(kHighHz / kLowHz, share);
}

void drawFrequencyGrid(QPainter& p, const QRectF& area, double labelY)
{
    static constexpr std::array<std::pair<double, const char*>, 8> kLines{{
        {50.0, "50"},
        {100.0, "100"},
        {200.0, "200"},
        {500.0, "500"},
        {1000.0, "1k"},
        {2000.0, "2k"},
        {5000.0, "5k"},
        {10000.0, "10k"},
    }};
    p.save();
    p.setFont(style::font(p.font(), 10));
    for (const auto& [hz, label] : kLines) {
        const double x = std::round(xOfHz(hz, area.left(), area.width())) + 0.5;
        p.setPen(QPen(style::grid(), 1.0));
        p.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        p.setPen(style::muted());
        p.drawText(QPointF(x + 4.0, labelY), QString::fromLatin1(label));
    }
    p.restore();
}

QPainterPath wavePath(
    std::span<const float> samples, const QRectF& area, double centreY, double unitPixels, bool held)
{
    QPainterPath path;
    if (samples.empty())
        return path;
    const double step = area.width() / static_cast<double>(samples.size());
    const auto y = [&](float sample) {
        const double value = std::isfinite(sample) ? static_cast<double>(sample) : 0.0;
        return centreY - value * unitPixels;
    };
    path.moveTo(area.left(), y(samples.front()));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double x = area.left() + step * static_cast<double>(i);
        if (held) {
            path.lineTo(x, y(samples[i]));
            path.lineTo(x + step, y(samples[i]));
        } else {
            path.lineTo(x, y(samples[i]));
        }
    }
    return path;
}

} // namespace vloomdaw::display
