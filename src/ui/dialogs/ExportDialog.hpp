// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include "audio/FileEncoder.hpp"

#include <QDialog>
#include <QString>

#include <array>

class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolButton;

namespace vloomdaw {

class ToggleSwitch;

/// What the user chose in the export dialog.
struct ExportSettings {
    enum class WavDepth { Pcm16, Pcm24, Float32 };

    QString path; ///< absolute, with the format's extension
    AudioFileFormat format = AudioFileFormat::Wav;
    WavDepth wavDepth = WavDepth::Pcm24;
    int flacBits = 24; ///< 16 or 24
    int bitrateKbps = 256; ///< MP3, Ogg, M4A
    bool mono = false;
    bool dither = true; ///< 16-bit and 24-bit integer formats
    bool tail = true; ///< go on after the end until the sound has died away
    bool showFile = false; ///< open its folder when it is written
    AudioMetadata metadata; ///< the file's tags
};

/// What the dialog shows about the song.
struct ExportSong {
    QString folder; ///< where the file is suggested to go
    QString name; ///< its suggested name, without extension
    int bars = 0;
    double seconds = 0.0; ///< without the tail
    double sampleRate = 48000.0;
};

/// File > Export…: where to write the song, in which format (WAV, FLAC, MP3,
/// Ogg Vorbis, M4A) and how.
///
/// Laid out like the Settings window: the format as a row of tiles, then
/// cards side by side (the sound on the left; the tags and the file on the
/// right), and at the bottom about how large the file will be next to the
/// Export button. The title starts as the project's name; the artist and the
/// genre are remembered, as they tend to stay the same from song to song.
///
/// Formats the app's FFmpeg
/// cannot write are greyed out. The choices are remembered for the next time.
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExportDialog(const ExportSong& song, QWidget* parent = nullptr);

    /// What was chosen; meaningful once the dialog was accepted.
    [[nodiscard]] ExportSettings settings() const;

    void accept() override;

private:
    void formatChanged();
    void refresh();
    void browse();
    [[nodiscard]] AudioFileFormat format() const;
    [[nodiscard]] double estimatedBytes() const;

    ExportSong song_;
    std::array<QToolButton*, 5> formats_{};
    QLabel* qualityName_ = nullptr; ///< "Bit depth" or "Bit rate", by format
    QLabel* qualityHint_ = nullptr;
    QStackedWidget* quality_ = nullptr; ///< the one of the three below that applies
    QComboBox* wavDepth_ = nullptr;
    QComboBox* flacBits_ = nullptr;
    QComboBox* bitrate_ = nullptr;
    QComboBox* channels_ = nullptr;
    ToggleSwitch* dither_ = nullptr;
    QLabel* ditherHint_ = nullptr;
    ToggleSwitch* tail_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* artist_ = nullptr;
    QLineEdit* album_ = nullptr;
    QLineEdit* year_ = nullptr;
    QLineEdit* genre_ = nullptr;
    QLineEdit* comment_ = nullptr;
    QLineEdit* path_ = nullptr;
    ToggleSwitch* showFile_ = nullptr;
    QLabel* size_ = nullptr;
};

} // namespace vloomdaw
