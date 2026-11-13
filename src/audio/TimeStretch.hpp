// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <memory>

namespace vloom {
class SampleBuffer;
}

namespace vloomdaw {

/// `sample` made `ratio` times as long without changing its pitch (a loop
/// recorded at 146 BPM played at 130: ratio 146 / 130), with Signalsmith
/// Stretch. Same channels and rate. Null for a ratio outside 0.25 .. 4, or
/// a sample too short to stretch.
///
/// Slow (about a tenth of the sample's length in time): call it off the UI
/// thread.
[[nodiscard]] std::shared_ptr<const vloom::SampleBuffer> timeStretch(
    const vloom::SampleBuffer& sample, double ratio);

} // namespace vloomdaw
