// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
//
// Minimal VST 2 synth module for vloomdaw-plugin-probe and PluginHost tests.

#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define VLOOM_FAKE_VST2_CALL __cdecl
#define VLOOM_FAKE_VST2_EXPORT __declspec(dllexport)
#else
#define VLOOM_FAKE_VST2_CALL
#define VLOOM_FAKE_VST2_EXPORT __attribute__((visibility("default")))
#endif

namespace {

struct FakeEffect;

using HostCallback = std::intptr_t(VLOOM_FAKE_VST2_CALL*)(
    FakeEffect* effect, std::int32_t opcode, std::int32_t index, std::intptr_t value, void* ptr, float opt);
using DispatcherFn = std::intptr_t(VLOOM_FAKE_VST2_CALL*)(
    FakeEffect* effect, std::int32_t opcode, std::int32_t index, std::intptr_t value, void* ptr, float opt);
using ProcessFn
    = void(VLOOM_FAKE_VST2_CALL*)(FakeEffect* effect, float** inputs, float** outputs, std::int32_t frames);
using ProcessDoubleFn
    = void(VLOOM_FAKE_VST2_CALL*)(FakeEffect* effect, double** inputs, double** outputs, std::int32_t frames);
using SetParamFn = void(VLOOM_FAKE_VST2_CALL*)(FakeEffect* effect, std::int32_t index, float value);
using GetParamFn = float(VLOOM_FAKE_VST2_CALL*)(FakeEffect* effect, std::int32_t index);

struct FakeEffect {
    std::int32_t magic = 0x56737450; // 'VstP'
    DispatcherFn dispatcher = nullptr;
    ProcessFn processAccumulating = nullptr;
    SetParamFn setParameter = nullptr;
    GetParamFn getParameter = nullptr;
    std::int32_t numPrograms = 1;
    std::int32_t numParams = 1;
    std::int32_t numInputs = 0;
    std::int32_t numOutputs = 2;
    std::int32_t flags = (1 << 4) | (1 << 8); // kCanReplacing | kIsSynth
    std::intptr_t hostData1 = 0;
    std::intptr_t hostData2 = 0;
    std::int32_t initialDelay = 0;
    std::int32_t unused1 = 0;
    std::int32_t unused2 = 0;
    float unused3 = 1.0f;
    void* object = nullptr;
    void* user = nullptr;
    std::int32_t uniqueId = 0x564C5432; // 'VLT2'
    std::int32_t version = 200;
    ProcessFn processReplacing = nullptr;
    ProcessDoubleFn processDoubleReplacing = nullptr;
    char future[56]{};
};

struct State {
    FakeEffect effect{};
    float gain = 0.5f;
};

void copyStr(void* dst, const char* src)
{
    if (dst != nullptr)
        std::memcpy(dst, src, std::strlen(src) + 1);
}

std::intptr_t VLOOM_FAKE_VST2_CALL dispatch(FakeEffect* effect, std::int32_t opcode, std::int32_t index,
    std::intptr_t /*value*/, void* ptr, float /*opt*/)
{
    auto* st = static_cast<State*>(effect->object);
    switch (opcode) {
    case 1: // kClose
        delete st;
        return 0;
    case 8: // kGetParamName
        copyStr(ptr, index == 0 ? "Gain" : "");
        return 0;
    case 45: // kGetEffectName
        copyStr(ptr, "Fake VST2 Synth");
        return 1;
    case 47: // kGetVendorString
        copyStr(ptr, "Vloom Test");
        return 1;
    default:
        return 0;
    }
}

void VLOOM_FAKE_VST2_CALL setParam(FakeEffect* effect, std::int32_t index, float value)
{
    if (index == 0)
        static_cast<State*>(effect->object)->gain = value;
}

float VLOOM_FAKE_VST2_CALL getParam(FakeEffect* effect, std::int32_t index)
{
    return index == 0 ? static_cast<State*>(effect->object)->gain : 0.0f;
}

void VLOOM_FAKE_VST2_CALL processReplacing(
    FakeEffect* /*effect*/, float** /*inputs*/, float** outputs, std::int32_t frames)
{
    for (int ch = 0; ch < 2; ++ch)
        for (std::int32_t i = 0; i < frames; ++i)
            outputs[ch][i] = 0.0f;
}

} // namespace

extern "C" VLOOM_FAKE_VST2_EXPORT FakeEffect* VLOOM_FAKE_VST2_CALL VSTPluginMain(HostCallback /*host*/)
{
    std::puts("@@vloom\tplugin\tForged VST2\tEvil\t1.0\tFx");
    std::fflush(stdout);
    auto* st = new State();
    st->effect.dispatcher = &dispatch;
    st->effect.setParameter = &setParam;
    st->effect.getParameter = &getParam;
    st->effect.processReplacing = &processReplacing;
    st->effect.object = st;
    return &st->effect;
}
