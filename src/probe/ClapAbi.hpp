// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
//
// The parts of the CLAP ABI (a plain C ABI) needed to list the plugins in a
// .clap file with their descriptors. No plugin is created.
#pragma once

#include <cstdint>

namespace vloomdaw::clap {

struct Version {
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t revision;
};

struct PluginDescriptor {
    Version clapVersion;
    const char* id;
    const char* name;
    const char* vendor;
    const char* url;
    const char* manualUrl;
    const char* supportUrl;
    const char* version;
    const char* description;
    const char* const* features; ///< null-terminated, e.g. "audio-effect", "reverb"
};

struct PluginFactory {
    std::uint32_t (*getPluginCount)(const PluginFactory* factory);
    const PluginDescriptor* (*getPluginDescriptor)(const PluginFactory* factory, std::uint32_t index);
    const void* (*createPlugin)(const PluginFactory* factory, const void* host, const char* pluginId);
};

/// The exported `clap_entry` symbol is a variable of this type.
struct PluginEntry {
    Version clapVersion;
    bool (*init)(const char* pluginPath);
    void (*deinit)();
    const void* (*getFactory)(const char* factoryId);
};

inline constexpr const char* kPluginFactoryId = "clap.plugin-factory";

} // namespace vloomdaw::clap
