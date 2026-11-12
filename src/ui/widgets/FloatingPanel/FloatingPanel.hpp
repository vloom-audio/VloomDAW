// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QString>
#include <QWidget>

#include <functional>

namespace vloomdaw {

class WindowButtons;

/// A separate window for a detached panel (mixer, piano roll, playlist): a
/// top-level window with minimise / maximise / close and its own taskbar
/// button, so it can go to another screen or be minimised. Closing it hides
/// the panel; it stays detached until docked. Remembers its geometry under
/// `settingsKey`. Unparented: the main window deletes it.
///
/// On Windows it has no native title bar, like the main window: the window
/// buttons go into the panel's own header, whose empty space moves the
/// window, and the edges still resize it.
class FloatingPanel : public QWidget {
    Q_OBJECT

public:
    FloatingPanel(const QString& title, const QString& settingsKey);

    /// Takes `content` (reparenting it) and fills the window with it.
    /// `addToHeader` receives the window buttons to place in the content's
    /// header (Windows; unused where the native title bar is kept).
    void host(QWidget* content, const std::function<void(QWidget*)>& addToHeader);
    /// Gives the content back (unparented, for the caller to place) and
    /// hides the window.
    [[nodiscard]] QWidget* release();
    [[nodiscard]] QWidget* content() const { return content_; }
    /// True when a saved position was restored (otherwise the caller places it).
    [[nodiscard]] bool hasSavedGeometry() const { return geometryRestored_; }

signals:
    /// The user closed the window (the panel is now hidden, still detached).
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;
#if defined(Q_OS_WIN)
    void paintEvent(QPaintEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void rememberGeometry();
    void updateFrame(); ///< window buttons' icon and the 1 px outline follow the state
    /// True where a press should move the window: the content's header.
    [[nodiscard]] bool isCaptionAt(QPoint pos) const;

    QString settingsKey_;
    QWidget* content_ = nullptr;
    WindowButtons* windowButtons_ = nullptr; ///< Windows only
    bool geometryRestored_ = false;
};

} // namespace vloomdaw
