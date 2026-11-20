// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/SampleFiles.hpp"

#include <vloom/engine/plugin/AudioProcessor.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QToolTip>
#include <QWidget>

#include <string>

namespace vloomdaw {

QString chooseSampleFile(QWidget* parent, const QString& current)
{
    const QString start = current.isEmpty() ? QDir::homePath() : QFileInfo(current).absolutePath();
    return QFileDialog::getOpenFileName(parent,
        QCoreApplication::translate("vloomdaw::SampleFiles", "Load Sample"), start,
        QCoreApplication::translate("vloomdaw::SampleFiles",
            "Audio files (*.wav *.mp3 *.flac *.ogg *.aif *.aiff *.m4a *.opus);;All files (*)"));
}

bool loadSampleFile(vloom::AudioProcessor& processor, int slot, const QString& path, QWidget* anchor)
{
    QApplication::setOverrideCursor(Qt::WaitCursor); // a file other than WAV is decoded first
    std::string error;
    const bool loaded = processor.loadSample(slot, path.toStdString(), &error);
    QApplication::restoreOverrideCursor();
    if (!loaded && anchor != nullptr)
        QToolTip::showText(anchor->mapToGlobal(QPoint(0, anchor->height())),
            QCoreApplication::translate("vloomdaw::SampleFiles", "Could not load %1: %2")
                .arg(QFileInfo(path).fileName(), QString::fromStdString(error)),
            anchor);
    return loaded;
}

} // namespace vloomdaw
