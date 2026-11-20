// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/AudioFeatures.hpp"

#include <algorithm>
#include <cmath>

namespace vloomdaw::motion {

namespace {

constexpr double kPulseSeconds = 0.15; ///< time constant of pulse()'s fall
constexpr double kReductionSeconds = 0.03;
constexpr double kBandFloorDb = -90.0;
constexpr double kLowestHz = 30.0;
constexpr double kHighestHz = 16000.0;
constexpr double kSilentDb = -120.0; ///< what silence counts as in an average of levels

} // namespace

double smoothingFactor(double dt, double tau) noexcept
{
    if (!(dt > 0.0))
        return 0.0;
    if (!(tau > 0.0))
        return 1.0;
    return 1.0 - std::exp(-dt / tau);
}

double approach(double current, double target, double dt, double tau) noexcept
{
    return current + (target - current) * smoothingFactor(dt, tau);
}

double decayFactor(double dt, double seconds, double db) noexcept
{
    if (!(dt > 0.0))
        return 1.0;
    if (!(seconds > 0.0))
        return 0.0;
    return std::pow(10.0, -db / 20.0 * dt / seconds);
}

double normalisedLevel(double db, double floorDb) noexcept
{
    if (!std::isfinite(db) || !(floorDb < 0.0))
        return db > 0.0 && std::isinf(db) ? 1.0 : 0.0;
    return std::clamp(1.0 - db / floorDb, 0.0, 1.0);
}

// --- EnvelopeFollower ----------------------------------------------------------

EnvelopeFollower::EnvelopeFollower(double attackSeconds, double releaseSeconds) noexcept
    : attack_(attackSeconds)
    , release_(releaseSeconds)
{
}

void EnvelopeFollower::setTimes(double attackSeconds, double releaseSeconds) noexcept
{
    attack_ = attackSeconds;
    release_ = releaseSeconds;
}

double EnvelopeFollower::step(double target, double dt) noexcept
{
    if (!std::isfinite(target))
        target = 0.0;
    value_ = approach(value_, target, dt, target > value_ ? attack_ : release_);
    return value_;
}

// --- OnsetDetector -------------------------------------------------------------

OnsetDetector::OnsetDetector() noexcept
    : OnsetDetector(Settings{})
{
}

OnsetDetector::OnsetDetector(Settings settings) noexcept
    : settings_(settings)
    , averageDb_(kSilentDb)
{
}

bool OnsetDetector::step(double levelDb, double dt) noexcept
{
    const double level = std::isfinite(levelDb) ? std::max(levelDb, kSilentDb) : kSilentDb;
    sinceOnset_ += std::max(0.0, dt);
    const double rise = level - averageDb_;
    const bool onset
        = level > settings_.floorDb && rise >= settings_.riseDb && sinceOnset_ >= settings_.holdSeconds;
    averageDb_ = approach(averageDb_, level, dt, settings_.averageSeconds);
    if (onset) {
        sinceOnset_ = 0.0;
        strength_ = std::clamp(rise / (settings_.riseDb * 3.0), 0.0, 1.0);
        averageDb_ = level; // a held note after it is not another onset
    }
    return onset;
}

void OnsetDetector::reset() noexcept
{
    averageDb_ = kSilentDb;
    sinceOnset_ = std::numeric_limits<double>::infinity();
    strength_ = 0.0;
}

// --- AudioFeatures -------------------------------------------------------------

AudioFeatures::AudioFeatures()
    : wave_(kWaveFrames, 0.0F)
{
}

AudioFeatures::~AudioFeatures() = default;

void AudioFeatures::enableSpectrum(double sampleRate, std::size_t bands)
{
    bands_.assign(bands, 0.0);
    bandEdges_.clear();
    if (bands == 0 || !(sampleRate > 0.0)) {
        spectrum_.reset();
        bands_.clear();
        return;
    }
    spectrum_ = std::make_unique<SpectrumAnalyzer>(sampleRate);
    const double top = std::min(kHighestHz, sampleRate * 0.45);
    for (std::size_t i = 0; i <= bands; ++i)
        bandEdges_.push_back(
            kLowestHz * std::pow(top / kLowestHz, static_cast<double>(i) / static_cast<double>(bands)));
}

void AudioFeatures::update(const AudioInput& input, double dt)
{
    dt = std::clamp(dt, 0.0, 0.25);
    running_ = input.running && !input.mono.empty();
    onset_ = false;

    if (running_) {
        float peak = 0.0F;
        double squares = 0.0;
        for (const float sample : input.mono) {
            peak = std::max(peak, std::abs(sample));
            squares += static_cast<double>(sample) * static_cast<double>(sample);
        }
        const double mean = squares / static_cast<double>(input.mono.size());
        peakDb_ = peak > 0.0F ? 20.0 * std::log10(static_cast<double>(peak))
                              : -std::numeric_limits<double>::infinity();
        rmsDb_ = mean > 0.0 ? 10.0 * std::log10(mean) : -std::numeric_limits<double>::infinity();

        // The newest samples at the end of the waveform.
        const std::span<const float> fresh = input.mono.last(std::min(input.mono.size(), kWaveFrames));
        const auto keep = static_cast<std::ptrdiff_t>(kWaveFrames - fresh.size());
        std::copy(wave_.end() - keep, wave_.end(), wave_.begin());
        std::copy(fresh.begin(), fresh.end(), wave_.begin() + keep);
    } else {
        if (std::isfinite(peakDb_))
            std::fill(wave_.begin(), wave_.end(), 0.0F); // the sound is gone, once
        peakDb_ = -std::numeric_limits<double>::infinity();
        rmsDb_ = -std::numeric_limits<double>::infinity();
    }

    const bool sounding = running_ && peakDb_ > kSilenceDb;
    silent_ = sounding ? 0.0 : std::min(silent_ + dt, kSettleSeconds * 4.0);

    level_.step(normalisedLevel(peakDb_), dt);
    // An RMS reads 3 dB under the peak of a sine: lift it so a sine reads its peak.
    envelope_.step(normalisedLevel(rmsDb_ + 3.0), dt);
    onset_ = onsets_.step(peakDb_, dt);
    pulse_ *= std::exp(-dt / kPulseSeconds);
    if (onset_)
        pulse_ = std::max(pulse_, onsets_.strength());

    const double reduction = std::isfinite(input.reductionDb) ? std::max(0.0, input.reductionDb) : 0.0;
    reduction_ = approach(reduction_, running_ ? reduction : 0.0, dt, kReductionSeconds);

    if (spectrum_ && !settled()) {
        spectrum_->analyse(running_ ? std::span<const float>(wave_) : std::span<const float>());
        for (std::size_t i = 0; i < bands_.size(); ++i)
            bands_[i] = normalisedLevel(spectrum_->levelDb(bandEdges_[i], bandEdges_[i + 1]), kBandFloorDb);
    }
}

void AudioFeatures::reset()
{
    peakDb_ = -std::numeric_limits<double>::infinity();
    rmsDb_ = -std::numeric_limits<double>::infinity();
    level_.reset();
    envelope_.reset();
    onsets_.reset();
    onset_ = false;
    pulse_ = 0.0;
    reduction_ = 0.0;
    std::fill(wave_.begin(), wave_.end(), 0.0F);
    running_ = false;
    silent_ = kSettleSeconds;
    if (spectrum_)
        spectrum_->clear();
    std::fill(bands_.begin(), bands_.end(), 0.0);
}

double AudioFeatures::liveliness() const noexcept
{
    const double t = std::clamp(silent_ / kSettleSeconds, 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

} // namespace vloomdaw::motion
