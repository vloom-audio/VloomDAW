// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/StemModels.hpp"

#include <system_error>

#if defined(_M_X64) || defined(__x86_64__)
#if defined(_MSC_VER)
#include <immintrin.h>
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace vloomdaw {

namespace {

/// The weights of Demucs (MIT) converted for demucs.cpp, as published by its
/// author. The revision is fixed, and every file is checked against the
/// SHA-256 below, so what is run is exactly what was reviewed here.
constexpr std::string_view kRepository = "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/"
                                         "8f58ac0491bbea657275bcd3e38af1ab3a27bfc9/";

} // namespace

std::uint64_t StemModel::bytes() const
{
    std::uint64_t total = 0;
    for (const StemModelFile& file : files)
        total += file.bytes;
    return total;
}

const std::vector<StemModel>& stemModels()
{
    static const std::vector<StemModel> kModels = {
        {"htdemucs", "HT Demucs",
            "Four stems. The one to start with: good, and the fastest of the v4 models.",
            {"drums", "bass", "other", "vocals"},
            {{"ggml-model-htdemucs-4s-f16.bin", 83994361,
                "72b17c42d308982ddb5069bc3bf48b81a5aac4cb6516e4366c0fa7cef6df0064"}}},
        {"htdemucs_6s", "HT Demucs 6", "Six stems: guitar and piano too. The piano stem is the weakest.",
            {"drums", "bass", "other", "vocals", "guitar", "piano"},
            {{"ggml-model-htdemucs-6s-f16.bin", 54855129,
                "09704f4ceae204e56e77d5eefd6ac71d7275be81fd507e6913371d59abcee856"}}},
        {"htdemucs_ft", "HT Demucs FT",
            "Four stems, one fine-tuned model for each: the cleanest, and four times as slow.",
            {"drums", "bass", "other", "vocals"},
            {{"ggml-model-htdemucs_ft_drums-4s-f16.bin", 83994361,
                 "cda268755cc80deb5f4616a99f9d09f26bc5ce3882aa35e0bc504708a12604bb"},
                {"ggml-model-htdemucs_ft_bass-4s-f16.bin", 83994361,
                    "e6ded215aa15fb4eff7a37f3a51fd596ee17c94ae0ddb7ef3f2f6ecf8b86cdb5"},
                {"ggml-model-htdemucs_ft_other-4s-f16.bin", 83994361,
                    "cbc315571584d512e7853c9d40a499182393a7f4155cb8723a9040a685c2435b"},
                {"ggml-model-htdemucs_ft_vocals-4s-f16.bin", 83994361,
                    "19186500a45a551a034d96e9500415ebe73c8bd570bf55337ddc8cc8f53a9120"}}},
        {"hdemucs_mmi", "Hybrid Demucs v3",
            "Four stems, the older model: faster than HT Demucs, a little less clean.",
            {"drums", "bass", "other", "vocals"},
            {{"ggml-model-hdemucs_mmi-v3-f16.bin", 167294211,
                "f72a3b8ecce3aa50da9e062a91c46367e15b5f5a71d070874217c5e4cb6b35a4"}},
            true},
    };
    return kModels;
}

const StemModel* findStemModel(std::string_view id)
{
    for (const StemModel& model : stemModels())
        if (model.id == id)
            return &model;
    return nullptr;
}

std::string stemModelUrl(const StemModelFile& file)
{
    return std::string(kRepository) + std::string(file.name);
}

bool stemModelInstalled(const StemModel& model, const std::filesystem::path& folder)
{
    for (const StemModelFile& file : model.files) {
        std::error_code ec;
        const std::filesystem::path path = folder / std::string(file.name);
        if (!std::filesystem::is_regular_file(path, ec) || ec)
            return false;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec || size != file.bytes)
            return false;
    }
    return !model.files.empty();
}

bool stemSeparationSupported()
{
#if defined(_M_X64) || defined(__x86_64__)
    // AVX2 and FMA, and an operating system that saves the wider registers.
    const auto cpuid = [](unsigned leaf, unsigned sub, unsigned out[4]) {
#if defined(_MSC_VER)
        int regs[4];
        __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(sub));
        for (int i = 0; i < 4; ++i)
            out[i] = static_cast<unsigned>(regs[i]);
#else
        __cpuid_count(leaf, sub, out[0], out[1], out[2], out[3]);
#endif
    };
    unsigned regs[4] = {};
    cpuid(0, 0, regs);
    if (regs[0] < 7)
        return false;
    cpuid(1, 0, regs);
    constexpr unsigned kFma = 1U << 12;
    constexpr unsigned kOsXsave = 1U << 27;
    constexpr unsigned kAvx = 1U << 28;
    if ((regs[2] & (kFma | kOsXsave | kAvx)) != (kFma | kOsXsave | kAvx))
        return false;
#if defined(_MSC_VER)
    const unsigned long long saved = _xgetbv(0);
#else
    unsigned low = 0;
    unsigned high = 0;
    __asm__("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
    const unsigned long long saved = (static_cast<unsigned long long>(high) << 32) | low;
#endif
    if ((saved & 0x6) != 0x6) // XMM and YMM state
        return false;
    cpuid(7, 0, regs);
    return (regs[1] & (1U << 5)) != 0; // AVX2
#else
    return true;
#endif
}

} // namespace vloomdaw
