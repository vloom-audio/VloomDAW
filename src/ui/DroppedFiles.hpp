// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>

class QMimeData;

namespace vloomdaw {

/// The first local .wav file in a drag (browser or desktop), or "" (only
/// WAV can be loaded). Network paths are refused: file://host/share/x.wav
/// would make Windows open an SMB connection to that host (and send the
/// user's NTLM hash).
[[nodiscard]] QString droppedWav(const QMimeData* mime);

} // namespace vloomdaw
