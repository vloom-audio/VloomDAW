// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
//
// A minimal VST3 module for the probe tests: a factory with one audio
// processor ("Fake Reverb", Fx|Reverb) and one edit controller the probe must
// skip. It cannot be instantiated; it only answers factory questions.
#include "probe/Vst3Abi.hpp"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#define VLOOM_EXPORT extern "C" __declspec(dllexport)
#else
#define VLOOM_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace vloomdaw::vst3;

namespace {

template <std::size_t N> void copy(char (&out)[N], const char* text)
{
    const std::size_t length = std::min(std::strlen(text), N - 1);
    std::memcpy(out, text, length);
    out[length] = '\0';
}

class Factory final : public IPluginFactory2 {
public:
    tresult VLOOM_VST3_API queryInterface(const TUID iid, void** object) override
    {
        if (std::memcmp(iid, kIPluginFactory2.bytes, sizeof(TUID)) == 0) {
            *object = static_cast<IPluginFactory2*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return 1; // no such interface
    }
    std::uint32_t VLOOM_VST3_API addRef() override { return 1; }
    std::uint32_t VLOOM_VST3_API release() override { return 1; }

    tresult VLOOM_VST3_API getFactoryInfo(PFactoryInfo* info) override
    {
        *info = {};
        copy(info->vendor, "Vloom Test");
        return kResultOk;
    }
    std::int32_t VLOOM_VST3_API countClasses() override { return 2; }
    tresult VLOOM_VST3_API getClassInfo(std::int32_t index, PClassInfo* info) override
    {
        PClassInfo2 full{};
        if (getClassInfo2(index, &full) != kResultOk)
            return 1;
        *info = {};
        copy(info->category, full.category);
        copy(info->name, full.name);
        return kResultOk;
    }
    tresult VLOOM_VST3_API createInstance(const char*, const char*, void** object) override
    {
        *object = nullptr;
        return 1;
    }
    tresult VLOOM_VST3_API getClassInfo2(std::int32_t index, PClassInfo2* info) override
    {
        *info = {};
        if (index == 0) {
            copy(info->category, kAudioEffectClass);
            copy(info->name, "Fake Reverb");
            copy(info->subCategories, "Fx|Reverb");
            copy(info->version, "1.2.3");
            return kResultOk; // vendor left empty: the factory's applies
        }
        if (index == 1) {
            copy(info->category, "Component Controller Class");
            copy(info->name, "Fake Controller");
            return kResultOk;
        }
        return 1;
    }
};

} // namespace

VLOOM_EXPORT IPluginFactory* VLOOM_VST3_API GetPluginFactory()
{
    static Factory factory;
    return &factory;
}

#if defined(_WIN32)
VLOOM_EXPORT bool VLOOM_VST3_API InitDll() { return true; }
VLOOM_EXPORT bool VLOOM_VST3_API ExitDll() { return true; }
#elif defined(__APPLE__)
VLOOM_EXPORT bool bundleEntry(void*) { return true; }
VLOOM_EXPORT bool bundleExit() { return true; }
#else
VLOOM_EXPORT bool ModuleEntry(void*) { return true; }
VLOOM_EXPORT bool ModuleExit() { return true; }
#endif
