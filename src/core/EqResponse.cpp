// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/EqResponse.hpp"

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

using Eq = vloom::dsp::Eq;

constexpr int kBlockFrames = 64; // the curves never process audio; any size does

} // namespace

EqSectionParameters eqSectionParameters(EqSection section) noexcept
{
    switch (section) {
    case EqSection::LowCut:
        return {.frequency = Eq::kLowCutFrequency, .gain = {}, .q = {}, .on = Eq::kLowCutOn};
    case EqSection::LowShelf:
        return {.frequency = Eq::kLowShelfFrequency, .gain = Eq::kLowShelfGain, .q = {}, .on = {}};
    case EqSection::LowMid:
        return {.frequency = Eq::kLowMidFrequency, .gain = Eq::kLowMidGain, .q = Eq::kLowMidQ, .on = {}};
    case EqSection::HighMid:
        return {.frequency = Eq::kHighMidFrequency, .gain = Eq::kHighMidGain, .q = Eq::kHighMidQ, .on = {}};
    case EqSection::HighShelf:
        return {.frequency = Eq::kHighShelfFrequency, .gain = Eq::kHighShelfGain, .q = {}, .on = {}};
    case EqSection::HighCut:
        break;
    }
    return {.frequency = Eq::kHighCutFrequency, .gain = {}, .q = {}, .on = Eq::kHighCutOn};
}

EqSection eqSectionOf(std::uint32_t id) noexcept
{
    if (id <= Eq::kLowCutFrequency)
        return EqSection::LowCut;
    if (id <= Eq::kLowShelfGain)
        return EqSection::LowShelf;
    if (id <= Eq::kLowMidQ)
        return EqSection::LowMid;
    if (id <= Eq::kHighMidQ)
        return EqSection::HighMid;
    if (id <= Eq::kHighShelfGain)
        return EqSection::HighShelf;
    return EqSection::HighCut;
}

const vloom::dsp::ParameterSpec& eqParameter(std::uint32_t id) noexcept
{
    const auto parameters = Eq::parameters();
    return parameters[std::min<std::size_t>(id, parameters.size() - 1)];
}

EqResponse::EqResponse(double sampleRate)
{
    for (const vloom::dsp::ParameterSpec& parameter : Eq::parameters())
        values_[parameter.id] = parameter.defaultValue;
    total_.prepare(sampleRate, kBlockFrames);
    for (Eq& section : sections_)
        section.prepare(sampleRate, kBlockFrames);
}

void EqResponse::set(std::uint32_t id, double value)
{
    if (id >= values_.size() || !std::isfinite(value))
        return;
    const vloom::dsp::ParameterSpec& parameter = eqParameter(id);
    value = std::clamp(value, parameter.minValue, parameter.maxValue);
    values_[id] = value;
    total_.setParameter(id, value);
    sections_[static_cast<std::size_t>(eqSectionOf(id))].setParameter(id, value);
}

double EqResponse::value(std::uint32_t id) const noexcept { return id < values_.size() ? values_[id] : 0.0; }

double EqResponse::totalDb(double frequency) const { return total_.magnitudeDb(frequency); }

double EqResponse::sectionDb(EqSection section, double frequency) const
{
    return sections_[static_cast<std::size_t>(section)].magnitudeDb(frequency);
}

} // namespace vloomdaw
