// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "audio/MixerAudio.hpp"

#include "core/Decibels.hpp"

#include <vloom/engine/plugin/AudioProcessor.hpp>

#include <memory>

namespace vloomdaw {

namespace {

class MonoSumProcessor final : public vloom::AudioProcessor {
public:
    [[nodiscard]] vloom::PluginInfo info() const override
    {
        return vloom::PluginInfo{
            "vloomdaw.mono", "Mono Sum", "Vloom Audio", "1.0", vloom::PluginInfo::Kind::Effect};
    }

    bool activate(double /*sampleRate*/, int maxBlockFrames) override { return maxBlockFrames > 0; }

    void process(const vloom::ProcessContext& context) noexcept override
    {
        if (context.channels == nullptr || context.numChannels < 2 || context.numFrames <= 0)
            return;
        float* left = context.channels[0];
        float* right = context.channels[1];
        if (left == nullptr || right == nullptr)
            return;
        for (int i = 0; i < context.numFrames; ++i) {
            const float mono = 0.5f * (left[i] + right[i]);
            left[i] = mono;
            right[i] = mono;
        }
    }
};

std::shared_ptr<vloom::PluginInstance> makeMonoSumInstance()
{
    auto instance = std::make_shared<vloom::PluginInstance>(std::make_unique<MonoSumProcessor>());
    (void)instance->activate(48000.0, vloom::kMaxBlockFrames);
    return instance;
}

} // namespace

static_assert(MixerModel::kMaxInserts == static_cast<std::size_t>(vloom::kMaxInserts));
static_assert(MixerModel::kMaxSends <= static_cast<std::size_t>(vloom::kMaxSends));

std::unique_ptr<const vloom::MixerState> engineMixerState(
    const MixerModel& mixer, const PluginResolver& plugins)
{
    auto state = std::make_unique<vloom::MixerState>();
    if (plugins)
        for (const PluginSlot& slot : mixer.track(MixerModel::kMaster).effects)
            if (auto effect = plugins(slot.instance)) {
                effect->setBypassed(slot.bypassed);
                state->masterEffects.push_back(std::move(effect));
            }
    if (mixer.track(MixerModel::kMaster).mono)
        state->masterEffects.push_back(makeMonoSumInstance());
    for (std::size_t i = 1; i < mixer.size() && static_cast<int>(i) <= vloom::kMaxInserts; ++i) {
        const MixerTrack& mt = mixer.track(i);
        vloom::InsertState insert;
        insert.gain = static_cast<float>(mixer.effectiveGain(i));
        insert.pan = static_cast<float>(mt.pan);
        insert.output
            = (mt.output >= 0 && static_cast<std::size_t>(mt.output) < mixer.size()) ? mt.output : 0;
        if (plugins)
            for (const PluginSlot& slot : mt.effects)
                if (auto effect = plugins(slot.instance)) {
                    effect->setBypassed(slot.bypassed);
                    insert.effects.push_back(std::move(effect));
                }
        if (mt.mono)
            insert.effects.push_back(makeMonoSumInstance());
        for (const MixerSend& send : mt.sends) {
            if (send.muted || send.target <= 0 || static_cast<std::size_t>(send.target) >= mixer.size())
                continue;
            vloom::Send engSend;
            engSend.target = send.target;
            engSend.gain = static_cast<float>(faderToGain(send.level));
            engSend.preFader = send.preFader;
            engSend.sidechain = send.sidechain;
            insert.sends.push_back(engSend);
        }
        state->inserts.push_back(std::move(insert));
    }
    return state;
}

std::vector<vloom::MixerRoute> droppedMixerRoutes(const vloom::MixerState& state)
{
    return vloom::droppedRoutes(state);
}

} // namespace vloomdaw
