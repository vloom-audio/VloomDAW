// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
//
// The few VST 3 interfaces needed to read a plugin's factory: what the
// module contains, who made it and its categories. Written from the stable
// VST 3 ABI (COM-like vtables); nothing else of the SDK is needed because
// the probe never creates or runs a plugin instance.
#pragma once

#include <cstdint>

#if defined(_WIN32)
#define VLOOM_VST3_API __stdcall
#else
#define VLOOM_VST3_API
#endif

namespace vloomdaw::vst3 {

using tresult = std::int32_t;
using TUID = char[16];
inline constexpr tresult kResultOk = 0;

/// Builds a TUID the way the VST 3 SDK's INLINE_UID does: COM layout on
/// Windows, plain big-endian elsewhere.
struct Uid {
    char bytes[16];
};
constexpr Uid makeUid(std::uint32_t l1, std::uint32_t l2, std::uint32_t l3, std::uint32_t l4)
{
    const auto b = [](std::uint32_t v, int shift) { return static_cast<char>((v >> shift) & 0xFF); };
#if defined(_WIN32)
    return {{b(l1, 0), b(l1, 8), b(l1, 16), b(l1, 24), b(l2, 16), b(l2, 24), b(l2, 0), b(l2, 8), b(l3, 24),
        b(l3, 16), b(l3, 8), b(l3, 0), b(l4, 24), b(l4, 16), b(l4, 8), b(l4, 0)}};
#else
    return {{b(l1, 24), b(l1, 16), b(l1, 8), b(l1, 0), b(l2, 24), b(l2, 16), b(l2, 8), b(l2, 0), b(l3, 24),
        b(l3, 16), b(l3, 8), b(l3, 0), b(l4, 24), b(l4, 16), b(l4, 8), b(l4, 0)}};
#endif
}

inline constexpr Uid kIPluginFactory2 = makeUid(0x0007B650, 0xF24B4C0B, 0xA464EDB9, 0xF00B2ABB);

/// Class category of audio processors ("Audio Module Class").
inline constexpr const char* kAudioEffectClass = "Audio Module Class";

struct PFactoryInfo {
    char vendor[64];
    char url[256];
    char email[128];
    std::int32_t flags;
};

struct PClassInfo {
    TUID cid;
    std::int32_t cardinality;
    char category[32];
    char name[64];
};

struct PClassInfo2 {
    TUID cid;
    std::int32_t cardinality;
    char category[32];
    char name[64];
    std::uint32_t classFlags;
    char subCategories[128]; ///< e.g. "Fx|Reverb", "Instrument|Synth"
    char vendor[64];
    char version[64];
    char sdkVersion[64];
};

class FUnknown {
public:
    virtual tresult VLOOM_VST3_API queryInterface(const TUID iid, void** object) = 0;
    virtual std::uint32_t VLOOM_VST3_API addRef() = 0;
    virtual std::uint32_t VLOOM_VST3_API release() = 0;

protected:
    ~FUnknown() = default;
};

class IPluginFactory : public FUnknown {
public:
    virtual tresult VLOOM_VST3_API getFactoryInfo(PFactoryInfo* info) = 0;
    virtual std::int32_t VLOOM_VST3_API countClasses() = 0;
    virtual tresult VLOOM_VST3_API getClassInfo(std::int32_t index, PClassInfo* info) = 0;
    virtual tresult VLOOM_VST3_API createInstance(const char* cid, const char* iid, void** object) = 0;

protected:
    ~IPluginFactory() = default;
};

class IPluginFactory2 : public IPluginFactory {
public:
    virtual tresult VLOOM_VST3_API getClassInfo2(std::int32_t index, PClassInfo2* info) = 0;

protected:
    ~IPluginFactory2() = default;
};

using GetFactoryProc = IPluginFactory*(VLOOM_VST3_API*)();

} // namespace vloomdaw::vst3
