// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/AudioController.hpp"

#include "core/OutputOpenPlan.hpp"

#include <algorithm>
#include <cmath>

namespace vloomdaw {

AudioController::~AudioController()
{
    closeMidiInput();
    closeInputDevice();
    closeDevice();
}

OutputDeviceList AudioController::outputDevices()
{
    OutputDeviceList list;
    const auto devices = device_.listOutputDevices();
    for (std::size_t i = 0; i < devices.size(); ++i) {
        list.names.push_back(devices[i].name);
        if (devices[i].isDefault)
            list.defaultIndex = static_cast<int>(i);
    }
    return list;
}

OutputDeviceList AudioController::inputDevices()
{
    OutputDeviceList list;
    const auto devices = input_.listInputDevices();
    for (std::size_t i = 0; i < devices.size(); ++i) {
        list.names.push_back(devices[i].name);
        if (devices[i].isDefault)
            list.defaultIndex = static_cast<int>(i);
    }
    return list;
}

std::vector<std::string> AudioController::midiInputDevices()
{
    std::vector<std::string> names;
    for (const vloom::MidiInputInfo& info : vloom::MidiInput::listDevices())
        names.push_back(info.name);
    return names;
}

bool AudioController::openDevice(int index, std::string* error)
{
    const bool reopenInput = hasRequestedInput_;
    if (reopenInput)
        input_.close();
    closeDevice();

    const int maxOutCh = std::max(outLeft_.load(std::memory_order_relaxed),
        outStereo_.load(std::memory_order_relaxed) ? outRight_.load(std::memory_order_relaxed) : 0);
    const int wantedOutChannels = maxOutCh >= 2 ? (maxOutCh < 4 ? 4 : (maxOutCh < 6 ? 6 : 8)) : 2;

    auto callback = [this](float* const* out, int channels, int frames) {
        if (out == nullptr || channels <= 0 || frames <= 0)
            return;
        const bool stereo = outStereo_.load(std::memory_order_relaxed);
        const int lIdx = std::clamp(outLeft_.load(std::memory_order_relaxed), 0, channels - 1);
        const int rIdx
            = stereo ? std::clamp(outRight_.load(std::memory_order_relaxed), 0, channels - 1) : lIdx;
        if (stereo && lIdx == 0 && rIdx == 1 && channels == 2) {
            engine_.process(out, channels, frames);
            return;
        }
        for (int ch = 0; ch < channels; ++ch)
            std::fill_n(out[ch], frames, 0.0f);
        for (int done = 0; done < frames;) {
            const int part = std::min(frames - done, vloom::AudioDevice::kMaxBlockFrames);
            float* pair[2] = {outScratchL_.data(), outScratchR_.data()};
            engine_.process(pair, 2, part);
            if (stereo) {
                float* dstL = out[lIdx] + done;
                float* dstR = out[rIdx] + done;
                if (lIdx == rIdx) {
                    for (int i = 0; i < part; ++i)
                        dstL[i] = 0.5f
                            * (outScratchL_[static_cast<std::size_t>(i)]
                                + outScratchR_[static_cast<std::size_t>(i)]);
                } else {
                    std::copy_n(outScratchL_.data(), part, dstL);
                    std::copy_n(outScratchR_.data(), part, dstR);
                }
            } else {
                float* dstM = out[lIdx] + done;
                for (int i = 0; i < part; ++i)
                    dstM[i] = 0.5f
                        * (outScratchL_[static_cast<std::size_t>(i)]
                            + outScratchR_[static_cast<std::size_t>(i)]);
            }
            done += part;
        }
    };

    vloom::AudioDeviceConfig config;
    config.deviceIndex = index;
    config.sampleRate = requestedSampleRate_;
    config.bufferFrames = bufferFrames_;

    // Exclusive tries first when asked for; if none opens, shared mode keeps
    // sound playing and exclusiveFallbackError() says why exclusive failed.
    exclusiveFallbackError_.clear();
    std::string exclusiveError;
    std::string sharedError;
    bool opened = false;
    for (const OutputOpenStep& step : outputOpenSteps(exclusiveRequested_, wantedOutChannels)) {
        config.exclusive = step.exclusive;
        config.channels = step.channels;
        std::string stepError;
        if (device_.open(config, callback, &stepError)) {
            opened = true;
            break;
        }
        (step.exclusive ? exclusiveError : sharedError) = std::move(stepError);
    }
    if (!opened) {
        if (error != nullptr)
            *error = sharedError;
        return false;
    }
    if (exclusiveRequested_ && !device_.isExclusive())
        exclusiveFallbackError_ = exclusiveError;

    // Audio is stopped here, so preparing the engine is safe.
    engine_.prepare(device_.sampleRate(), vloom::AudioDevice::kMaxBlockFrames);
    (void)engine_.setRecordLatency(bufferFrames_);
    (void)engine_.setAudioRecordLatency(bufferFrames_ * 2);

    if (!device_.start(error)) {
        device_.close();
        return false;
    }
    if (reopenInput)
        (void)reopenInputAtCurrentRate();
    return true;
}

void AudioController::closeDevice() { device_.close(); }

void AudioController::setActiveInputRouting(bool stereo, int leftChannel, int rightChannel) noexcept
{
    const int l = std::clamp(leftChannel, 0, static_cast<int>(kMaxHwChannels) - 1);
    const int r = stereo ? std::clamp(rightChannel, 0, static_cast<int>(kMaxHwChannels) - 1) : l;
    inStereo_.store(stereo, std::memory_order_relaxed);
    inLeft_.store(l, std::memory_order_relaxed);
    inRight_.store(r, std::memory_order_relaxed);
}

void AudioController::setActiveOutputRouting(bool stereo, int leftChannel, int rightChannel) noexcept
{
    const int l = std::clamp(leftChannel, 0, static_cast<int>(kMaxHwChannels) - 1);
    const int r = stereo ? std::clamp(rightChannel, 0, static_cast<int>(kMaxHwChannels) - 1) : l;
    outStereo_.store(stereo, std::memory_order_relaxed);
    outLeft_.store(l, std::memory_order_relaxed);
    outRight_.store(r, std::memory_order_relaxed);
}

vloom::AudioInput::Callback AudioController::makeInputCallback()
{
    return [this](const float* const* in, int channels, int frames) {
        if (in == nullptr || channels <= 0 || frames <= 0)
            return;

        const int hwCount = std::min(channels, static_cast<int>(kMaxHwChannels));
        for (int ch = 0; ch < hwCount; ++ch) {
            const float* src = in[ch];
            if (src == nullptr)
                continue;
            float pk = 0.0f;
            for (int i = 0; i < frames; ++i)
                pk = std::max(pk, std::abs(src[i]));
            auto& slot = hwInputPeaks_[static_cast<std::size_t>(ch)];
            float prev = slot.load(std::memory_order_relaxed);
            while (pk > prev && !slot.compare_exchange_weak(prev, pk, std::memory_order_relaxed)) { }
        }

        const bool stereo = inStereo_.load(std::memory_order_relaxed);
        const int lIdx = std::clamp(inLeft_.load(std::memory_order_relaxed), 0, channels - 1);
        const int rIdx
            = stereo ? std::clamp(inRight_.load(std::memory_order_relaxed), 0, channels - 1) : lIdx;
        const float* left = in[lIdx];
        const float* right = in[rIdx];
        float pkL = 0.0f;
        float pkR = 0.0f;
        const int perPeak = std::max(1, waveFramesPerPeak_.load(std::memory_order_relaxed));
        for (int i = 0; i < frames; ++i) {
            const float sl = left != nullptr ? std::abs(left[i]) : 0.0f;
            const float sr = right != nullptr ? std::abs(right[i]) : sl;
            pkL = std::max(pkL, sl);
            pkR = std::max(pkR, sr);
            waveBucketPeak_ = std::max(waveBucketPeak_, std::max(sl, sr));
            if (++waveBucketFrames_ >= perPeak) {
                const std::size_t w = waveWrite_.load(std::memory_order_relaxed);
                const std::size_t r = waveRead_.load(std::memory_order_acquire);
                if (w - r < kWaveRingSize) {
                    waveRing_[w & (kWaveRingSize - 1)] = waveBucketPeak_;
                    waveWrite_.store(w + 1, std::memory_order_release);
                }
                waveBucketPeak_ = 0.0f;
                waveBucketFrames_ = 0;
            }
        }
        float prevL = inputPeakL_.load(std::memory_order_relaxed);
        while (pkL > prevL && !inputPeakL_.compare_exchange_weak(prevL, pkL, std::memory_order_relaxed)) { }
        float prevR = inputPeakR_.load(std::memory_order_relaxed);
        while (pkR > prevR && !inputPeakR_.compare_exchange_weak(prevR, pkR, std::memory_order_relaxed)) { }
        engine_.pushAudioInput(left, right, frames);
    };
}

bool AudioController::openInputWithFallback(int index, std::string* error)
{
    input_.close();
    const int targetRate = device_.sampleRate() > 0 ? device_.sampleRate() : requestedSampleRate_;
    waveFramesPerPeak_.store(std::max(1, targetRate / 200), std::memory_order_relaxed);
    waveBucketPeak_ = 0.0f;
    waveBucketFrames_ = 0;
    clearInputWavePeaks();

    const int maxInCh = std::max(inLeft_.load(std::memory_order_relaxed),
        inStereo_.load(std::memory_order_relaxed) ? inRight_.load(std::memory_order_relaxed) : 0);
    const int preferredCh = maxInCh >= 2 ? (maxInCh < 4 ? 4 : (maxInCh < 6 ? 6 : 8)) : 2;
    const std::array<int, 6> channelTries{preferredCh, 2, 1, 4, 6, 8};
    const std::array<int, 2> rateTries{targetRate, 0};
    std::string firstErr;
    for (const int reqRate : rateTries) {
        for (std::size_t attempt = 0; attempt < channelTries.size(); ++attempt) {
            if (attempt > 0 && channelTries[attempt] == preferredCh)
                continue;
            vloom::AudioInputConfig config;
            config.deviceIndex = index;
            config.sampleRate = reqRate;
            config.bufferFrames = bufferFrames_;
            config.channels = channelTries[attempt];

            std::string attemptErr;
            if (!input_.open(config, makeInputCallback(), &attemptErr)) {
                if (firstErr.empty() && !attemptErr.empty())
                    firstErr = attemptErr;
                continue;
            }
            const int actualRate = input_.sampleRate() > 0 ? input_.sampleRate() : targetRate;
            waveFramesPerPeak_.store(std::max(1, actualRate / 200), std::memory_order_relaxed);
            if (!input_.start(&attemptErr)) {
                if (firstErr.empty() && !attemptErr.empty())
                    firstErr = attemptErr;
                input_.close();
                continue;
            }
            (void)engine_.setAudioRecordLatency(bufferFrames_ * 2);
            return true;
        }
    }
    if (error != nullptr)
        *error = firstErr;
    return false;
}

bool AudioController::reopenInputAtCurrentRate()
{
    return openInputWithFallback(requestedInputIndex_, nullptr);
}

bool AudioController::openInputDevice(int index, std::string* error)
{
    closeInputDevice();
    hasRequestedInput_ = true;
    requestedInputIndex_ = index;
    if (!openInputWithFallback(index, error)) {
        hasRequestedInput_ = false;
        return false;
    }
    return true;
}

void AudioController::closeInputDevice()
{
    hasRequestedInput_ = false;
    input_.close();
    inputPeakL_.store(0.0f, std::memory_order_relaxed);
    inputPeakR_.store(0.0f, std::memory_order_relaxed);
    for (auto& pk : hwInputPeaks_)
        pk.store(0.0f, std::memory_order_relaxed);
    clearInputWavePeaks();
}

float AudioController::takeInputPeak(int channel) noexcept
{
    if (channel == 0)
        return inputPeakL_.exchange(0.0f, std::memory_order_relaxed);
    if (channel == 1)
        return inputPeakR_.exchange(0.0f, std::memory_order_relaxed);
    return 0.0f;
}

float AudioController::takeHardwareInputPeak(int channel) noexcept
{
    if (channel < 0 || channel >= static_cast<int>(kMaxHwChannels))
        return 0.0f;
    return hwInputPeaks_[static_cast<std::size_t>(channel)].exchange(0.0f, std::memory_order_relaxed);
}

std::size_t AudioController::takeInputWavePeaks(std::span<float> out) noexcept
{
    std::size_t r = waveRead_.load(std::memory_order_relaxed);
    const std::size_t w = waveWrite_.load(std::memory_order_acquire);
    const std::size_t avail = w - r;
    const std::size_t count = std::min(out.size(), avail);
    for (std::size_t i = 0; i < count; ++i)
        out[i] = waveRing_[(r + i) & (kWaveRingSize - 1)];
    if (count > 0)
        waveRead_.store(r + count, std::memory_order_release);
    return count;
}

void AudioController::clearInputWavePeaks() noexcept
{
    const std::size_t w = waveWrite_.load(std::memory_order_acquire);
    waveRead_.store(w, std::memory_order_release);
}

double AudioController::inputWavePeakSeconds() const noexcept
{
    const int sr = input_.sampleRate() > 0
        ? input_.sampleRate()
        : (device_.sampleRate() > 0 ? device_.sampleRate() : requestedSampleRate_);
    return sr > 0 ? static_cast<double>(std::max(1, waveFramesPerPeak_.load(std::memory_order_relaxed)))
            / static_cast<double>(sr)
                  : 0.005;
}

bool AudioController::openMidiInput(int index, std::string* error)
{
    closeMidiInput();
    if (index < 0)
        return false;
    auto callback = [this](const vloom::MidiMessage& message) {
        midiActivityCount_.fetch_add(1, std::memory_order_relaxed);
        const int ch = liveMidiChannel_.load(std::memory_order_relaxed);
        (void)engine_.playMidi(ch, message);
    };
    if (!midi_.open(index, std::move(callback), error))
        return false;
    if (!midi_.start(error)) {
        midi_.close();
        return false;
    }
    (void)engine_.setRecordLatency(bufferFrames_);
    return true;
}

void AudioController::closeMidiInput()
{
    midi_.close();
    (void)engine_.releaseLiveNotes();
}

void AudioController::setExclusive(bool exclusive) noexcept
{
#if defined(_WIN32)
    exclusiveRequested_ = exclusive;
#else
    (void)exclusive; // WASAPI only
    exclusiveRequested_ = false;
#endif
}

void AudioController::setFormat(int sampleRate, int bufferFrames) noexcept
{
    requestedSampleRate_ = validSampleRate(sampleRate);
    bufferFrames_ = validBufferFrames(bufferFrames);
}

} // namespace vloomdaw
