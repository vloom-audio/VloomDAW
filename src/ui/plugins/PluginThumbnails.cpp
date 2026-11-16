// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/plugins/PluginThumbnails.hpp"

#include "core/PluginMetadata.hpp"
#include "ui/Theme.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>

namespace vloomdaw {

namespace {

constexpr int kMaxSavedWidth = 420;
constexpr int kMaxSavedHeight = 260;

struct ThumbnailCacheEntry {
    bool checkedDisk = false;
    QPixmap raw;
};

QHash<QString, ThumbnailCacheEntry>& thumbnailCache()
{
    static QHash<QString, ThumbnailCacheEntry> cache;
    return cache;
}

const QString& thumbnailsDirectory()
{
    static const QString dir = [] {
        QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (base.isEmpty())
            base = QDir::homePath() + QStringLiteral("/.vloomdaw");
        return QDir(base).filePath(QStringLiteral("thumbnails"));
    }();
    return dir;
}

/// Scans `thumbnails/` once into a RAM set so checking 300+ plugins never
/// performs per-plugin filesystem `stat()` syscalls.
QSet<QString>& diskThumbnailIds()
{
    static QSet<QString> ids;
    static bool scanned = false;
    if (!scanned) {
        scanned = true;
        const QDir dir(thumbnailsDirectory());
        if (dir.exists()) {
            const QStringList files = dir.entryList({QStringLiteral("*.png")}, QDir::Files);
            ids.reserve(files.size());
            for (const QString& file : files) {
                if (file.size() == 20 && file.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
                    ids.insert(file.left(16).toLower());
            }
        }
    }
    return ids;
}

const ThumbnailCacheEntry& ensureLoaded(std::string_view thumbId)
{
    static const ThumbnailCacheEntry kEmpty{};
    if (!isValidPluginThumbnailId(thumbId))
        return kEmpty;
    const QString key = QString::fromUtf8(thumbId.data(), static_cast<qsizetype>(thumbId.size()));
    auto& cache = thumbnailCache();
    auto it = cache.find(key);
    if (it != cache.end() && it->checkedDisk)
        return *it;

    ThumbnailCacheEntry entry;
    entry.checkedDisk = true;
    if (diskThumbnailIds().contains(key)) {
        const QString path = pluginThumbnailPath(thumbId);
        QPixmap loaded(path, "PNG");
        if (!loaded.isNull())
            entry.raw = std::move(loaded);
        else
            diskThumbnailIds().remove(key);
    }
    it = cache.insert(key, std::move(entry));
    return *it;
}

} // namespace

QString pluginThumbnailPath(std::string_view thumbId)
{
    if (!isValidPluginThumbnailId(thumbId))
        return {};
    const QString file
        = QString::fromUtf8(thumbId.data(), static_cast<qsizetype>(thumbId.size())) + QStringLiteral(".png");
    return QDir(thumbnailsDirectory()).filePath(file);
}

bool hasPluginThumbnail(std::string_view thumbId) { return !ensureLoaded(thumbId).raw.isNull(); }

QPixmap loadPluginThumbnail(std::string_view thumbId, int maxWidth, int maxHeight, qreal dpr)
{
    const QPixmap& raw = ensureLoaded(thumbId).raw;
    if (raw.isNull())
        return {};
    if (maxWidth <= 0 || maxHeight <= 0)
        return raw;

    const qreal scale = std::max<qreal>(1.0, dpr);
    const QSize targetPx(
        static_cast<int>(std::lround(maxWidth * scale)), static_cast<int>(std::lround(maxHeight * scale)));
    QPixmap scaled = raw;
    if (raw.width() > targetPx.width() || raw.height() > targetPx.height())
        scaled = raw.scaled(targetPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(scale);
    return scaled;
}

bool savePluginThumbnail(std::string_view thumbId, const QPixmap& pixmap)
{
    if (!isValidPluginThumbnailId(thumbId) || pixmap.isNull() || pixmap.width() < 8 || pixmap.height() < 8)
        return false;

    const QString dirPath = thumbnailsDirectory();
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    QImage img = pixmap.toImage();
    if (img.isNull())
        return false;
    if (img.width() > kMaxSavedWidth || img.height() > kMaxSavedHeight) {
        img = img.scaled(
            QSize(kMaxSavedWidth, kMaxSavedHeight), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    const QString path = pluginThumbnailPath(thumbId);
    if (!img.save(path, "PNG"))
        return false;

    const QString key = QString::fromUtf8(thumbId.data(), static_cast<qsizetype>(thumbId.size())).toLower();
    diskThumbnailIds().insert(key);
    ThumbnailCacheEntry entry;
    entry.checkedDisk = true;
    entry.raw = QPixmap::fromImage(std::move(img));
    thumbnailCache().insert(key, std::move(entry));
    return true;
}

bool removePluginThumbnail(std::string_view thumbId)
{
    if (!isValidPluginThumbnailId(thumbId))
        return false;
    const QString key = QString::fromUtf8(thumbId.data(), static_cast<qsizetype>(thumbId.size())).toLower();
    diskThumbnailIds().remove(key);
    thumbnailCache().remove(key);
    const QString path = pluginThumbnailPath(thumbId);
    if (!QFileInfo::exists(path))
        return false;
    return QFile::remove(path);
}

QPixmap capturePluginView(QWidget* view)
{
    if (view == nullptr || !view->isVisible() || view->width() <= 0 || view->height() <= 0)
        return {};

    QScreen* screen = view->screen();
    if (screen == nullptr)
        screen = QGuiApplication::primaryScreen();

    if (screen != nullptr) {
        const QPoint topLeft = view->mapToGlobal(QPoint(0, 0));
        QPixmap grabbed = screen->grabWindow(0, topLeft.x(), topLeft.y(), view->width(), view->height());
        if (!grabbed.isNull() && grabbed.width() > 0 && grabbed.height() > 0)
            return grabbed;
    }
    return view->grab();
}

PluginEditorHeader::PluginEditorHeader(
    PluginEditorContext context, QWidget* captureTarget, QWidget* editorWindow, QWidget* parent)
    : QWidget(parent)
    , context_(std::move(context))
    , captureTarget_(captureTarget)
    , editorWindow_(editorWindow)
{
    setObjectName(QStringLiteral("pluginEditorHeader"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(kHeight);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 3, 8, 3);
    row->setSpacing(6);

    powerBtn_ = new QToolButton(this);
    powerBtn_->setObjectName(QStringLiteral("pluginPowerBtn"));
    powerBtn_->setCheckable(true);
    powerBtn_->setChecked(!context_.bypassed);
    powerBtn_->setIcon(Theme::icon(QStringLiteral("power")));
    powerBtn_->setIconSize(QSize(14, 14));
    powerBtn_->setFixedSize(24, 24);
    powerBtn_->setToolTip(tr("Enable / Disable Plug-in"));
    connect(powerBtn_, &QToolButton::toggled, this, [this](bool active) {
        context_.bypassed = !active;
        if (context_.onSetBypassed)
            context_.onSetBypassed(context_.bypassed);
    });
    row->addWidget(powerBtn_);

    locationLbl_ = new QLabel(this);
    locationLbl_->setObjectName(QStringLiteral("pluginHeaderLocation"));
    row->addWidget(locationLbl_);

    slotMenuBtn_ = new QPushButton(this);
    slotMenuBtn_->setObjectName(QStringLiteral("pluginSlotMenuBtn"));
    slotMenuBtn_->setLayoutDirection(Qt::RightToLeft);
    slotMenuBtn_->setIcon(Theme::icon(QStringLiteral("chevron-down")));
    slotMenuBtn_->setIconSize(QSize(12, 12));
    slotMenuBtn_->setFixedHeight(24);
    slotMenuBtn_->setToolTip(tr("Plug-in Actions and Thumbnail Menu"));
    connect(slotMenuBtn_, &QPushButton::clicked, this, &PluginEditorHeader::openSlotMenu);
    row->addWidget(slotMenuBtn_);

    row->addStretch(1);

    captureBtn_ = new QPushButton(tr("Update Thumbnail"), this);
    captureBtn_->setObjectName(QStringLiteral("pluginCaptureBtn"));
    captureBtn_->setIcon(Theme::icon(QStringLiteral("thumbnail")));
    captureBtn_->setIconSize(QSize(14, 14));
    captureBtn_->setFixedHeight(24);
    captureBtn_->setToolTip(tr("Capture the current plug-in window as its browser thumbnail"));
    connect(captureBtn_, &QPushButton::clicked, this, [this] { captureAfterDelay(40); });
    row->addWidget(captureBtn_);

    refreshUi();
}

void PluginEditorHeader::setContext(PluginEditorContext context)
{
    context_ = std::move(context);
    refreshUi();
}

void PluginEditorHeader::refreshUi()
{
    setBypassedState(context_.bypassed);

    locationLbl_->setText(context_.locationLabel);
    locationLbl_->setVisible(!context_.locationLabel.isEmpty());

    const QString title = context_.slotTitle.isEmpty() ? tr("Plug-in") : context_.slotTitle;
    slotMenuBtn_->setText(title + QStringLiteral(" "));

    const bool canThumb = !context_.thumbnailId.empty();
    captureBtn_->setVisible(canThumb);
}

void PluginEditorHeader::setBypassedState(bool bypassed)
{
    context_.bypassed = bypassed;
    powerBtn_->blockSignals(true);
    powerBtn_->setChecked(!bypassed);
    powerBtn_->blockSignals(false);
    powerBtn_->setEnabled(static_cast<bool>(context_.onSetBypassed));
}

void PluginEditorHeader::openSlotMenu()
{
    QMenu menu(this);

    if (context_.onToggleFavorite) {
        QAction* favAct = menu.addAction(tr("Favorite"));
        favAct->setCheckable(true);
        favAct->setChecked(context_.favorite);
        connect(favAct, &QAction::toggled, this, [this](bool fav) {
            context_.favorite = fav;
            if (context_.onToggleFavorite)
                context_.onToggleFavorite(fav);
        });
        menu.addSeparator();
    }

    if (context_.onReplace) {
        QAction* replaceAct = menu.addAction(tr("Replace..."));
        connect(replaceAct, &QAction::triggered, this, [this] {
            if (context_.onReplace)
                context_.onReplace();
        });
    }

    if (context_.onSetBypassed) {
        QAction* disableAct = menu.addAction(tr("Disable"));
        disableAct->setCheckable(true);
        disableAct->setChecked(context_.bypassed);
        connect(disableAct, &QAction::toggled, this, [this](bool disabled) {
            setBypassedState(disabled);
            if (context_.onSetBypassed)
                context_.onSetBypassed(disabled);
        });
    }

    if (context_.onRemove) {
        QAction* removeAct = menu.addAction(tr("Remove"));
        connect(removeAct, &QAction::triggered, this, [this] {
            auto removeFn = context_.onRemove;
            QPointer<QWidget> win = editorWindow_;
            if (win != nullptr)
                win->close();
            if (removeFn)
                removeFn();
        });
    }

    if (context_.onShowInMixer) {
        if (!menu.isEmpty())
            menu.addSeparator();
        QAction* showConsoleAct = menu.addAction(tr("Show in Console"));
        connect(showConsoleAct, &QAction::triggered, this, [this] {
            if (context_.onShowInMixer)
                context_.onShowInMixer();
        });
    }

    if (!context_.thumbnailId.empty()) {
        if (!menu.isEmpty())
            menu.addSeparator();
        QAction* updateThumbAct
            = menu.addAction(Theme::icon(QStringLiteral("thumbnail")), tr("Update Plug-in Thumbnail"));
        connect(updateThumbAct, &QAction::triggered, this, [this] {
            // Wait briefly so the popup QMenu closes and DWM composites the clean plugin view.
            captureAfterDelay(150);
        });

        if (hasPluginThumbnail(context_.thumbnailId)) {
            QAction* clearThumbAct = menu.addAction(tr("Remove Plug-in Thumbnail"));
            connect(clearThumbAct, &QAction::triggered, this, [this] {
                if (removePluginThumbnail(context_.thumbnailId) && context_.onThumbnailChanged)
                    context_.onThumbnailChanged();
            });
        }
    }

    menu.exec(slotMenuBtn_->mapToGlobal(QPoint(0, slotMenuBtn_->height() + 2)));
}

void PluginEditorHeader::captureAfterDelay(int delayMs)
{
    QPointer<PluginEditorHeader> self(this);
    QTimer::singleShot(delayMs, this, [self] {
        if (self != nullptr)
            self->performCapture();
    });
}

void PluginEditorHeader::performCapture()
{
    if (context_.thumbnailId.empty() || captureTarget_ == nullptr)
        return;

    if (editorWindow_ != nullptr) {
        editorWindow_->raise();
        editorWindow_->repaint();
    }

    const QPixmap shot = capturePluginView(captureTarget_);
    if (!savePluginThumbnail(context_.thumbnailId, shot))
        return;

    if (context_.onThumbnailChanged)
        context_.onThumbnailChanged();

    if (captureBtn_ != nullptr) {
        captureBtn_->setText(tr("Thumbnail Saved"));
        QPointer<QPushButton> btn(captureBtn_);
        const QString normalLabel = tr("Update Thumbnail");
        QTimer::singleShot(1300, this, [btn, normalLabel] {
            if (btn != nullptr)
                btn->setText(normalLabel);
        });
    }
}

} // namespace vloomdaw
