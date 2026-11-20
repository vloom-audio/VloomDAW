// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>

class QWidget;

namespace vloom {
class AudioProcessor;
}

namespace vloomdaw {

/// Loading a file into a sample slot of a built-in sampler or drum sampler
/// (AudioProcessor::loadSample()), for every editor that does: the generic
/// ParameterEditorWindow and the dedicated ones. The host's
/// SampleFileResolver (MainWindow) decodes the types other than WAV and
/// refuses network paths.

/// Asks for an audio file; empty when none is chosen. `current`: the file
/// the slot has, whose folder the dialog opens in (else the home folder).
[[nodiscard]] QString chooseSampleFile(QWidget* parent, const QString& current);

/// Loads `path` into `slot` of `processor`, with a wait cursor while a file
/// is decoded. On failure the slot keeps what it had and a tooltip under
/// `anchor` says why. True when it loaded.
bool loadSampleFile(vloom::AudioProcessor& processor, int slot, const QString& path, QWidget* anchor);

} // namespace vloomdaw
