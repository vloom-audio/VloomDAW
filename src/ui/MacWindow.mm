// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/MacWindow.hpp"

#include <QWidget>

#import <AppKit/AppKit.h>

namespace vloomdaw {

namespace {

NSWindow* nativeWindow(QWidget* window)
{
    if (window == nullptr)
        return nil;
    // winId() of a top-level QWidget is its NSView (it creates one if needed).
    auto* view = reinterpret_cast<NSView*>(window->winId());
    return view.window;
}

} // namespace

void placeMacWindowButtons(QWidget* window, int barHeight, int leftInset)
{
    NSWindow* nsWindow = nativeWindow(window);
    if (nsWindow == nil || barHeight <= 0)
        return;

    NSButton* close = [nsWindow standardWindowButton:NSWindowCloseButton];
    NSButton* minimise = [nsWindow standardWindowButton:NSWindowMiniaturizeButton];
    NSButton* zoom = [nsWindow standardWindowButton:NSWindowZoomButton];
    // The buttons live in a title bar view inside a title bar container view.
    NSView* container = close.superview.superview;
    if (close == nil || minimise == nil || zoom == nil || container == nil)
        return;

    // Keep the system spacing between the buttons.
    const CGFloat spacing = minimise.frame.origin.x - close.frame.origin.x;
    const CGFloat buttonHeight = close.frame.size.height;
    const CGFloat height = static_cast<CGFloat>(barHeight);

    // Make the container as tall as our bar, pinned to the top of the window.
    NSRect containerFrame = container.frame;
    containerFrame.size.height = height;
    containerFrame.origin.y = nsWindow.frame.size.height - height;
    container.frame = containerFrame;

    // Then centre the buttons in it (AppKit's y axis points up).
    const CGFloat y = (height - buttonHeight) / 2.0;
    CGFloat x = static_cast<CGFloat>(leftInset);
    for (NSButton* button in @[ close, minimise, zoom ]) {
        [button setFrameOrigin:NSMakePoint(x, y)];
        x += spacing;
    }
}

void disableMacNativeFullScreen(QWidget* window)
{
    NSWindow* nsWindow = nativeWindow(window);
    if (nsWindow == nil)
        return;
    NSWindowCollectionBehavior behavior = nsWindow.collectionBehavior;
    behavior &= ~(NSWindowCollectionBehaviorFullScreenPrimary | NSWindowCollectionBehaviorFullScreenAuxiliary);
    behavior |= NSWindowCollectionBehaviorFullScreenNone;
    nsWindow.collectionBehavior = behavior;
}

void hideMacWindowTitle(QWidget* window)
{
    NSWindow* nsWindow = nativeWindow(window);
    if (nsWindow != nil)
        nsWindow.titleVisibility = NSWindowTitleHidden;
}

void setMacMenuBarAndDockHidden(bool on)
{
    NSApp.presentationOptions = on
        ? (NSApplicationPresentationAutoHideMenuBar | NSApplicationPresentationAutoHideDock)
        : NSApplicationPresentationDefault;
}

} // namespace vloomdaw
