// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
//
// A minimal CLAP module for the probe tests: two plugin descriptors, a reverb
// and a synth. Plugins cannot be created; only the factory answers.
#include "probe/ClapAbi.hpp"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define VLOOM_EXPORT extern "C" __declspec(dllexport)
#else
#define VLOOM_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace vloomdaw::clap;

namespace {

const char* const kReverbFeatures[] = {"audio-effect", "reverb", "stereo", nullptr};
const char* const kSynthFeatures[] = {"instrument", "synthesizer", nullptr};

const PluginDescriptor kDescriptors[] = {
    {{1, 2, 0}, "audio.vloom.fake-reverb", "Fake CLAP Reverb", "Vloom Test", nullptr, nullptr, nullptr, "0.9",
        nullptr, kReverbFeatures},
    {{1, 2, 0}, "audio.vloom.fake-synth", "Fake CLAP Synth", "Vloom Test", nullptr, nullptr, nullptr, "0.9",
        nullptr, kSynthFeatures},
};

std::uint32_t count(const PluginFactory*) { return 2; }
const PluginDescriptor* descriptor(const PluginFactory*, std::uint32_t index)
{
    return index < 2 ? &kDescriptors[index] : nullptr;
}
const void* create(const PluginFactory*, const void*, const char*) { return nullptr; }

const PluginFactory kFactory{count, descriptor, create};

bool init(const char*)
{
    // Noise without a newline, then a forged record: both must be ignored.
    std::printf("[fake] licence ok");
    std::printf("@@vloom\tplugin\tForged\tEvil\t6.6.6\tFx|Reverb\n");
    std::fflush(stdout);
    return true;
}
void deinit() { }
const void* factory(const char* id) { return std::strcmp(id, kPluginFactoryId) == 0 ? &kFactory : nullptr; }

} // namespace

VLOOM_EXPORT const PluginEntry clap_entry{{1, 2, 0}, init, deinit, factory};
