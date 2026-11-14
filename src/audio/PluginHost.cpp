// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/PluginHost.hpp"

#include <vloom/engine/mixer/MixerState.hpp>
#include <vloom/engine/plugin/ClapHost.hpp>
#include <vloom/engine/plugin/Vst3Host.hpp>

#include <algorithm>

namespace vloomdaw {

namespace {

/// The plugin to create from `plugins`: by id, else by name, else the first.
const vloom::PluginInfo* pick(
    const std::vector<vloom::PluginInfo>& plugins, std::string_view id, std::string_view name)
{
    if (plugins.empty())
        return nullptr;
    if (!id.empty()) {
        const auto it = std::find_if(
            plugins.begin(), plugins.end(), [id](const vloom::PluginInfo& p) { return p.id == id; });
        return it == plugins.end() ? nullptr : &*it;
    }
    const auto it = std::find_if(
        plugins.begin(), plugins.end(), [name](const vloom::PluginInfo& p) { return p.name == name; });
    return it == plugins.end() ? &plugins.front() : &*it;
}

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
    const vloom::PluginInfo* chosen = pick(library->plugins(), id, name);
    if (chosen == nullptr) {
        loaded.error = id.empty() ? "the file holds no plugins" : "the plugin is no longer in its file";
        return loaded;
    }
    loaded.info = *chosen;
    processor = library->create(chosen->id, &error);
    if (processor == nullptr)
        loaded.error = error.empty() ? "the plugin refused to start" : error;
    return loaded;
}

} // namespace

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
    Loaded loaded = format == PluginFormat::Clap
        ? create(openCached(clap_, path, error), id, name, error, processor)
        : create(openCached(vst3_, path, error), id, name, error, processor);
    if (processor == nullptr)
        return loaded;

    auto instance = std::make_shared<vloom::PluginInstance>(std::move(processor));
    if (!instance->activate(sampleRate, vloom::kMaxBlockFrames)) {
        loaded.error
            = "the plugin could not start at " + std::to_string(static_cast<int>(sampleRate)) + " Hz";
        return loaded;
    }
    loaded.handle = next_++;
    instances_.emplace(loaded.handle, std::move(instance));
    return loaded;
}

std::shared_ptr<vloom::PluginInstance> PluginHost::instance(std::uint64_t handle) const
{
    const auto it = instances_.find(handle);
    return it == instances_.end() ? nullptr : it->second;
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
        it = instances_.erase(it);
    }
}

void PluginHost::idle()
{
    for (auto& [handle, instance] : instances_)
        instance->processor().idle();
}

} // namespace vloomdaw
