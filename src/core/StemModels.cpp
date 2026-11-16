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

// Every source is a fixed revision, and every file is checked against the
// SHA-256 listed with it, so what is run is exactly what was reviewed here.

/// The weights of Demucs (MIT) converted for demucs.cpp, as published by its author.
constexpr std::string_view kDemucsCpp = "https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/"
                                        "8f58ac0491bbea657275bcd3e38af1ab3a27bfc9/";

#if defined(VLOOMDAW_STEMS_ONNX)
/// HT Demucs exported to ONNX with the Fourier transform left outside (MIT;
/// from Meta's weights, see the repository's README): what graphics cards run.
constexpr std::string_view kOnnxCore = "https://huggingface.co/Ghilda/htdemucs-onnx/resolve/"
                                       "850cd89461d0817276337061c29e6abceb86d75f/";
/// ONNX exports with the transform inside (MIT), weights stored as 16-bit floats.
constexpr std::string_view kOnnxSix = "https://huggingface.co/StemSplitio/htdemucs-6s-onnx/resolve/"
                                      "49df9b6989cf2150840ea65b0bef77a2e471b678/";
constexpr std::string_view kOnnxFineTuned = "https://huggingface.co/StemSplitio/htdemucs-ft-onnx/resolve/"
                                            "8616370ed541bc183dbe15fb0d54d5f49918f47e/";
/// The MDX-Net models of Ultimate Vocal Remover (MIT; its developers ask to be
/// credited), from the project's own releases. A release is not a fixed
/// revision: the SHA-256 of each file is what pins it.
constexpr std::string_view kUvr
    = "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/";
#endif

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
    using Engine = StemModel::Engine;
    static const std::vector<StemModel> kModels = {
#if defined(VLOOMDAW_STEMS_ONNX)
        {"htdemucs_onnx", "HT Demucs",
            "Four stems. The one to start with: runs on the graphics card (NVIDIA, AMD or Intel), "
            "and on the processor where there is none.",
            {"drums", "bass", "other", "vocals"},
            {{"htdemucs.onnx", 174266088,
                "e528a932a7d091e15938369135569884b62c2193fb11044c3d4a0d4c7b9221af"}},
            kOnnxCore, false, Engine::Onnx, true},
        {"htdemucs_6s_onnx", "HT Demucs 6",
            "Six stems: guitar and piano too. The piano stem is the weakest. On the processor.",
            {"drums", "bass", "other", "vocals", "guitar", "piano"},
            {{"htdemucs_6s_fp16weights.onnx", 136428532,
                "7ce55792e2231c93fbf92de95f5fd5b3a5e6c89f7db690dfd693e8f1dce56869"}},
            kOnnxSix, false, Engine::Onnx, false},
        {"htdemucs_ft_onnx", "HT Demucs FT",
            "Four stems, one fine-tuned model for each: the cleanest, and four times as slow. On the "
            "processor.",
            {"drums", "bass", "other", "vocals"},
            {{"htdemucs_ft_drums_fp16weights.onnx", 165612636,
                 "047764dff888cfb87da917013377d4ec7a134f7419cbe486d9c339aa17975ddd"},
                {"htdemucs_ft_bass_fp16weights.onnx", 165612636,
                    "b533037176b14b2df31c92a5d5b3d5660d0811b9b360d3db761964768b079961"},
                {"htdemucs_ft_other_fp16weights.onnx", 165612636,
                    "b739171a7057b3107bb0711c6222d4a619b41b13a8f04026431d30f32ad2bd71"},
                {"htdemucs_ft_vocals_fp16weights.onnx", 165612636,
                    "0cbe651f535415c9d26a7bb614f7d322dd5a080fa0298f2e50f478030a994dce"}},
            kOnnxFineTuned, false, Engine::Onnx, false},
        {"mdx_vocals", "MDX-Net Vocals",
            "Vocals and the rest (UVR's Voc FT). Sharper vocals than Demucs, and quick on the graphics card.",
            {"vocals", "instrumental"},
            {{"UVR-MDX-NET-Voc_FT.onnx", 66762490,
                "534b2070fcc7df514b13ef660dc8cbb328679c2374d04354a5c42bb14ecce111"}},
            kUvr, false, Engine::Onnx, true, 7680, 1021},
        {"mdx_instrumental", "MDX-Net Instrumental",
            "The instrumental and the vocals (UVR's Inst HQ 3): for a clean backing track.",
            {"instrumental", "vocals"},
            {{"UVR-MDX-NET-Inst_HQ_3.onnx", 66759214,
                "317554b07fe1ea5279a77f2b1520a41ea4b93432560c4ffd08792c30fddf9adc"}},
            kUvr, false, Engine::Onnx, true, 6144, 1022},
        {"mdx_karaoke", "MDX-Net Karaoke",
            "Takes the lead vocal out and keeps the backing vocals with the music (UVR's Karaoke 2).",
            {"karaoke", "lead-vocals"},
            {{"UVR_MDXNET_KARA_2.onnx", 52786726,
                "bf32e15105a09c0f7dddd2b67346146334d6f3ecb399ed7638eba2ab07cbf5f4"}},
            kUvr, false, Engine::Onnx, true, 5120, 1065},
        {"mdx_reverb", "MDX-Net De-Reverb",
            "Splits the reverb from the dry sound (Reverb HQ by FoxJoy). Best on a vocal stem. On the "
            "processor: it takes twice the memory of the others, more than a 4 GB card has.",
            {"reverb", "dry"},
            {{"Reverb_HQ_By_FoxJoy.onnx", 66780123,
                "233bb5c6aaa365e568659a0a81211746fa881f8f47f82d9e864fce1f7692db80"}},
            kUvr, false, Engine::Onnx, false, 6144, 1035},
#endif
        // demucs.cpp's: small downloads, the processor only, many times slower.
        {"htdemucs", "HT Demucs (compact)",
            "Four stems in a small download. Many times slower: the processor only, without ONNX Runtime.",
            {"drums", "bass", "other", "vocals"},
            {{"ggml-model-htdemucs-4s-f16.bin", 83994361,
                "72b17c42d308982ddb5069bc3bf48b81a5aac4cb6516e4366c0fa7cef6df0064"}},
            kDemucsCpp},
        {"htdemucs_6s", "HT Demucs 6 (compact)",
            "Six stems in a small download. Many times slower; the piano stem is the weakest.",
            {"drums", "bass", "other", "vocals", "guitar", "piano"},
            {{"ggml-model-htdemucs-6s-f16.bin", 54855129,
                "09704f4ceae204e56e77d5eefd6ac71d7275be81fd507e6913371d59abcee856"}},
            kDemucsCpp},
        {"htdemucs_ft", "HT Demucs FT (compact)",
            "Four stems, one fine-tuned model for each, in a smaller download. Very slow.",
            {"drums", "bass", "other", "vocals"},
            {{"ggml-model-htdemucs_ft_drums-4s-f16.bin", 83994361,
                 "cda268755cc80deb5f4616a99f9d09f26bc5ce3882aa35e0bc504708a12604bb"},
                {"ggml-model-htdemucs_ft_bass-4s-f16.bin", 83994361,
                    "e6ded215aa15fb4eff7a37f3a51fd596ee17c94ae0ddb7ef3f2f6ecf8b86cdb5"},
                {"ggml-model-htdemucs_ft_other-4s-f16.bin", 83994361,
                    "cbc315571584d512e7853c9d40a499182393a7f4155cb8723a9040a685c2435b"},
                {"ggml-model-htdemucs_ft_vocals-4s-f16.bin", 83994361,
                    "19186500a45a551a034d96e9500415ebe73c8bd570bf55337ddc8cc8f53a9120"}},
            kDemucsCpp},
        {"hdemucs_mmi", "Hybrid Demucs v3",
            "Four stems, the older model. The processor only; a little less clean than HT Demucs.",
            {"drums", "bass", "other", "vocals"},
            {{"ggml-model-hdemucs_mmi-v3-f16.bin", 167294211,
                "f72a3b8ecce3aa50da9e062a91c46367e15b5f5a71d070874217c5e4cb6b35a4"}},
            kDemucsCpp, true},
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

std::string stemModelUrl(const StemModel& model, const StemModelFile& file)
{
    return std::string(model.source) + std::string(file.name);
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
