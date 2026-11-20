// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/PluginHost.hpp"

#include <vloom/engine/mixer/MixerState.hpp>
#include <vloom/engine/plugin/BuiltinProcessors.hpp>
#include <vloom/engine/plugin/ClapHost.hpp>
#include <vloom/engine/plugin/Vst2Host.hpp>
#include <vloom/engine/plugin/Vst3Host.hpp>

#include <algorithm>

namespace vloomdaw {

PluginPick pickPlugin(
    const std::vector<vloom::PluginInfo>& plugins, std::string_view id, std::string_view name)
{
    if (plugins.empty())
        return {nullptr, "the file holds no plugins"};
    if (!id.empty()) {
        const auto it = std::find_if(
            plugins.begin(), plugins.end(), [id](const vloom::PluginInfo& p) { return p.id == id; });
        if (it == plugins.end())
            return {nullptr, "the plugin is no longer in its file"};
        return {&*it, {}};
    }
    const auto it = std::find_if(
        plugins.begin(), plugins.end(), [name](const vloom::PluginInfo& p) { return p.name == name; });
    if (it != plugins.end())
        return {&*it, {}};
    if (plugins.size() == 1)
        return {&plugins.front(), {}}; // the scan named it after its file
    return {nullptr,
        "the file holds " + std::to_string(plugins.size())
            + " plugins and none is named so; scan again with \"Read vendor and category\" on to list each"};
}

namespace {

template <typename Library>
std::shared_ptr<Library> openCached(std::map<std::filesystem::path, std::shared_ptr<Library>>& cache,
    const std::filesystem::path& path, std::string& error)
{
    if (const auto it = cache.find(path); it != cache.end())
        return it->second;
    auto library = Library::open(path, &error);
    if (library != nullptr)
        cache.emplace(path, library);
    return library;
}

template <typename Library>
PluginHost::Loaded create(const std::shared_ptr<Library>& library, std::string_view id, std::string_view name,
    std::string& error, std::unique_ptr<vloom::AudioProcessor>& processor)
{
    PluginHost::Loaded loaded;
    if (library == nullptr) {
        loaded.error = error.empty() ? "not a plugin this app can load" : error;
        return loaded;
    }
    const PluginPick picked = pickPlugin(library->plugins(), id, name);
    const vloom::PluginInfo* chosen = picked.plugin;
    if (chosen == nullptr) {
        loaded.error = picked.error;
        return loaded;
    }
    loaded.info = *chosen;
    processor = library->create(chosen->id, &error);
    if (processor == nullptr)
        loaded.error = error.empty() ? "the plugin refused to start" : error;
    return loaded;
}

} // namespace

BuiltinCategory builtinCategory(const vloom::PluginInfo& builtin)
{
    const std::string& kind = builtin.category; // as vloom-engine names it
    if (builtin.kind == vloom::PluginInfo::Kind::Instrument || kind == "Synth" || kind == "Sampler"
        || kind == "Drums") {
        // The plugin lists tell instruments from effects by category, and
        // their type by tags (core/PluginMetadata).
        if (kind == "Drums")
            return {"Instrument", "Instrument|Drum"};
        if (kind == "Sampler")
            return {"Instrument", "Instrument|Sampler"};
        return {"Instrument", "Instrument|Synth"};
    }
    // The engine's groups, split into the app's own categories.
    std::string category = "Utility";
    if (kind == "Dynamics")
        category = "Dynamics";
    else if (kind == "EQ/Filter")
        category = builtin.id == "vloom.eq" ? "EQ" : "Filter";
    else if (kind == "Delay/Reverb")
        category = builtin.id == "vloom.reverb" ? "Reverb" : "Delay";
    else if (kind == "Modulation")
        category = "Modulation";
    else if (kind == "Distortion")
        category = "Distortion";
    return {category, "Fx|" + category};
}

std::vector<PluginInfo> builtinPlugins()
{
    std::vector<PluginInfo> list;
    for (const vloom::PluginInfo& builtin : vloom::builtinProcessors()) {
        const BuiltinCategory kind = builtinCategory(builtin);
        PluginInfo info;
        info.name = builtin.name;
        info.format = PluginFormat::Builtin;
        info.path = builtin.id;
        info.vendor = builtin.vendor;
        info.category = kind.category;
        info.version = builtin.version;
        info.tags = kind.tags;
        info.probe = PluginInfo::Probe::Ok;
        list.push_back(std::move(info));
    }
    return list;
}

std::vector<std::string> sampleProblems(const vloom::AudioProcessor& processor)
{
    std::vector<std::string> problems;
    for (int slot = 0; slot < processor.sampleSlotCount(); ++slot) {
        const vloom::SampleSlotInfo info = processor.sampleSlot(slot);
        if (info.path.empty() || info.error.empty())
            continue;
        problems.push_back(info.name + ": " + info.error);
    }
    return problems;
}

PluginHost::~PluginHost()
{
    // Editors close before their instances go (ADR 0009).
    for (auto& [handle, instance] : instances_)
        if (instance->processor().isEditorOpen())
            instance->processor().closeEditor();
}

PluginHost::Loaded PluginHost::load(PluginFormat format, const std::filesystem::path& path,
    std::string_view id, std::string_view name, double sampleRate)
{
    std::string error;
    std::unique_ptr<vloom::AudioProcessor> processor;
    Loaded loaded;
    if (format == PluginFormat::Builtin) {
        processor = vloom::createBuiltinProcessor(id);
        if (processor != nullptr)
            loaded.info = processor->info();
        else
            loaded.error = "this version of VloomDAW has no built-in effect \"" + std::string(id) + "\"";
    } else if (format == PluginFormat::Clap) {
        loaded = create(openCached(clap_, path, error), id, name, error, processor);
    } else if (format == PluginFormat::Vst2) {
        loaded = create(openCached(vst2_, path, error), id, name, error, processor);
    } else {
        loaded = create(openCached(vst3_, path, error), id, name, error, processor);
    }
    if (processor == nullptr)
        return loaded;

    // Built-ins that play samples read the files the app decodes (MP3,
    // FLAC, ...) through the resolver, also when a state is loaded.
    if (sampleResolver_ && processor->sampleSlotCount() > 0)
        processor->setSampleFileResolver(sampleResolver_);
    auto instance = std::make_shared<vloom::PluginInstance>(std::move(processor));
    if (!instance->activate(sampleRate, vloom::kMaxBlockFrames)) {
        loaded.error
            = "the plugin could not start at " + std::to_string(static_cast<int>(sampleRate)) + " Hz";
        return loaded;
    }
    loaded.handle = next_++;
    if (!instance->processor().hasFailed())
        lastStates_[loaded.handle] = instance->processor().saveState();
    instances_.emplace(loaded.handle, std::move(instance));
    rates_[loaded.handle] = sampleRate;
    return loaded;
}

PluginHost::Loaded PluginHost::loadWithState(PluginFormat format, const std::filesystem::path& path,
    std::string_view id, std::string_view name, double sampleRate, std::span<const std::uint8_t> state)
{
    Loaded loaded = load(format, path, id, name, sampleRate);
    if (loaded.handle == 0 || state.empty())
        return loaded;
    auto inst = instance(loaded.handle);
    if (!inst->processor().loadState(state)) {
        loaded.error = "its saved settings could not be restored";
    } else if (!inst->processor().hasFailed()) {
        std::vector<std::uint8_t> saved = inst->processor().saveState();
        lastStates_[loaded.handle]
            = saved.empty() ? std::vector<std::uint8_t>(state.begin(), state.end()) : std::move(saved);
    } else {
        lastStates_[loaded.handle].assign(state.begin(), state.end());
    }
    return loaded;
}

void PluginHost::setSampleFileResolver(vloom::SampleFileResolver resolver)
{
    sampleResolver_ = std::move(resolver);
    for (auto& [handle, instance] : instances_)
        if (instance->processor().sampleSlotCount() > 0)
            instance->processor().setSampleFileResolver(sampleResolver_);
}

std::shared_ptr<vloom::PluginInstance> PluginHost::instance(std::uint64_t handle) const
{
    const auto it = instances_.find(handle);
    return it == instances_.end() ? nullptr : it->second;
}

double PluginHost::sampleRate(std::uint64_t handle) const
{
    const auto it = rates_.find(handle);
    return it == rates_.end() ? 0.0 : it->second;
}

bool PluginHost::reactivate(std::uint64_t handle, double sampleRate)
{
    const auto it = instances_.find(handle);
    if (it == instances_.end() || !it->second->activate(sampleRate, vloom::kMaxBlockFrames))
        return false;
    rates_[handle] = sampleRate;
    return true;
}

PluginResolver PluginHost::resolver() const
{
    return [this](std::uint64_t handle) { return instance(handle); };
}

void PluginHost::retainOnly(
    const std::vector<std::uint64_t>& live, const std::function<void(std::uint64_t handle)>& beforeDrop)
{
    for (auto it = instances_.begin(); it != instances_.end();) {
        if (std::find(live.begin(), live.end(), it->first) != live.end()) {
            ++it;
            continue;
        }
        if (beforeDrop)
            beforeDrop(it->first);
        if (it->second->processor().isEditorOpen())
            it->second->processor().closeEditor();
        rates_.erase(it->first);
        lastStates_.erase(it->first);
        failed_.erase(it->first);
        it = instances_.erase(it);
    }
}

std::vector<std::uint8_t> PluginHost::saveState(
    std::uint64_t handle, std::span<const std::uint8_t> fallback) const
{
    const auto it = instances_.find(handle);
    if (it != instances_.end() && !it->second->processor().hasFailed()) {
        std::vector<std::uint8_t> state = it->second->processor().saveState();
        lastStates_[handle] = state;
        return state;
    }
    if (const auto cached = lastStates_.find(handle); cached != lastStates_.end() && !cached->second.empty())
        return cached->second;
    return {fallback.begin(), fallback.end()};
}

std::vector<std::uint64_t> PluginHost::idle()
{
    std::vector<std::uint64_t> newlyFailed;
    for (auto& [handle, instance] : instances_) {
        instance->processor().idle();
        if (instance->processor().hasFailed() && failed_.insert(handle).second)
            newlyFailed.push_back(handle);
    }
    return newlyFailed;
}

} // namespace vloomdaw
