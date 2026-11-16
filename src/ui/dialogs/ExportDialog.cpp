// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/dialogs/ExportDialog.hpp"

#include "ui/dialogs/SettingsDialog/SettingsCard.hpp"
#include "ui/widgets/ToggleSwitch/ToggleSwitch.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr std::array kFormats = {AudioFileFormat::Wav, AudioFileFormat::Flac, AudioFileFormat::Mp3,
    AudioFileFormat::Ogg, AudioFileFormat::M4a};

constexpr QLatin1StringView kFormatKey{"export/format"};
constexpr QLatin1StringView kWavDepthKey{"export/wavDepth"};
constexpr QLatin1StringView kFlacBitsKey{"export/flacBits"};
constexpr QLatin1StringView kBitrateKey{"export/bitrate"};
constexpr QLatin1StringView kMonoKey{"export/mono"};
constexpr QLatin1StringView kDitherKey{"export/dither"};
constexpr QLatin1StringView kTailKey{"export/tail"};
constexpr QLatin1StringView kShowFileKey{"export/showFile"};
constexpr QLatin1StringView kArtistKey{"export/artist"};
constexpr QLatin1StringView kGenreKey{"export/genre"};
constexpr int kMaxTagLength = 200; // characters; the encoder cuts by bytes as well

QString extension(AudioFileFormat format)
{
    const std::string_view text = audioFileExtension(format);
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

QString label(AudioFileFormat format)
{
    switch (format) {
    case AudioFileFormat::Wav:
        return QStringLiteral("WAV");
    case AudioFileFormat::Flac:
        return QStringLiteral("FLAC");
    case AudioFileFormat::Mp3:
        return QStringLiteral("MP3");
    case AudioFileFormat::Ogg:
        return QStringLiteral("OGG");
    case AudioFileFormat::M4a:
        return QStringLiteral("M4A");
    }
    return {};
}

/// Selects the item of `box` whose data is `value`, or leaves it.
void select(QComboBox* box, int value)
{
    if (const int index = box->findData(value); index >= 0)
        box->setCurrentIndex(index);
}

} // namespace

ExportDialog::ExportDialog(const ExportSong& song, QWidget* parent)
    : QDialog(parent)
    , song_(song)
{
    setWindowTitle(tr("Export"));
    setObjectName(QStringLiteral("exportDialog"));
    setMinimumWidth(940);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    // The song: what is about to be rendered.
    auto* heading = new QLabel(tr("Export"), this);
    heading->setObjectName(QStringLiteral("settingsHeading"));
    layout->addWidget(heading);
    const int whole = static_cast<int>(std::lround(song_.seconds));
    const QString time = QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
    auto* about = new QLabel(tr("%1  ·  %2 bars  ·  %3  ·  %4 kHz")
                                 .arg(song_.name, QString::number(song_.bars), time,
                                     QLocale().toString(song_.sampleRate / 1000.0, 'g', 4)),
        this);
    about->setObjectName(QStringLiteral("settingsSubtitle"));
    layout->addWidget(about);

    // The format: one tile each, with what it is good for.
    const std::array<QString, 5> hints
        = {tr("Uncompressed"), tr("Lossless"), tr("Universal"), tr("Open format"), tr("AAC")};
    auto* tiles = new QHBoxLayout;
    tiles->setSpacing(8);
    auto* group = new QButtonGroup(this);
    for (std::size_t i = 0; i < kFormats.size(); ++i) {
        auto* tile = new QToolButton(this);
        tile->setObjectName(QStringLiteral("exportFormat"));
        tile->setCheckable(true);
        tile->setAccessibleName(label(kFormats[i]));
        tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        tile->setFixedHeight(62);
        auto* inside = new QVBoxLayout(tile);
        inside->setContentsMargins(12, 9, 12, 9);
        inside->setSpacing(2);
        auto* name = new QLabel(label(kFormats[i]), tile);
        name->setObjectName(QStringLiteral("exportFormatName"));
        auto* hint = new QLabel(hints[i], tile);
        hint->setObjectName(QStringLiteral("exportFormatHint"));
        for (QLabel* each : {name, hint}) {
            each->setAttribute(Qt::WA_TransparentForMouseEvents); // the tile takes the click
            inside->addWidget(each);
        }
        if (!audioEncoderAvailable(kFormats[i])) {
            tile->setEnabled(false);
            tile->setToolTip(tr("This build of VloomDAW cannot write %1.").arg(label(kFormats[i])));
            hint->setText(tr("Not in this build"));
        }
        group->addButton(tile);
        tiles->addWidget(tile);
        formats_[i] = tile;
    }
    layout->addLayout(tiles);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(14);
    auto* left = new QVBoxLayout;
    auto* right = new QVBoxLayout;
    right->setSpacing(14);
    auto* sound = new SettingsCard(tr("SOUND"), this);
    quality_ = new QStackedWidget(sound);
    quality_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    wavDepth_ = new QComboBox(quality_);
    wavDepth_->addItem(tr("16-bit (CD)"), static_cast<int>(ExportSettings::WavDepth::Pcm16));
    wavDepth_->addItem(tr("24-bit"), static_cast<int>(ExportSettings::WavDepth::Pcm24));
    wavDepth_->addItem(tr("32-bit float"), static_cast<int>(ExportSettings::WavDepth::Float32));
    flacBits_ = new QComboBox(quality_);
    flacBits_->addItem(tr("16-bit (CD)"), 16);
    flacBits_->addItem(tr("24-bit"), 24);
    bitrate_ = new QComboBox(quality_);
    for (const int kbps : audioBitrates(AudioFileFormat::Mp3))
        bitrate_->addItem(tr("%1 kbps").arg(kbps), kbps);
    for (QComboBox* box : {wavDepth_, flacBits_, bitrate_})
        quality_->addWidget(box);
    quality_->setFixedHeight(wavDepth_->sizeHint().height());
    qualityHint_ = sound->addRow(tr("Bit depth"), QStringLiteral(" "), quality_);
    qualityName_ = quality_->parentWidget()->findChild<QLabel*>(QStringLiteral("settingsRowName"));

    channels_ = new QComboBox(sound);
    channels_->addItem(tr("Stereo"), 2);
    channels_->addItem(tr("Mono"), 1);
    sound->addRow(tr("Channels"), tr("Mono mixes the two sides into one."), channels_);
    dither_ = new ToggleSwitch(sound);
    dither_->setAccessibleName(tr("Dithering"));
    ditherHint_ = sound->addRow(tr("Dithering"), QStringLiteral(" "), dither_);
    tail_ = new ToggleSwitch(sound);
    tail_->setAccessibleName(tr("Let the sound ring out"));
    sound->addRow(
        tr("Let the sound ring out"), tr("Keeps reverbs and releases after the end, 10 s at most."), tail_);
    left->addWidget(sound);
    left->addStretch(1);

    // The tags: what players show for the file. A grid, each field under its name.
    auto* tagsCard = new SettingsCard(tr("TAGS"), this);
    auto* tagsBox = new QWidget(tagsCard);
    auto* grid = new QGridLayout(tagsBox);
    grid->setContentsMargins(0, 4, 0, 8);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(3);
    const auto field = [&](const QString& name, int row, int column, int span) {
        auto* caption = new QLabel(name, tagsBox);
        caption->setObjectName(QStringLiteral("settingsRowDescription"));
        caption->setContentsMargins(0, 6, 0, 0); // a little air above each row of fields
        auto* edit = new QLineEdit(tagsBox);
        edit->setMaxLength(kMaxTagLength);
        edit->setAccessibleName(name);
        caption->setBuddy(edit);
        grid->addWidget(caption, row, column, 1, span);
        grid->addWidget(edit, row + 1, column, 1, span);
        return edit;
    };
    title_ = field(tr("Title"), 0, 0, 2);
    artist_ = field(tr("Artist"), 0, 2, 2);
    album_ = field(tr("Album"), 2, 0, 2);
    year_ = field(tr("Year"), 2, 2, 1);
    genre_ = field(tr("Genre"), 2, 3, 1);
    comment_ = field(tr("Comment"), 4, 0, 4);
    year_->setMaxLength(4);
    year_->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,4}")), year_));
    year_->setPlaceholderText(tr("e.g. 2026"));
    for (int column = 0; column < 4; ++column)
        grid->setColumnStretch(column, 1);
    tagsCard->addWide(tagsBox);
    right->addWidget(tagsCard);

    auto* file = new SettingsCard(tr("FILE"), this);
    auto* fileRow = new QWidget(file);
    auto* fileLayout = new QHBoxLayout(fileRow);
    fileLayout->setContentsMargins(0, 8, 0, 8);
    path_ = new QLineEdit(fileRow);
    path_->setAccessibleName(tr("File"));
    auto* browseButton = new QPushButton(tr("Browse…"), fileRow);
    fileLayout->addWidget(path_, 1);
    fileLayout->addWidget(browseButton);
    file->addWide(fileRow);
    showFile_ = new ToggleSwitch(file);
    showFile_->setAccessibleName(tr("Show the file when it is done"));
    file->addRow(tr("Show the file when it is done"), tr("Opens its folder."), showFile_);
    right->addWidget(file);
    right->addStretch(1);
    columns->addLayout(left, 1);
    columns->addLayout(right, 1);
    layout->addLayout(columns);
    layout->addStretch(1);

    // About how large it will be, next to the button that makes it.
    auto* footer = new QHBoxLayout;
    size_ = new QLabel(this);
    size_->setObjectName(QStringLiteral("exportSize"));
    auto* cancel = new QPushButton(tr("Cancel"), this);
    auto* start = new QPushButton(tr("Export"), this);
    start->setObjectName(QStringLiteral("accentButton"));
    start->setDefault(true);
    start->setMinimumWidth(110);
    footer->addWidget(size_, 1);
    footer->addWidget(cancel);
    footer->addWidget(start);
    layout->addLayout(footer);

    // What was chosen the last time; a format this build lacks falls back to WAV.
    const QSettings saved;
    std::size_t chosen = 0;
    const QString savedFormat = saved.value(kFormatKey).toString();
    for (std::size_t i = 0; i < kFormats.size(); ++i)
        if (extension(kFormats[i]) == savedFormat && formats_[i]->isEnabled())
            chosen = i;
    formats_[chosen]->setChecked(true);
    select(wavDepth_, saved.value(kWavDepthKey, static_cast<int>(ExportSettings::WavDepth::Pcm24)).toInt());
    select(flacBits_, saved.value(kFlacBitsKey, 24).toInt());
    select(bitrate_, saved.value(kBitrateKey, 256).toInt());
    select(channels_, saved.value(kMonoKey, false).toBool() ? 1 : 2);
    dither_->setChecked(saved.value(kDitherKey, true).toBool());
    tail_->setChecked(saved.value(kTailKey, true).toBool());
    showFile_->setChecked(saved.value(kShowFileKey, false).toBool());
    title_->setText(song_.name.left(kMaxTagLength));
    artist_->setText(saved.value(kArtistKey).toString().left(kMaxTagLength));
    genre_->setText(saved.value(kGenreKey).toString().left(kMaxTagLength));
    path_->setText(QDir::toNativeSeparators(
        QDir(song_.folder).filePath(song_.name + QLatin1Char('.') + extension(format()))));

    connect(group, &QButtonGroup::buttonToggled, this, [this](QAbstractButton*, bool on) {
        if (on)
            formatChanged();
    });
    for (QComboBox* box : {wavDepth_, flacBits_, bitrate_, channels_})
        connect(box, &QComboBox::currentIndexChanged, this, &ExportDialog::refresh);
    connect(browseButton, &QPushButton::clicked, this, &ExportDialog::browse);
    connect(start, &QPushButton::clicked, this, &ExportDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    refresh();
}

AudioFileFormat ExportDialog::format() const
{
    for (std::size_t i = 0; i < kFormats.size(); ++i)
        if (formats_[i]->isChecked())
            return kFormats[i];
    return AudioFileFormat::Wav;
}

void ExportDialog::formatChanged()
{
    // The file keeps its name and takes the format's extension.
    const QFileInfo file(QDir::fromNativeSeparators(path_->text().trimmed()));
    if (!file.fileName().isEmpty())
        path_->setText(QDir::toNativeSeparators(
            file.dir().filePath(file.completeBaseName() + QLatin1Char('.') + extension(format()))));
    refresh();
}

double ExportDialog::estimatedBytes() const
{
    const double channels = channels_->currentData().toInt();
    const double pcmPerSecond = song_.sampleRate * channels;
    switch (format()) {
    case AudioFileFormat::Wav: {
        const auto depth = static_cast<ExportSettings::WavDepth>(wavDepth_->currentData().toInt());
        const double bytes = depth == ExportSettings::WavDepth::Pcm16 ? 2.0
            : depth == ExportSettings::WavDepth::Pcm24                ? 3.0
                                                                      : 4.0;
        return song_.seconds * pcmPerSecond * bytes;
    }
    case AudioFileFormat::Flac:
        // Music usually packs to somewhat over half its size.
        return song_.seconds * pcmPerSecond * (flacBits_->currentData().toInt() / 8.0) * 0.6;
    case AudioFileFormat::Mp3:
    case AudioFileFormat::Ogg:
    case AudioFileFormat::M4a:
        return song_.seconds * bitrate_->currentData().toInt() * 1000.0 / 8.0;
    }
    return 0.0;
}

void ExportDialog::refresh()
{
    const AudioFileFormat chosen = format();
    const bool wav = chosen == AudioFileFormat::Wav;
    const bool flac = chosen == AudioFileFormat::Flac;
    // The first row is the format's own measure of quality.
    quality_->setCurrentWidget(wav ? wavDepth_ : flac ? flacBits_ : bitrate_);
    if (qualityName_ != nullptr)
        qualityName_->setText(wav || flac ? tr("Bit depth") : tr("Bit rate"));
    qualityHint_->setText(wav ? tr("24-bit suits most uses; 32-bit float cannot clip.")
            : flac            ? tr("Lossless either way; 16-bit is what a CD holds.")
                              : tr("Higher is closer to the original, and larger."));
    qualityHint_->setVisible(true);

    // Dither only does something where samples are rounded to integers.
    const bool floating = static_cast<ExportSettings::WavDepth>(wavDepth_->currentData().toInt())
        == ExportSettings::WavDepth::Float32;
    const bool rounded = (wav && !floating) || (flac && flacBits_->currentData().toInt() == 16);
    dither_->setEnabled(rounded);
    ditherHint_->setText(rounded ? tr("A very quiet noise that hides the rounding to 16 or 24 bits.")
                                 : tr("Not needed here: nothing is rounded to whole numbers."));
    ditherHint_->setVisible(true);

    size_->setText(tr("About %1").arg(QLocale().formattedDataSize(static_cast<qint64>(estimatedBytes()), 1)));
}

void ExportDialog::browse()
{
    const QString current = QDir::fromNativeSeparators(path_->text().trimmed());
    const QString filter = tr("%1 audio (*.%2)").arg(label(format()), extension(format()));
    // The dialog here asks before overwriting, so the system's own question is off.
    const QString picked = QFileDialog::getSaveFileName(
        this, tr("Export"), current, filter, nullptr, QFileDialog::DontConfirmOverwrite);
    if (!picked.isEmpty())
        path_->setText(QDir::toNativeSeparators(picked));
}

ExportSettings ExportDialog::settings() const
{
    ExportSettings out;
    out.format = format();
    QString path = QDir::fromNativeSeparators(path_->text().trimmed());
    const QString suffix = QLatin1Char('.') + extension(out.format);
    if (!path.endsWith(suffix, Qt::CaseInsensitive))
        path += suffix;
    out.path
        = QFileInfo(path).isAbsolute() ? QDir::cleanPath(path) : QDir(song_.folder).absoluteFilePath(path);
    out.wavDepth = static_cast<ExportSettings::WavDepth>(wavDepth_->currentData().toInt());
    out.flacBits = flacBits_->currentData().toInt();
    out.bitrateKbps = bitrate_->currentData().toInt();
    out.mono = channels_->currentData().toInt() == 1;
    out.dither = dither_->isChecked();
    out.tail = tail_->isChecked();
    out.showFile = showFile_->isChecked();
    out.metadata.title = title_->text().trimmed().toStdString();
    out.metadata.artist = artist_->text().trimmed().toStdString();
    out.metadata.album = album_->text().trimmed().toStdString();
    out.metadata.year = year_->text().trimmed().toStdString();
    out.metadata.genre = genre_->text().trimmed().toStdString();
    out.metadata.comment = comment_->text().trimmed().toStdString();
    return out;
}

void ExportDialog::accept()
{
    const ExportSettings chosen = settings();
    const QFileInfo file(chosen.path);
    if (path_->text().trimmed().isEmpty() || file.completeBaseName().isEmpty()) {
        QMessageBox::information(this, tr("Export"), tr("Give the file a name."));
        return;
    }
    if (!file.dir().exists()) {
        QMessageBox::information(this, tr("Export"),
            tr("The folder %1 does not exist.").arg(QDir::toNativeSeparators(file.absolutePath())));
        return;
    }
    if (file.exists()
        && QMessageBox::question(this, tr("Export"),
               tr("%1 exists already. Replace it?").arg(file.fileName()), QMessageBox::Yes | QMessageBox::No,
               QMessageBox::No)
            != QMessageBox::Yes)
        return;

    QSettings saved;
    saved.setValue(kFormatKey, extension(chosen.format));
    saved.setValue(kWavDepthKey, static_cast<int>(chosen.wavDepth));
    saved.setValue(kFlacBitsKey, chosen.flacBits);
    saved.setValue(kBitrateKey, chosen.bitrateKbps);
    saved.setValue(kMonoKey, chosen.mono);
    saved.setValue(kDitherKey, chosen.dither);
    saved.setValue(kTailKey, chosen.tail);
    saved.setValue(kShowFileKey, chosen.showFile);
    saved.setValue(kArtistKey, artist_->text().trimmed());
    saved.setValue(kGenreKey, genre_->text().trimmed());
    QDialog::accept();
}

} // namespace vloomdaw
