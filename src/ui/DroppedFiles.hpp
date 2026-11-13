// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>

class QMimeData;

namespace vloomdaw {

/// The first local audio file the app can load in a drag (browser or
/// desktop): WAV, or a type it decodes (AudioDecoder: MP3, FLAC, Ogg,
/// AIFF); "" if none. Network paths are refused: file://host/share/x.wav
/// would make Windows open an SMB connection to that host (and send the
/// user's NTLM hash).
[[nodiscard]] QString droppedAudio(const QMimeData* mime);

} // namespace vloomdaw
