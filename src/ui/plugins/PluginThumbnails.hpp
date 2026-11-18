// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <string>
#include <string_view>

class QComboBox;
class QPushButton;
class QToolButton;
class QLabel;

namespace vloomdaw {

/// Disk path where `thumbId`'s PNG preview is stored.
[[nodiscard]] QString pluginThumbnailPath(std::string_view thumbId);

/// True when a saved thumbnail PNG exists on disk (or in cache) for `thumbId`.
[[nodiscard]] bool hasPluginThumbnail(std::string_view thumbId);

/// Loads the saved thumbnail for `thumbId`, optionally scaled to fit within
/// `maxWidth` x `maxHeight` (device-independent logical pixels, preserving HiDPI).
[[nodiscard]] QPixmap loadPluginThumbnail(
    std::string_view thumbId, int maxWidth = 0, int maxHeight = 0, qreal dpr = 1.0);

/// Saves `pixmap` as `thumbId`'s thumbnail PNG (smooth-scaled down to browser
/// preview dimensions) and updates the in-memory thumbnail cache.
bool savePluginThumbnail(std::string_view thumbId, const QPixmap& pixmap);

/// Deletes `thumbId`'s saved thumbnail PNG and evicts it from cache.
bool removePluginThumbnail(std::string_view thumbId);

/// Captures a live plugin view widget (including native HWND child surfaces
/// rendered via Direct2D, OpenGL or JUCE).
[[nodiscard]] QPixmap capturePluginView(QWidget* view);

/// Context and callbacks for the Studio One-style header bar at the top of
/// an open plugin editor window.
struct PluginEditorContext {
    std::string thumbnailId;
    QString locationLabel; ///< e.g. "Insert 1" or "Channel 2"
    QString slotTitle; ///< e.g. "1 - CA-2A" or "Serum 2"
    bool bypassed = false;
    bool favorite = false;
    QStringList programs{};
    int currentProgram = -1;

    std::function<void(bool bypassed)> onSetBypassed;
    std::function<void(bool favorite)> onToggleFavorite;
    std::function<void(int index)> onSelectProgram;
    std::function<void()> onReplace;
    std::function<void()> onRemove;
    std::function<void()> onShowInMixer;
    std::function<void()> onThumbnailChanged;
};

/// Studio One-style 32 px toolbar shown above an open plugin GUI or generic
/// parameter editor: power/bypass toggle, slot dropdown menu (Favorite,
/// Replace, Disable, Remove, Show in Mixer, Update Plug-in Thumbnail), optional
/// factory program selector, and a 1-click "Update Thumbnail" capture button.
class PluginEditorHeader : public QWidget {
    Q_OBJECT

public:
    static constexpr int kHeight = 32;

    PluginEditorHeader(PluginEditorContext context, QWidget* captureTarget, QWidget* editorWindow,
        QWidget* parent = nullptr);

    void setContext(PluginEditorContext context);
    void setCaptureTarget(QWidget* target) { captureTarget_ = target; }
    void setBypassedState(bool bypassed);

private:
    void refreshUi();
    void openSlotMenu();
    void captureAfterDelay(int delayMs);
    void performCapture();

    PluginEditorContext context_;
    QPointer<QWidget> captureTarget_;
    QPointer<QWidget> editorWindow_;
    QToolButton* powerBtn_ = nullptr;
    QLabel* locationLbl_ = nullptr;
    QPushButton* slotMenuBtn_ = nullptr;
    QComboBox* programCombo_ = nullptr;
    QPushButton* captureBtn_ = nullptr;
};

} // namespace vloomdaw
