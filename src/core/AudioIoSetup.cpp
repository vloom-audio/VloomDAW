// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/AudioIoSetup.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace vloomdaw {

namespace {

constexpr std::size_t kMaxBusNameLen = 48;

std::string sanitizeName(std::string name, const char* fallback)
{
    if (name.size() > kMaxBusNameLen) {
        std::size_t len = kMaxBusNameLen;
        while (len > 0 && (static_cast<unsigned char>(name[len]) & 0xC0u) == 0x80u)
            --len;
        name.resize(len);
    }
    const auto first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return fallback;
    const auto last = name.find_last_not_of(" \t\r\n");
    return name.substr(first, last - first + 1);
}

void clampBuses(std::vector<AudioIoBus>& buses, const char* fallbackPrefix)
{
    if (buses.size() > AudioIoConfig::kMaxBuses)
        buses.resize(AudioIoConfig::kMaxBuses);
    const int maxCh = AudioIoConfig::kMaxHardwareChannels - 1;
    for (std::size_t i = 0; i < buses.size(); ++i) {
        AudioIoBus& b = buses[i];
        const std::string fallback = std::string(fallbackPrefix) + " " + std::to_string(i + 1);
        b.name = sanitizeName(std::move(b.name), fallback.c_str());
        b.leftChannel = std::clamp(b.leftChannel, 0, maxCh);
        if (b.stereo) {
            b.rightChannel = std::clamp(b.rightChannel, 0, maxCh);
        } else {
            b.rightChannel = b.leftChannel;
        }
    }
}

nlohmann::json busToJson(const AudioIoBus& b)
{
    return nlohmann::json{
        {"name", b.name},
        {"stereo", b.stereo},
        {"left", b.leftChannel},
        {"right", b.rightChannel},
    };
}

AudioIoBus busFromJson(const nlohmann::json& j)
{
    AudioIoBus b;
    if (j.is_object()) {
        b.name = j.value("name", std::string{});
        b.stereo = j.value("stereo", true);
        b.leftChannel = j.value("left", 0);
        b.rightChannel = j.value("right", b.stereo ? 1 : b.leftChannel);
    }
    return b;
}

} // namespace

AudioIoConfig defaultAudioIoConfig()
{
    AudioIoConfig cfg;
    cfg.unifiedDevice = false;
    cfg.inputs = {
        AudioIoBus{"IN 0", false, 0, 0},
        AudioIoBus{"IN 1", false, 1, 1},
        AudioIoBus{"IN 0 + 1", true, 0, 1},
    };
    cfg.outputs = {
        AudioIoBus{"Main", true, 0, 1},
    };
    cfg.activeInputBus = 2;
    cfg.activeOutputBus = 0;
    return cfg;
}

AudioIoConfig clampedAudioIoConfig(AudioIoConfig config)
{
    clampBuses(config.inputs, "IN");
    if (config.inputs.empty())
        config.inputs = defaultAudioIoConfig().inputs;
    clampBuses(config.outputs, "Out");
    if (config.outputs.empty())
        config.outputs = defaultAudioIoConfig().outputs;

    config.activeInputBus = std::clamp(config.activeInputBus, 0, static_cast<int>(config.inputs.size()) - 1);
    config.activeOutputBus
        = std::clamp(config.activeOutputBus, 0, static_cast<int>(config.outputs.size()) - 1);
    return config;
}

std::string serializeAudioIoConfig(const AudioIoConfig& config)
{
    const AudioIoConfig c = clampedAudioIoConfig(config);
    nlohmann::json ins = nlohmann::json::array();
    for (const AudioIoBus& b : c.inputs)
        ins.push_back(busToJson(b));
    nlohmann::json outs = nlohmann::json::array();
    for (const AudioIoBus& b : c.outputs)
        outs.push_back(busToJson(b));

    const nlohmann::json root{
        {"version", 1},
        {"unifiedDevice", c.unifiedDevice},
        {"activeInputBus", c.activeInputBus},
        {"activeOutputBus", c.activeOutputBus},
        {"inputs", std::move(ins)},
        {"outputs", std::move(outs)},
    };
    return root.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

AudioIoConfig deserializeAudioIoConfig(const std::string& jsonText)
{
    if (jsonText.empty() || jsonText.size() > 65536)
        return defaultAudioIoConfig();
    try {
        const nlohmann::json root = nlohmann::json::parse(jsonText);
        if (!root.is_object())
            return defaultAudioIoConfig();
        AudioIoConfig cfg;
        cfg.unifiedDevice = root.value("unifiedDevice", false);
        cfg.activeInputBus = root.value("activeInputBus", 2);
        cfg.activeOutputBus = root.value("activeOutputBus", 0);
        if (const auto it = root.find("inputs"); it != root.end() && it->is_array()) {
            for (const auto& item : *it) {
                if (cfg.inputs.size() >= AudioIoConfig::kMaxBuses)
                    break;
                cfg.inputs.push_back(busFromJson(item));
            }
        }
        if (const auto it = root.find("outputs"); it != root.end() && it->is_array()) {
            for (const auto& item : *it) {
                if (cfg.outputs.size() >= AudioIoConfig::kMaxBuses)
                    break;
                cfg.outputs.push_back(busFromJson(item));
            }
        }
        return clampedAudioIoConfig(std::move(cfg));
    } catch (...) {
        return defaultAudioIoConfig();
    }
}

const char* nativeAudioDriverName() noexcept
{
#if defined(_WIN32)
    return "Windows Audio (WASAPI Low-Latency)";
#elif defined(__APPLE__)
    return "CoreAudio (HAL Audio Unit)";
#elif defined(__linux__)
    return "ALSA / PipeWire Audio";
#else
    return "System Audio";
#endif
}

} // namespace vloomdaw
