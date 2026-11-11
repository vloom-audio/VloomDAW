// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/AudioController.hpp"

namespace vloomdaw {

AudioController::~AudioController() { closeDevice(); }

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

bool AudioController::openDevice(int index, std::string* error)
{
    closeDevice();

    vloom::AudioDeviceConfig config;
    config.deviceIndex = index;
    config.sampleRate = requestedSampleRate_;
    config.bufferFrames = bufferFrames_;

    auto callback
        = [this](float* const* out, int channels, int frames) { engine_.process(out, channels, frames); };
    if (!device_.open(config, callback, error))
        return false;

    // Audio is stopped here, so preparing the engine is safe.
    engine_.prepare(device_.sampleRate(), vloom::AudioDevice::kMaxBlockFrames);

    if (!device_.start(error)) {
        device_.close();
        return false;
    }
    return true;
}

void AudioController::closeDevice() { device_.close(); }

void AudioController::setFormat(int sampleRate, int bufferFrames) noexcept
{
    requestedSampleRate_ = validSampleRate(sampleRate);
    bufferFrames_ = validBufferFrames(bufferFrames);
}

} // namespace vloomdaw
