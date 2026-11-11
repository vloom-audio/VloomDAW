// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <string>
#include <vector>

namespace vloomdaw {

/// Identifies an audio device across restarts and hot-plugging.
///
/// Device indices change when devices come and go, and two devices can share
/// a name (e.g. two identical monitors), so a device is remembered as its name
/// plus which occurrence of that name it is in the list.
struct DeviceRef {
    std::string name; ///< empty = system default
    int occurrence = 1; ///< 1 = first device with this name

    [[nodiscard]] bool isDefault() const noexcept { return name.empty(); }
    bool operator==(const DeviceRef&) const = default;
};

/// The reference for `names[index]`; the default reference for a bad index.
DeviceRef deviceRefAt(const std::vector<std::string>& names, int index);

/// Index of `ref` in `names`, or -1 when it is the default or not present.
int findDevice(const std::vector<std::string>& names, const DeviceRef& ref);

} // namespace vloomdaw
