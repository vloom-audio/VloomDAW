// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
//
// vloomdaw-stems: separates one piece of audio into stems (drums, bass,
// other, vocals...) with a Demucs model, using demucs.cpp. VloomDAW runs it
// as a separate process: the work takes minutes and gigabytes, can be
// cancelled by ending the process, and a model file the library cannot cope
// with cannot take the app down. The app only passes model files whose
// SHA-256 it has checked (core/StemModels).
//
//   vloomdaw-stems --in <audio.f32> --out <folder> --threads <n> [--v3]
//                  --model <file> [--model <file> ...]
//   vloomdaw-stems --in <audio.f32> --out <folder> --onnx [--device auto|cpu]
//                  [--overlap <percent>]
//                  [--mdx-fft <samples> --mdx-gain <thousandths>]
//                  --model <file> [--model <file> ...]
//
// The input is raw 32-bit float samples, stereo interleaved, at 44100 Hz.
// Each stem is written the same way to <folder>/<index>.f32.
//
// One model: every stem comes from it. Several: stem i comes from model i
// (the fine-tuned set, one model per stem). --v3: the files are Hybrid
// Demucs (v3) models.
//
// --overlap: how much of each piece given to the model is shared with the
// next, 25 (the default) to 75 percent; more is smoother and slower.
//
// --mdx-fft: the model is an MDX-Net one, made for a transform window of
// that many samples. It gives two stems: its own, multiplied by --mdx-gain,
// and the rest of the song.
//
// Without --onnx the models are demucs.cpp's and run on the processor.
// With it (Windows builds) they are ONNX models run by ONNX Runtime: on the
// graphics card through DirectML when there is one that can (any DirectX 12
// card: NVIDIA, AMD, Intel), else on the processor, which is still many
// times faster than demucs.cpp. --device cpu leaves the card alone.
//
// Output (one record per line, tab-separated; other lines are the library's
// own and to be ignored):
//   @@vloom	stems	1
//   @@vloom	progress	<0 .. 1>
//   @@vloom	device	gpu | cpu[: why the graphics card was given up]   (--onnx)
//   @@vloom	done	<number of stems>
//   @@vloom	error	<message>              (exit code 1)

#include "core/StemChunks.hpp"
#if defined(VLOOMDAW_STEMS_ONNX)
#include "stems/Onnx.hpp"
#endif

#include <Eigen/Dense>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <model.hpp>
#include <mutex>
#include <string>
#include <system_error>
#include <tensor.hpp>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using vloomdaw::StemChunk;

namespace {

constexpr int kRate = 44100; // what the models were trained at
constexpr std::int64_t kMinFrames = kRate / 10;
constexpr std::int64_t kMaxFrames = std::int64_t{kRate} * 60 * 30; // half an hour
constexpr std::int64_t kOverlap = kRate * 3 / 4; // between chunks, as demucs.cpp's own threaded driver
constexpr std::int64_t kMinChunkFrames = std::int64_t{kRate} * 10; // the model works on 7.8 s at a time
constexpr int kMaxThreads = 16;
constexpr std::size_t kMaxModels = 4;

struct Args {
    fs::path in;
    fs::path out;
    std::vector<fs::path> models;
    int threads = 1;
    bool v3 = false;
    bool onnx = false;
    bool gpu = true; ///< --onnx: try the graphics card first
    bool unsafeFusion = false;
    int overlap = 25; ///< --onnx: percent
    int mdxFft = 0; ///< --onnx: an MDX-Net model, its transform window
    float mdxGain = 1.0f;
};

struct Failure {
    std::string message;
};

void say(const char* record, const std::string& value)
{
    std::string text;
    for (const char c : value)
        text.push_back(static_cast<unsigned char>(c) < 0x20 ? ' ' : c);
    std::printf("@@vloom\t%s\t%s\n", record, text.c_str());
    std::fflush(stdout);
}

/// Progress of the whole run, from the chunks working at once.
class Progress {
public:
    Progress(std::size_t models, std::size_t chunks)
        : chunks_(chunks, 0.0f)
        , models_(static_cast<float>(models))
    {
    }

    void startModel(std::size_t index)
    {
        const std::lock_guard lock(mutex_);
        model_ = static_cast<float>(index);
        std::fill(chunks_.begin(), chunks_.end(), 0.0f);
    }

    void set(std::size_t chunk, float value)
    {
        const std::lock_guard lock(mutex_);
        chunks_[chunk] = std::clamp(value, 0.0f, 1.0f);
        float sum = 0.0f;
        for (const float each : chunks_)
            sum += each;
        const float total = (model_ + sum / static_cast<float>(chunks_.size())) / models_;
        if (total - said_ < 0.002f)
            return;
        said_ = total;
        say("progress", std::to_string(total));
    }

private:
    std::mutex mutex_;
    std::vector<float> chunks_;
    float models_;
    float model_ = 0.0f;
    float said_ = -1.0f;
};

/// Stems as they are put together: [stem][2 * frame + channel].
using Stems = std::vector<std::vector<float>>;

/// Separates `audio` chunk by chunk, every chunk on a thread of its own, and
/// adds the stems `first` .. `first + count - 1` of the result to `stems`
/// (cross-faded where chunks meet).
void separate(
    const std::function<Eigen::Tensor3dXf(const Eigen::MatrixXf&, demucscpp::ProgressCallback)>& infer,
    const Eigen::MatrixXf& audio, const std::vector<StemChunk>& chunks, int first, int count, Stems& stems,
    Progress& progress)
{
    const auto frames = static_cast<std::int64_t>(audio.cols());
    std::vector<std::thread> threads;
    std::mutex failureMutex;
    std::string failure;
    std::mutex stemsMutex;
    for (std::size_t i = 0; i < chunks.size(); ++i)
        threads.emplace_back([&, i] {
            try {
                const StemChunk& chunk = chunks[i];
                const Eigen::MatrixXf part = audio.block(0, chunk.from, 2, chunk.to - chunk.from);
                const Eigen::Tensor3dXf out
                    = infer(part, [&progress, i](float done, const std::string&) { progress.set(i, done); });
                if (out.dimension(0) < first + count || out.dimension(1) != 2
                    || out.dimension(2) != chunk.to - chunk.from)
                    throw std::runtime_error("the model gave a result of the wrong size");
                const std::lock_guard lock(stemsMutex);
                for (int stem = 0; stem < count; ++stem) {
                    std::vector<float>& into = stems[static_cast<std::size_t>(first + stem)];
                    for (std::int64_t frame = chunk.from; frame < chunk.to; ++frame) {
                        const float weight = vloomdaw::stemChunkWeight(chunk, frame, frames, kOverlap);
                        for (int channel = 0; channel < 2; ++channel) {
                            const float value = out(first + stem, channel, frame - chunk.from);
                            into[static_cast<std::size_t>(2 * frame + channel)]
                                += std::isfinite(value) ? value * weight : 0.0f;
                        }
                    }
                }
                progress.set(i, 1.0f);
            } catch (const std::exception& e) {
                const std::lock_guard lock(failureMutex);
                failure = e.what();
            } catch (...) {
                const std::lock_guard lock(failureMutex);
                failure = "unknown error";
            }
        });
    for (std::thread& thread : threads)
        thread.join();
    if (!failure.empty())
        throw Failure{"separating failed: " + failure};
}

Eigen::MatrixXf readInput(const fs::path& path)
{
    std::error_code ec;
    const std::uintmax_t bytes = fs::file_size(path, ec);
    if (ec)
        throw Failure{"cannot read the input"};
    constexpr std::uintmax_t kFrameBytes = 2 * sizeof(float);
    if (bytes % kFrameBytes != 0 || bytes / kFrameBytes < static_cast<std::uintmax_t>(kMinFrames)
        || bytes / kFrameBytes > static_cast<std::uintmax_t>(kMaxFrames))
        throw Failure{"the input is too short or too long (0.1 s to 30 min)"};
    const auto frames = static_cast<std::int64_t>(bytes / kFrameBytes);
    std::vector<float> samples(static_cast<std::size_t>(2 * frames));
    std::ifstream in(path, std::ios::binary);
    in.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(bytes));
    if (!in || in.gcount() != static_cast<std::streamsize>(bytes))
        throw Failure{"cannot read the input"};
    Eigen::MatrixXf audio(2, frames);
    for (std::int64_t i = 0; i < frames; ++i)
        for (int channel = 0; channel < 2; ++channel) {
            const float value = samples[static_cast<std::size_t>(2 * i + channel)];
            audio(channel, i) = std::isfinite(value) ? value : 0.0f;
        }
    return audio;
}

/// demucs.cpp opens the model with fopen(), which on Windows cannot name
/// every file. So: into the model's folder, and the bare (ASCII) file name.
std::string enterModelFolder(const fs::path& model)
{
    const std::string name = model.filename().string();
    for (const char c : name)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e)
            throw Failure{"the model's file name must be plain ASCII"};
    std::error_code ec;
    fs::current_path(model.parent_path(), ec);
    if (ec)
        throw Failure{"cannot open the model's folder"};
    return name;
}

void writeStems(const Args& args, const Stems& stems)
{
    for (std::size_t i = 0; i < stems.size(); ++i) {
        const fs::path path = args.out / (std::to_string(i) + ".f32");
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(stems[i].data()),
            static_cast<std::streamsize>(stems[i].size() * sizeof(float)));
        out.flush();
        if (!out)
            throw Failure{"cannot write the stems"};
    }
    say("done", std::to_string(stems.size()));
}

#if defined(VLOOMDAW_STEMS_ONNX)
int runOnnx(const Args& args, const Eigen::MatrixXf& audio)
{
    const auto frames = static_cast<std::size_t>(audio.cols());
    std::vector<float> left(frames);
    std::vector<float> right(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        left[i] = audio(0, static_cast<Eigen::Index>(i));
        right[i] = audio(1, static_cast<Eigen::Index>(i));
    }
    vloomdaw::stems::OnnxJob job;
    job.models = args.models;
    job.allowGpu = args.gpu;
    job.unsafeFusion = args.unsafeFusion;
    job.overlapPercent = args.overlap;
    job.mdxFft = args.mdxFft;
    job.mdxGain = args.mdxGain;
    float said = -1.0f;
    job.progress = [&said](double done) {
        if (static_cast<float>(done) - said < 0.002f)
            return;
        said = static_cast<float>(done);
        say("progress", std::to_string(done));
    };
    job.device = [](const std::string& device) { say("device", device); };
    try {
        writeStems(args, vloomdaw::stems::separateOnnx(left, right, job));
    } catch (const Failure&) {
        throw;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception& e) {
        // ONNX Runtime's own messages name files and lines: not for the user.
        std::fprintf(stderr, "onnx: %s\n", e.what());
        throw Failure{"the model could not be run"};
    }
    return 0;
}
#endif

int run(const Args& args)
{
    say("stems", "1");
    const Eigen::MatrixXf audio = readInput(args.in);
    if (args.onnx) {
#if defined(VLOOMDAW_STEMS_ONNX)
        return runOnnx(args, audio);
#else
        throw Failure{"this build cannot run ONNX models"};
#endif
    }
    const auto frames = static_cast<std::int64_t>(audio.cols());
    const std::vector<StemChunk> chunks
        = vloomdaw::stemChunks(frames, args.threads, kOverlap, kMinChunkFrames);
    Progress progress(args.models.size(), chunks.size());

    Stems stems;
    const auto room = [&](int count) {
        if (stems.empty())
            stems.assign(
                static_cast<std::size_t>(count), std::vector<float>(static_cast<std::size_t>(2 * frames)));
    };
    for (std::size_t m = 0; m < args.models.size(); ++m) {
        progress.startModel(m);
        const std::string file = enterModelFolder(args.models[m]);
        // Several models: model m is the one fine-tuned for stem m.
        const bool whole = args.models.size() == 1;
        if (args.v3) {
            demucscpp_v3::demucs_v3_model model{};
            if (!demucscpp_v3::load_demucs_v3_model(file, &model))
                throw Failure{"cannot load the model"};
            room(4);
            separate(
                [&model](const Eigen::MatrixXf& part, demucscpp::ProgressCallback cb) {
                    return demucscpp_v3::demucs_v3_inference(model, part, std::move(cb));
                },
                audio, chunks, whole ? 0 : static_cast<int>(m), whole ? 4 : 1, stems, progress);
        } else {
            demucscpp::demucs_model model{};
            if (!demucscpp::load_demucs_model(file, &model))
                throw Failure{"cannot load the model"};
            const int count = model.is_4sources ? 4 : 6;
            if (!whole && count != static_cast<int>(args.models.size()))
                throw Failure{"the models do not belong together"};
            room(count);
            separate(
                [&model](const Eigen::MatrixXf& part, demucscpp::ProgressCallback cb) {
                    return demucscpp::demucs_inference(model, part, std::move(cb));
                },
                audio, chunks, whole ? 0 : static_cast<int>(m), whole ? count : 1, stems, progress);
        }
    }

    writeStems(args, stems);
    return 0;
}

template <typename Char> int start(int argc, Char** argv)
{
    try {
        Args args;
        for (int i = 1; i < argc; ++i) {
            const fs::path word(argv[i]);
            const std::string option = word.string();
            const auto value = [&]() -> fs::path {
                if (i + 1 >= argc)
                    throw Failure{"missing value for " + option};
                return fs::path(argv[++i]);
            };
            if (option == "--in")
                args.in = value();
            else if (option == "--out")
                args.out = value();
            else if (option == "--model")
                args.models.push_back(value());
            else if (option == "--threads")
                args.threads = std::clamp(std::atoi(value().string().c_str()), 1, kMaxThreads);
            else if (option == "--v3")
                args.v3 = true;
            else if (option == "--onnx")
                args.onnx = true;
            else if (option == "--device")
                args.gpu = value().string() != "cpu";
            else if (option == "--mdx-fft")
                args.mdxFft = std::clamp(std::atoi(value().string().c_str()), 0, 16384);
            else if (option == "--mdx-gain") // thousandths, so no decimal point to mind
                args.mdxGain = static_cast<float>(std::clamp(std::atoi(value().string().c_str()), 500, 2000))
                    / 1000.0f;
            else if (option == "--overlap")
                args.overlap = std::clamp(std::atoi(value().string().c_str()), 25, 75);
            else if (option == "--unsafe-fusion") // test only: see OnnxJob::unsafeFusion
                args.unsafeFusion = true;
            else
                throw Failure{"unknown option"};
        }
        if (args.in.empty() || args.out.empty() || args.models.empty() || args.models.size() > kMaxModels
            || !args.in.is_absolute() || !args.out.is_absolute())
            throw Failure{
                "usage: vloomdaw-stems --in <file> --out <folder> --threads <n> [--v3] --model <file>..."};
        for (const fs::path& model : args.models)
            if (!model.is_absolute())
                throw Failure{"paths must be absolute"};
        return run(args);
    } catch (const Failure& failure) {
        say("error", failure.message);
    } catch (const std::bad_alloc&) {
        say("error", "not enough memory");
    } catch (const std::exception& e) {
        say("error", e.what());
    } catch (...) {
        say("error", "unknown error");
    }
    return 1;
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) { return start(argc, argv); }
#else
int main(int argc, char** argv) { return start(argc, argv); }
#endif
