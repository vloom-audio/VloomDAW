// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "core/PluginScanner.hpp"

#include <vloom/engine/plugin/AudioProcessor.hpp>
#include <vloom/engine/plugin/PluginInstance.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vloom {
class ClapLibrary;
class Vst2Library;
class Vst3Library;
} // namespace vloom

namespace vloomdaw {

/// Finds a loaded plugin instance by its PluginHost handle; null when there
/// is none. What the mixer and the channel rack use to build engine states.
using PluginResolver = std::function<std::shared_ptr<vloom::PluginInstance>(std::uint64_t handle)>;

/// Which of a library's `plugins` to create: the one with `id`; with no
/// id, the one named `name` (a probed scan lists each plugin of a file by
/// name), else the only one (an unprobed scan names it after its file).
/// Several plugins and no match: none, with `error` saying why, rather than
/// a guess whose id would then be saved.
struct PluginPick {
    const vloom::PluginInfo* plugin = nullptr; ///< points into `plugins`
    std::string error{};
};
[[nodiscard]] PluginPick pickPlugin(
    const std::vector<vloom::PluginInfo>& plugins, std::string_view id, std::string_view name);

/// Where a built-in goes in the app's plugin lists: one of the app's
/// categories (core/PluginMetadata: "Dynamics", "EQ", "Filter", "Delay",
/// "Reverb", "Modulation", "Distortion", "Utility", "Analyzer", or
/// "Instrument") and the tags that give an instrument its type
/// ("Instrument|Drum", ...).
struct BuiltinCategory {
    std::string category;
    std::string tags;
};
/// From the category vloom-engine gives a built-in (PluginInfo::category).
[[nodiscard]] BuiltinCategory builtinCategory(const vloom::PluginInfo& builtin);

/// The effects and instruments that come with VloomDAW (vloom-engine's
/// built-in processors), as the plugin lists show them: format Builtin, the
/// id as path, a category.
[[nodiscard]] std::vector<PluginInfo> builtinPlugins();

/// What is wrong with the samples of `processor` (the built-in sampler and
/// drum sampler), one line per slot whose file could not be loaded, e.g.
/// "Pad 3: 'C:/kit/snare.wav' not found": for the list of problems a
/// project shows when it opens. Empty when all is well.
[[nodiscard]] std::vector<std::string> sampleProblems(const vloom::AudioProcessor& processor);

/// The CLAP, VST 3 and built-in plugins the project uses, loaded in-process (ADR 0006
/// of vloom-engine: a crashing plugin takes the app down). Each instance has
/// a handle the project stores (PluginSlot::instance). Libraries are opened
/// once per file and kept while the app runs.
///
/// UI thread only. An instance is dropped only by retainOnly(), after the
/// engine was sent states without it: the engine's own references keep it
/// alive until Engine::collectGarbage(), on this thread too.
class PluginHost {
public:
    struct Loaded {
        std::uint64_t handle = 0; ///< 0: failed, see error
        vloom::PluginInfo info{};
        std::string error{};
    };

    PluginHost() = default;
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    /// Loads the plugin of the library at `path` that pickPlugin() chooses
    /// for `id` and `name`, and activates it at `sampleRate` for blocks of
    /// vloom::kMaxBlockFrames. Built-in: the one with `id` (`path` is not
    /// used).
    [[nodiscard]] Loaded load(PluginFormat format, const std::filesystem::path& path, std::string_view id,
        std::string_view name, double sampleRate);

    /// load(), then gives the instance `state` (saved by AudioProcessor::
    /// saveState(); empty: its defaults) before the engine gets it. A state
    /// the plugin refuses leaves it loaded at its defaults, with `error` set.
    [[nodiscard]] Loaded loadWithState(PluginFormat format, const std::filesystem::path& path,
        std::string_view id, std::string_view name, double sampleRate, std::span<const std::uint8_t> state);

    /// How the built-ins that play samples read a file (vloom::
    /// SampleFileResolver): given to every such instance loaded from now on
    /// and to those loaded already. Set it before loading a project.
    void setSampleFileResolver(vloom::SampleFileResolver resolver);

    [[nodiscard]] std::shared_ptr<vloom::PluginInstance> instance(std::uint64_t handle) const;
    /// The rate instance `handle` was last activated at; 0 if there is none.
    [[nodiscard]] double sampleRate(std::uint64_t handle) const;
    /// Activates instance `handle` again at `sampleRate`. Only for an
    /// instance the engine has never been given (Engine::prepare() takes
    /// care of the published ones): the audio thread must not be processing
    /// it. False if there is no such instance or it refused.
    bool reactivate(std::uint64_t handle, double sampleRate);
    [[nodiscard]] PluginResolver resolver() const;
    [[nodiscard]] std::size_t size() const { return instances_.size(); }

    /// Drops every instance whose handle is not in `live`, calling
    /// `beforeDrop` with each first (to close its editor).
    void retainOnly(const std::vector<std::uint64_t>& live,
        const std::function<void(std::uint64_t handle)>& beforeDrop = {});

    /// Returns the live state of instance `handle`, updating its cached last-good
    /// snapshot while `!processor().hasFailed()`. If `handle` is not loaded or its
    /// bridge process ended (`hasFailed()`), returns the last good state (else `fallback`).
    [[nodiscard]] std::vector<std::uint8_t> saveState(
        std::uint64_t handle, std::span<const std::uint8_t> fallback = {}) const;

    /// Runs every instance's main-thread work (AudioProcessor::idle()):
    /// timers, editor edits. Returns handles that newly entered `hasFailed()`.
    std::vector<std::uint64_t> idle();

private:
    std::map<std::filesystem::path, std::shared_ptr<vloom::ClapLibrary>> clap_;
    std::map<std::filesystem::path, std::shared_ptr<vloom::Vst2Library>> vst2_;
    std::map<std::filesystem::path, std::shared_ptr<vloom::Vst3Library>> vst3_;
    std::map<std::uint64_t, std::shared_ptr<vloom::PluginInstance>> instances_;
    std::map<std::uint64_t, double> rates_; ///< what each instance was activated at
    mutable std::map<std::uint64_t, std::vector<std::uint8_t>>
        lastStates_; ///< kept if a bridged plugin fails
    std::set<std::uint64_t> failed_; ///< handles whose `hasFailed()` was already reported
    std::uint64_t next_ = 1;
    vloom::SampleFileResolver sampleResolver_; ///< for built-ins with sample slots
};

} // namespace vloomdaw
