// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/builtin/BuiltinEditors.hpp"

#include "ui/plugins/builtin/AnalyzerEditor.hpp"
#include "ui/plugins/builtin/BitcrusherEditor.hpp"
#include "ui/plugins/builtin/ChorusEditor.hpp"
#include "ui/plugins/builtin/CompressorEditor.hpp"
#include "ui/plugins/builtin/DeEsserEditor.hpp"
#include "ui/plugins/builtin/DelayEditor.hpp"
#include "ui/plugins/builtin/DrumSamplerEditor.hpp"
#include "ui/plugins/builtin/FilterEditor.hpp"
#include "ui/plugins/builtin/FlangerEditor.hpp"
#include "ui/plugins/builtin/GateEditor.hpp"
#include "ui/plugins/builtin/LimiterEditor.hpp"
#include "ui/plugins/builtin/PhaserEditor.hpp"
#include "ui/plugins/builtin/ReverbEditor.hpp"
#include "ui/plugins/builtin/SamplerEditor.hpp"
#include "ui/plugins/builtin/SaturatorEditor.hpp"
#include "ui/plugins/builtin/TransientEditor.hpp"
#include "ui/plugins/builtin/TremoloEditor.hpp"
#include "ui/plugins/builtin/TunerEditor.hpp"
#include "ui/plugins/builtin/UtilityEditor.hpp"

#include <vloom/engine/plugin/PluginInstance.hpp>

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace vloomdaw {

namespace {

using Make
    = BuiltinEditorWindow* (*)(std::shared_ptr<vloom::PluginInstance>, const QString&, double, QWidget*);

template <typename Editor>
BuiltinEditorWindow* make(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
{
    return new Editor(std::move(instance), title, sampleRate, parent);
}

struct Entry {
    std::string_view pluginId;
    Make make;
};

/// Every built-in with a dedicated editor, by plugin id. One line per editor.
constexpr std::array kEditors{
    Entry{"vloom.compressor", &make<CompressorEditor>},
    Entry{"vloom.limiter", &make<LimiterEditor>},
    Entry{"vloom.gate", &make<GateEditor>},
    Entry{"vloom.saturator", &make<SaturatorEditor>},
    Entry{"vloom.utility", &make<UtilityEditor>},
    Entry{"vloom.filter", &make<FilterEditor>},
    Entry{"vloom.bitcrusher", &make<BitcrusherEditor>},
    Entry{"vloom.deesser", &make<DeEsserEditor>},
    Entry{"vloom.transient", &make<TransientEditor>},
    Entry{"vloom.delay", &make<DelayEditor>},
    Entry{"vloom.reverb", &make<ReverbEditor>},
    Entry{"vloom.chorus", &make<ChorusEditor>},
    Entry{"vloom.flanger", &make<FlangerEditor>},
    Entry{"vloom.phaser", &make<PhaserEditor>},
    Entry{"vloom.tremolo", &make<TremoloEditor>},
    Entry{"vloom.sampler", &make<SamplerEditor>},
    Entry{"vloom.drums", &make<DrumSamplerEditor>},
    Entry{"vloom.tuner", &make<TunerEditor>},
    Entry{"vloom.analyzer", &make<AnalyzerEditor>},
};

const Entry* entryFor(const vloom::PluginInstance& instance)
{
    const vloom::AudioProcessor& processor = instance.processor();
    if (processor.hasEditor())
        return nullptr; // a plugin with its own editor keeps it
    const std::string id = processor.info().id;
    for (const Entry& entry : kEditors)
        if (entry.pluginId == id)
            return &entry;
    return nullptr;
}

} // namespace

bool hasBuiltinEditor(const vloom::PluginInstance& instance) { return entryFor(instance) != nullptr; }

BuiltinEditorWindow* createBuiltinEditor(
    std::shared_ptr<vloom::PluginInstance> instance, const QString& title, double sampleRate, QWidget* parent)
{
    if (instance == nullptr)
        return nullptr;
    const Entry* entry = entryFor(*instance);
    return entry != nullptr ? entry->make(std::move(instance), title, sampleRate, parent) : nullptr;
}

} // namespace vloomdaw
