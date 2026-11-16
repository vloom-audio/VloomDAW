// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "stems/Onnx.hpp"

#include "core/MdxSpectrogram.hpp"
#include "core/StemSpectrogram.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <dml_provider_factory.h>
#include <memory>
#include <numbers>
#include <onnxruntime_cxx_api.h>
#include <stdexcept>
#include <thread>

namespace vloomdaw::stems {

namespace {

using Spectrogram = vloomdaw::StemSpectrogram;
constexpr std::int64_t kSegment = Spectrogram::kSegmentFrames;
constexpr int kMinOverlap = 25;
constexpr int kMaxOverlap = 75;
constexpr std::size_t kPlane = Spectrogram::kPlane;
constexpr int kMaxStems = 8;

/// One loaded model on one device.
struct Session {
    std::unique_ptr<Ort::Session> session;
    std::vector<std::string> inputs; ///< the waveform, then the spectrogram
    std::vector<std::string> outputs; ///< the frequency branch, then the time branch
    int stems = 0;
    /// The model does the Fourier transform itself: one input (the
    /// waveform), one output (the stems' waveforms).
    bool whole = false;
};

/// The model's inputs and outputs by their shape, whatever they are called:
/// exports name them differently.
Ort::SessionOptions sessionOptions(bool gpu, bool fusion)
{
    Ort::SessionOptions options;
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    if (gpu) {
        // What DirectML asks for, and no graph fusion: fused, these models
        // give wrong numbers on some drivers (seen on NVIDIA 32.0.15).
        options.DisableMemPattern();
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        if (!fusion)
            options.AddConfigEntry("ep.dml.disable_graph_fusion", "1");
        const OrtDmlApi* dml = nullptr;
        Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi(
            "DML", ORT_API_VERSION, reinterpret_cast<const void**>(&dml)));
        // The fast card of a laptop with two, whoever made it.
        OrtDmlDeviceOptions device{OrtDmlPerformancePreference::HighPerformance, OrtDmlDeviceFilter::Gpu};
        Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML2(options, &device));
    }
    return options;
}

Session open(Ort::Env& env, const std::filesystem::path& model, bool gpu, bool fusion = false)
{
    Session out;
    out.session = std::make_unique<Ort::Session>(env, model.c_str(), sessionOptions(gpu, fusion));
    Ort::AllocatorWithDefaultOptions allocator;
    const auto rank
        = [](const Ort::TypeInfo& info) { return info.GetTensorTypeAndShapeInfo().GetShape().size(); };
    if (out.session->GetInputCount() == 1 && out.session->GetOutputCount() == 1) {
        const std::vector<std::int64_t> shape
            = out.session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (rank(out.session->GetInputTypeInfo(0)) != 3 || shape.size() != 4 || shape[1] < 1
            || shape[1] > kMaxStems)
            throw std::runtime_error("the model is not one this app can run");
        out.whole = true;
        out.stems = static_cast<int>(shape[1]);
        out.inputs = {out.session->GetInputNameAllocated(0, allocator).get()};
        out.outputs = {out.session->GetOutputNameAllocated(0, allocator).get()};
        return out;
    }
    if (out.session->GetInputCount() != 2 || out.session->GetOutputCount() != 2)
        throw std::runtime_error("the model is not one this app can run");
    std::string wave;
    std::string spec;
    for (std::size_t i = 0; i < 2; ++i) {
        const std::string name = out.session->GetInputNameAllocated(i, allocator).get();
        (rank(out.session->GetInputTypeInfo(i)) == 3 ? wave : spec) = name;
    }
    std::string frequency;
    std::string time;
    for (std::size_t i = 0; i < 2; ++i) {
        const std::string name = out.session->GetOutputNameAllocated(i, allocator).get();
        const std::vector<std::int64_t> shape
            = out.session->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
        (shape.size() == 5 ? frequency : time) = name;
        if (shape.size() >= 2)
            out.stems = static_cast<int>(shape[1]);
    }
    if (wave.empty() || spec.empty() || frequency.empty() || time.empty() || out.stems < 1
        || out.stems > kMaxStems)
        throw std::runtime_error("the model is not one this app can run");
    out.inputs = {wave, spec};
    out.outputs = {frequency, time};
    return out;
}

/// The stems of one segment: [stem][channel][frame].
using SegmentStems = std::vector<std::array<std::vector<float>, 2>>;

SegmentStems runSegment(
    Session& model, const Spectrogram& spectrogram, const std::vector<float>& mix, std::vector<float>& planes)
{
    // mix: the two channels one after the other, each kSegment long.
    const auto stemCount = static_cast<std::size_t>(model.stems);
    if (model.whole) {
        const Ort::MemoryInfo cpu = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<std::int64_t, 3> shape{1, 2, kSegment};
        Ort::Value input = Ort::Value::CreateTensor<float>(
            cpu, const_cast<float*>(mix.data()), mix.size(), shape.data(), shape.size());
        const char* inputName = model.inputs[0].c_str();
        const char* outputName = model.outputs[0].c_str();
        std::vector<Ort::Value> result
            = model.session->Run(Ort::RunOptions{nullptr}, &inputName, &input, 1, &outputName, 1);
        if (result.size() != 1
            || result[0].GetTensorTypeAndShapeInfo().GetElementCount()
                != stemCount * 2 * static_cast<std::size_t>(kSegment))
            throw std::runtime_error("the model gave a result of the wrong size");
        const float* waves = result[0].GetTensorData<float>();
        SegmentStems direct(stemCount);
        for (std::size_t s = 0; s < stemCount; ++s)
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const float* wave = waves + (s * 2 + channel) * static_cast<std::size_t>(kSegment);
                std::vector<float>& into = direct[s][channel];
                into.resize(static_cast<std::size_t>(kSegment));
                for (std::size_t i = 0; i < into.size(); ++i)
                    into[i] = std::isfinite(wave[i]) ? wave[i] : 0.0f;
            }
        return direct;
    }
    for (int channel = 0; channel < 2; ++channel)
        spectrogram.forward(mix.data() + channel * kSegment, planes.data() + channel * 2 * kPlane);
    const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<std::int64_t, 3> waveShape{1, 2, kSegment};
    const std::array<std::int64_t, 4> specShape{1, 4, Spectrogram::kBins, Spectrogram::kFrames};
    std::array<Ort::Value, 2> inputs{Ort::Value::CreateTensor<float>(memory, const_cast<float*>(mix.data()),
                                         mix.size(), waveShape.data(), waveShape.size()),
        Ort::Value::CreateTensor<float>(
            memory, planes.data(), planes.size(), specShape.data(), specShape.size())};
    const std::array<const char*, 2> inputNames{model.inputs[0].c_str(), model.inputs[1].c_str()};
    const std::array<const char*, 2> outputNames{model.outputs[0].c_str(), model.outputs[1].c_str()};
    std::vector<Ort::Value> outputs = model.session->Run(
        Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), 2, outputNames.data(), 2);

    const auto count
        = [](const Ort::Value& value) { return value.GetTensorTypeAndShapeInfo().GetElementCount(); };
    const std::size_t stems = stemCount;
    if (outputs.size() != 2 || count(outputs[0]) != stems * 4 * kPlane
        || count(outputs[1]) != stems * 2 * static_cast<std::size_t>(kSegment))
        throw std::runtime_error("the model gave a result of the wrong size");
    const float* frequency = outputs[0].GetTensorData<float>();
    const float* time = outputs[1].GetTensorData<float>();

    // Each stem is its frequency branch, turned back into a waveform, plus
    // its time branch. One thread per stem: the transforms are the
    // processor's part of the work.
    SegmentStems out(stems);
    std::vector<std::thread> workers;
    for (std::size_t s = 0; s < stems; ++s)
        workers.emplace_back([&, s] {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                std::vector<float>& wave = out[s][channel];
                wave.resize(static_cast<std::size_t>(kSegment));
                spectrogram.inverse(frequency + (s * 4 + channel * 2) * kPlane, wave.data());
                const float* branch = time + (s * 2 + channel) * static_cast<std::size_t>(kSegment);
                for (std::size_t i = 0; i < wave.size(); ++i) {
                    const float value = wave[i] + branch[i];
                    wave[i] = std::isfinite(value) ? value : 0.0f;
                }
            }
        });
    for (std::thread& worker : workers)
        worker.join();
    return out;
}

/// Whether the stems add up to the mix, as Demucs's do to within a few
/// percent. A graphics driver that computes the model wrongly gives noise
/// many times louder than the song, which this catches without knowing the
/// right answer.
bool addsUp(const SegmentStems& stems, const std::vector<float>& mix)
{
    double error = 0.0;
    double level = 0.0;
    for (std::size_t channel = 0; channel < 2; ++channel)
        for (std::size_t i = 0; i < static_cast<std::size_t>(kSegment); i += 7) { // a sample of it is enough
            double sum = 0.0;
            for (const auto& stem : stems)
                sum += stem[channel][i];
            const double original = mix[channel * static_cast<std::size_t>(kSegment) + i];
            error += (sum - original) * (sum - original);
            level += original * original;
        }
    // Near silence there is nothing to compare with; the stems must then be quiet too.
    constexpr double kQuiet = 1e-7;
    return error <= 0.25 * level + kQuiet * static_cast<double>(kSegment);
}

// ---------------------------------------------------------------- MDX-Net

constexpr int kMdxHop = 1024;
constexpr int kMdxMaxFrames = 1024;

/// An MDX-Net model on one device: a spectrogram in, the spectrogram of its stem out.
struct MdxSession {
    std::unique_ptr<Ort::Session> session;
    std::string input;
    std::string output;
    int bins = 0;
    int frames = 0;
};

MdxSession openMdx(Ort::Env& env, const std::filesystem::path& model, bool gpu, bool fusion = false)
{
    MdxSession out;
    out.session = std::make_unique<Ort::Session>(env, model.c_str(), sessionOptions(gpu, fusion));
    if (out.session->GetInputCount() != 1 || out.session->GetOutputCount() != 1)
        throw std::runtime_error("the model is not one this app can run");
    const std::vector<std::int64_t> in
        = out.session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    const std::vector<std::int64_t> result
        = out.session->GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    // [batch, the two channels' real and imaginary parts, bins, frames], the same out.
    if (in.size() != 4 || result.size() != 4 || in[1] != 4 || result[1] != 4 || in[2] < 1
        || in[2] != result[2] || in[3] < 2 || in[3] > kMdxMaxFrames || in[3] != result[3])
        throw std::runtime_error("the model is not one this app can run");
    Ort::AllocatorWithDefaultOptions allocator;
    out.input = out.session->GetInputNameAllocated(0, allocator).get();
    out.output = out.session->GetOutputNameAllocated(0, allocator).get();
    out.bins = static_cast<int>(in[2]);
    out.frames = static_cast<int>(in[3]);
    return out;
}

/// `count` pieces of work spread over the processor's cores: work(first, end).
template <typename Work> void spread(int count, const Work& work)
{
    const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 16);
    const int each = (count + threads - 1) / threads;
    std::vector<std::thread> workers;
    for (int first = 0; first < count; first += each)
        workers.emplace_back([&work, first, end = std::min(count, first + each)] { work(first, end); });
    for (std::thread& worker : workers)
        worker.join();
}

/// The model's stem of one chunk: `mix` and the result are the two channels
/// one after the other, each spectrogram.chunk() long. `card`: values that
/// are not numbers are a failure (a graphics card short of memory gives
/// them), not silence.
std::vector<float> runMdx(MdxSession& model, const MdxSpectrogram& spectrogram, const std::vector<float>& mix,
    std::vector<float>& planes, std::vector<float>& pieces, bool card)
{
    const auto chunk = static_cast<std::size_t>(spectrogram.chunk());
    const std::size_t plane = spectrogram.plane();
    const int frames = spectrogram.frames();
    for (std::size_t channel = 0; channel < 2; ++channel)
        spread(frames, [&](int first, int end) {
            spectrogram.forward(
                mix.data() + channel * chunk, planes.data() + channel * 2 * plane, first, end);
        });
    // As UVR does: the lowest three bins are not given to the model.
    for (std::size_t part = 0; part < 4; ++part)
        std::fill_n(planes.begin() + static_cast<std::ptrdiff_t>(part * plane),
            3 * static_cast<std::size_t>(frames), 0.0f);

    const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<std::int64_t, 4> shape{1, 4, spectrogram.bins(), frames};
    Ort::Value input
        = Ort::Value::CreateTensor<float>(memory, planes.data(), planes.size(), shape.data(), shape.size());
    const char* inputName = model.input.c_str();
    const char* outputName = model.output.c_str();
    std::vector<Ort::Value> result
        = model.session->Run(Ort::RunOptions{nullptr}, &inputName, &input, 1, &outputName, 1);
    if (result.size() != 1 || result[0].GetTensorTypeAndShapeInfo().GetElementCount() != 4 * plane)
        throw std::runtime_error("the model gave a result of the wrong size");
    const float* stem = result[0].GetTensorData<float>();
    if (card)
        for (std::size_t i = 0; i < 4 * plane; i += 5) // a sample of it: they spread through a network
            if (!std::isfinite(stem[i]))
                throw std::runtime_error("the graphics card gave values that are not numbers");

    std::vector<float> out(2 * chunk);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        spread(frames, [&](int first, int end) {
            spectrogram.inverseFrames(stem + channel * 2 * plane, pieces.data(), first, end);
        });
        spectrogram.overlapAdd(pieces.data(), out.data() + channel * chunk);
    }
    for (float& value : out)
        if (!std::isfinite(value))
            value = 0.0f;
    return out;
}

double energy(const std::vector<float>& wave)
{
    double sum = 0.0;
    for (const float value : wave)
        sum += static_cast<double>(value) * value;
    return sum;
}

/// Whether a stem can be part of this mix: not many times louder than it. A
/// graphics driver that computes the model wrongly gives loud noise.
bool plausible(const std::vector<float>& stem, const std::vector<float>& mix)
{
    constexpr double kQuiet = 1e-7;
    return energy(stem) <= 4.0 * energy(mix) + kQuiet * static_cast<double>(mix.size());
}

/// Whether two runs of the same chunk agree, as the card and the processor do when both are right.
bool agree(const std::vector<float>& a, const std::vector<float>& b)
{
    double difference = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        difference += static_cast<double>(a[i] - b[i]) * (a[i] - b[i]);
    constexpr double kQuiet = 1e-9;
    return difference <= 1e-3 * energy(b) + kQuiet * static_cast<double>(b.size());
}

/// MDX-Net: one model gives one stem; the other is what is left of the song.
/// As UVR does it: chunks a quarter overlapped, each faded with a Hann
/// window, half a transform window of silence around the song.
std::vector<std::vector<float>> separateMdx(
    Ort::Env& env, const std::vector<float>& left, const std::vector<float>& right, const OnnxJob& job)
{
    const auto frames = static_cast<std::int64_t>(left.size());
    if (job.models.size() != 1 || job.mdxFft < 1024 || job.mdxFft > 16384 || job.mdxFft % 2 != 0)
        throw std::runtime_error("the model is not one this app can run");
    const auto tell = [&job](const std::string& text) {
        if (job.device)
            job.device(text);
    };
    bool gpu = job.allowGpu;
    MdxSession model;
    if (gpu) {
        try {
            model = openMdx(env, job.models[0], true, job.unsafeFusion);
        } catch (const std::exception&) {
            gpu = false;
            tell("cpu: the graphics card cannot run this model");
        }
    }
    if (!gpu)
        model = openMdx(env, job.models[0], false);
    if (gpu)
        tell("gpu");
    else if (!job.allowGpu)
        tell("cpu");
    if (model.bins > job.mdxFft / 2 + 1
        || static_cast<std::int64_t>(kMdxHop) * (model.frames - 1) <= job.mdxFft)
        throw std::runtime_error("the model is not one this app can run");

    const MdxSpectrogram spectrogram(job.mdxFft, kMdxHop, model.bins, model.frames);
    const std::int64_t chunk = spectrogram.chunk();
    const std::int64_t trim = job.mdxFft / 2;
    const std::int64_t step = chunk * (100 - std::clamp(job.overlapPercent, kMinOverlap, kMaxOverlap)) / 100;
    // The song sits `trim` into a line of silence that ends as UVR's does
    // (its last chunks are cut short there); chunks that start past the
    // song's end add nothing to it.
    const std::int64_t gen = chunk - 2 * trim;
    const std::int64_t padded = trim + frames + gen + trim - frames % gen;
    const std::int64_t chunks = (trim + frames + step - 1) / step;

    std::vector<float> sum(static_cast<std::size_t>(2 * padded), 0.0f);
    std::vector<float> weights(static_cast<std::size_t>(padded), 0.0f);
    std::vector<float> mix(static_cast<std::size_t>(2 * chunk));
    std::vector<float> planes(4 * spectrogram.plane());
    std::vector<float> pieces(static_cast<std::size_t>(model.frames) * static_cast<std::size_t>(job.mdxFft));
    std::vector<float> fade(static_cast<std::size_t>(chunk));
    std::int64_t faded = 0; ///< the length `fade` is made for

    for (std::int64_t c = 0; c < chunks; ++c) {
        const std::int64_t start = c * step;
        std::fill(mix.begin(), mix.end(), 0.0f);
        // Sample i of the chunk is sample start + i - trim of the song.
        const std::int64_t from = std::max<std::int64_t>(0, trim - start);
        const std::int64_t to = std::min(chunk, frames + trim - start);
        if (to > from) {
            std::copy_n(left.begin() + (start + from - trim), to - from, mix.begin() + from);
            std::copy_n(right.begin() + (start + from - trim), to - from, mix.begin() + chunk + from);
        }

        std::vector<float> out;
        bool ok = false;
        if (gpu) {
            try {
                out = runMdx(model, spectrogram, mix, planes, pieces, true);
                ok = plausible(out, mix);
                if (ok && c == 0) {
                    // Once, against the processor: after that the cheap check is enough to
                    // catch a card that goes wrong.
                    MdxSession reference = openMdx(env, job.models[0], false);
                    std::vector<float> expected = runMdx(reference, spectrogram, mix, planes, pieces, false);
                    if (!agree(out, expected)) {
                        out = std::move(expected); // and the processor from here on
                        gpu = false;
                        model = std::move(reference);
                        tell("cpu: the graphics card gave a wrong result");
                    }
                } else if (!ok) {
                    tell("cpu: the graphics card gave a wrong result");
                }
            } catch (const std::exception&) {
                ok = false;
                tell("cpu: the graphics card ran out of memory or failed");
            }
            if (!ok) {
                gpu = false; // from here on, and this chunk again
                model = openMdx(env, job.models[0], false);
            }
        }
        if (!ok)
            out = runMdx(model, spectrogram, mix, planes, pieces, false);

        // A chunk that reaches past the end of the line is faded over the part inside it.
        const std::int64_t inside = std::min(chunk, padded - start);
        if (inside != faded) {
            faded = inside;
            for (std::int64_t i = 0; i < inside; ++i) // numpy.hanning: zero at both ends
                fade[static_cast<std::size_t>(i)] = inside == 1
                    ? 1.0f
                    : static_cast<float>(0.5
                          - 0.5
                              * std::cos(2.0 * std::numbers::pi * static_cast<double>(i)
                                  / static_cast<double>(inside - 1)));
        }
        for (std::int64_t i = 0; i < inside; ++i) {
            const float weight = fade[static_cast<std::size_t>(i)];
            const auto at = static_cast<std::size_t>(start + i);
            sum[2 * at] += out[static_cast<std::size_t>(i)] * weight;
            sum[2 * at + 1] += out[static_cast<std::size_t>(chunk + i)] * weight;
            weights[at] += weight;
        }
        if (job.progress)
            job.progress(static_cast<double>(c + 1) / static_cast<double>(chunks));
    }

    // The model's stem, at the level UVR found it needs; the other is the rest of the song.
    std::vector<std::vector<float>> stems(2, std::vector<float>(static_cast<std::size_t>(2 * frames)));
    for (std::int64_t i = 0; i < frames; ++i) {
        const auto at = static_cast<std::size_t>(trim + i);
        const float weight = weights[at];
        const auto to = static_cast<std::size_t>(2 * i);
        const std::array<float, 2> song{
            left[static_cast<std::size_t>(i)], right[static_cast<std::size_t>(i)]};
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const float value = weight > 1e-6f ? sum[2 * at + channel] / weight * job.mdxGain : 0.0f;
            stems[0][to + channel] = value;
            stems[1][to + channel] = song[channel] - value;
        }
    }
    return stems;
}

} // namespace

std::vector<std::vector<float>> separateOnnx(
    const std::vector<float>& left, const std::vector<float>& right, const OnnxJob& job)
{
    const auto frames = static_cast<std::int64_t>(left.size());
    if (frames <= 0 || right.size() != left.size() || job.models.empty())
        throw std::runtime_error("nothing to separate");
    Ort::Env env(ORT_LOGGING_LEVEL_FATAL, "vloomdaw-stems");
    if (job.mdxFft != 0)
        return separateMdx(env, left, right, job);
    const Spectrogram spectrogram;
    const auto tell = [&job](const std::string& text) {
        if (job.device)
            job.device(text);
    };

    // Segments overlap (by a quarter unless told otherwise, as Demucs does) and are cross-faded there.
    const std::int64_t overlap = kSegment * std::clamp(job.overlapPercent, kMinOverlap, kMaxOverlap) / 100;
    const std::int64_t stride = kSegment - overlap;
    const std::int64_t segments = std::max<std::int64_t>(1, (frames - overlap + stride - 1) / stride);
    const double total = static_cast<double>(segments) * static_cast<double>(job.models.size());
    std::vector<std::vector<float>> stems;
    std::vector<float> weights(static_cast<std::size_t>(frames), 0.0f);
    std::vector<float> mix(static_cast<std::size_t>(2 * kSegment));
    std::vector<float> planes(4 * kPlane);
    bool gpu = job.allowGpu;
    double done = 0.0;

    for (std::size_t m = 0; m < job.models.size(); ++m) {
        Session model;
        if (gpu) {
            try {
                model = open(env, job.models[m], true, job.unsafeFusion);
            } catch (const std::exception&) {
                gpu = false; // no usable card, or not enough memory on it
                tell("cpu: the graphics card cannot run this model");
            }
        }
        if (!gpu)
            model = open(env, job.models[m], false);
        if (m == 0) {
            if (gpu)
                tell("gpu");
            else if (!job.allowGpu)
                tell("cpu");
        }
        const bool whole = job.models.size() == 1;
        if (!whole && model.stems != static_cast<int>(job.models.size()))
            throw std::runtime_error("the models do not belong together");
        if (stems.empty())
            stems.assign(static_cast<std::size_t>(model.stems),
                std::vector<float>(static_cast<std::size_t>(2 * frames), 0.0f));
        else if (stems.size() != static_cast<std::size_t>(model.stems))
            throw std::runtime_error("the models do not belong together");

        for (std::int64_t segment = 0; segment < segments; ++segment) {
            const std::int64_t start = segment * stride;
            const std::int64_t length = std::min(kSegment, frames - start);
            std::fill(mix.begin(), mix.end(), 0.0f); // the last one is padded with silence
            std::copy_n(left.begin() + start, length, mix.begin());
            std::copy_n(right.begin() + start, length, mix.begin() + kSegment);

            SegmentStems out;
            bool ok = false;
            if (gpu) {
                try {
                    out = runSegment(model, spectrogram, mix, planes);
                    ok = addsUp(out, mix);
                    if (!ok)
                        tell("cpu: the graphics card gave a wrong result");
                } catch (const std::exception&) {
                    tell("cpu: the graphics card ran out of memory or failed");
                }
                if (!ok) {
                    gpu = false; // from here on, and this segment again
                    model = open(env, job.models[m], false);
                }
            }
            if (!ok)
                out = runSegment(model, spectrogram, mix, planes);

            // Into the song, faded in and out over the overlap with its neighbours.
            const std::size_t first = whole ? 0 : m;
            const std::size_t count = whole ? out.size() : 1;
            for (std::int64_t i = 0; i < length; ++i) {
                float weight = 1.0f;
                if (segment > 0 && i < overlap)
                    weight = (static_cast<float>(i) + 0.5f) / static_cast<float>(overlap);
                if (segment < segments - 1 && i >= kSegment - overlap)
                    weight = std::min(
                        weight, (static_cast<float>(kSegment - i) - 0.5f) / static_cast<float>(overlap));
                const auto at = static_cast<std::size_t>(start + i);
                for (std::size_t s = 0; s < count; ++s)
                    for (std::size_t channel = 0; channel < 2; ++channel)
                        stems[first + s][2 * at + channel]
                            += out[first + s][channel][static_cast<std::size_t>(i)] * weight;
                if (m == 0)
                    weights[at] += weight;
            }
            done += 1.0;
            if (job.progress)
                job.progress(done / total);
        }
    }
    for (std::vector<float>& stem : stems)
        for (std::size_t at = 0; at < weights.size(); ++at)
            if (const float weight = weights[at]; weight > 0.0f) {
                stem[2 * at] /= weight;
                stem[2 * at + 1] /= weight;
            }
    return stems;
}

} // namespace vloomdaw::stems
