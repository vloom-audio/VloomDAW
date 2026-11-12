// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#pragma once

#include <QLabel>
#include <QMargins>
#include <QPointer>
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
/// It has no title bar of its own, like the main window: the panel's header
/// takes its place and its empty space moves the window. On Windows our
/// window buttons join the header and the edges still resize it; on macOS
/// the content covers the title bar and the system buttons move into the
/// header (Qt 6.9 or later; older Qt keeps the native title bar).
class FloatingPanel : public QWidget {
    Q_OBJECT

public:
    /// The window is titled "VLOOM <title>", e.g. "VLOOM Mixer".
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
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
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

    // The content's header, this window's title bar: it shows "VLOOM" before
    // the panel's title and, on macOS, hosts the system window buttons.
    void adoptHeader(); ///< finds it, adds the app name; macOS: room for the buttons, drags the window
    void releaseHeader(); ///< undoes adoptHeader()
    void placeButtons();
    QPointer<QWidget> header_;
    QPointer<QLabel> appName_; ///< "VLOOM" in header_
    QMargins headerMargins_; ///< the header layout's margins before adoptHeader()
};

} // namespace vloomdaw
