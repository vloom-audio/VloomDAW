// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "core/DeviceRef.hpp"

namespace vloomdaw {

DeviceRef deviceRefAt(const std::vector<std::string>& names, int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= names.size())
        return {};
    const std::string& name = names[static_cast<std::size_t>(index)];
    int occurrence = 0;
    for (int i = 0; i <= index; ++i) {
        if (names[static_cast<std::size_t>(i)] == name)
            ++occurrence;
    }
    return {name, occurrence};
}

int findDevice(const std::vector<std::string>& names, const DeviceRef& ref)
{
    if (ref.isDefault())
        return -1;
    int seen = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == ref.name && ++seen == ref.occurrence)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace vloomdaw
