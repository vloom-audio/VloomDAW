// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>
#include <QStringList>

class QMimeData;

namespace vloomdaw {

/// All local audio files the app can load in a drag (browser or desktop), in
/// drag order, capped at Project::kMaxPlaylistTracks: WAV, or a type it decodes
/// (AudioDecoder: MP3, FLAC, Ogg, AIFF, M4A, Opus). Network paths are refused:
/// file://host/share/x.wav or \\host\share\x.wav would make Windows open an SMB
/// connection to that host (and send the user's NTLM hash).
[[nodiscard]] QStringList droppedAudioFiles(const QMimeData* mime);

/// The first local audio file in a drag, or "" if none.
[[nodiscard]] QString droppedAudio(const QMimeData* mime);

} // namespace vloomdaw
