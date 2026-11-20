// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/ParameterChoices.hpp"

#include <vloom/engine/plugin/BuiltinProcessors.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace vloomdaw;

namespace {

vloom::ParameterInfo parameterOf(std::string_view id, const std::string& name)
{
    const auto processor = vloom::createBuiltinProcessor(id);
    REQUIRE(processor != nullptr);
    const std::vector<vloom::ParameterInfo> list = processor->parameters();
    const auto it = std::find_if(
        list.begin(), list.end(), [&](const vloom::ParameterInfo& p) { return p.name == name; });
    REQUIRE(it != list.end());
    return *it;
}

} // namespace

TEST_CASE("A tempo-synced note length is a choice named by its note", "[parameters]")
{
    const vloom::ParameterInfo note = parameterOf("vloom.chorus", "Note");
    REQUIRE(isChoice(note));
    CHECK(choiceLabel(note, 9.0) == "1/4");
    CHECK(choiceLabel(note, 10.2) == "1/8 D"); // rounded
    CHECK(choiceLabel(note, 100.0) == "1/32 T"); // kept in range
    CHECK(choiceIndex(note, -5.0) == 0);
    CHECK(choiceValue(note, 12) == 12.0);
    CHECK(choiceValue(note, 99) == note.maxValue);

    const vloom::ParameterInfo delayNote = parameterOf("vloom.delay", "Note");
    REQUIRE(isChoice(delayNote));
    CHECK(choiceLabel(delayNote, 5.0) == "1/8");
}

TEST_CASE("Modes are choices, switches and amounts are not", "[parameters]")
{
    const vloom::ParameterInfo detector = parameterOf("vloom.compressor", "Detector");
    REQUIRE(isChoice(detector)); // 0 .. 1, but named: a list, not a check box
    CHECK(choiceLabel(detector, 1.0) == "RMS");
    const vloom::ParameterInfo type = parameterOf("vloom.filter", "Type");
    CHECK(choiceLabel(type, 3.0) == "Notch");
    const vloom::ParameterInfo key = parameterOf("vloom.sampler", "Root Key");
    CHECK(choiceLabel(key, 60.0) == "C3");

    const vloom::ParameterInfo sync = parameterOf("vloom.delay", "Sync");
    CHECK_FALSE(isChoice(sync));
    CHECK(choiceLabel(sync, 1.0).empty());
    CHECK_FALSE(isChoice(parameterOf("vloom.delay", "Feedback")));

    // Labels that do not name every value are not trusted.
    vloom::ParameterInfo odd = detector;
    odd.valueLabels.pop_back();
    CHECK_FALSE(isChoice(odd));
    odd = detector;
    odd.stepped = false;
    CHECK_FALSE(isChoice(odd));
}
