// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>
#include <QWidget>

#include <memory>
#include <vector>

class QLabel;
class QToolButton;

namespace vloomdaw {

/// The browser's preview strip, under its list: the selected file's name and
/// length, its waveform with a line where the preview is playing, and a
/// play / stop button. Click the waveform to play from the start too.
/// A view only: MainWindow feeds it from the SamplePreviewer.
class SampleWaveform : public QWidget {
    Q_OBJECT

public:
    explicit SampleWaveform(QWidget* parent = nullptr);

    /// Shows `path` while its waveform is loading; "" hides the strip.
    void setFile(const QString& path);
    [[nodiscard]] QString file() const { return path_; }
    /// The waveform of `path` (ignored unless it is the file shown).
    void setWaveform(const QString& path, std::shared_ptr<const std::vector<float>> waveform, double seconds);
    /// `path` could not be loaded (ignored unless it is the file shown).
    void setFailed(const QString& path, const QString& error);
    /// Where the preview of the file shown is, in seconds; < 0: not playing.
    void setPlaying(double seconds);

signals:
    void playRequested(const QString& path);
    void stopRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    [[nodiscard]] QRect waveRect() const;
    void updateButton();
    void updateLabels();

    QToolButton* play_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* length_ = nullptr;
    QString path_;
    std::shared_ptr<const std::vector<float>> waveform_;
    double seconds_ = 0.0;
    double playing_ = -1.0; ///< seconds into the file; < 0: not playing
    QString message_; ///< "Loading…" or why it failed, in place of the waveform
};

} // namespace vloomdaw
