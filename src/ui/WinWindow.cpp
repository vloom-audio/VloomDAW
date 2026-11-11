// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/WinWindow.hpp"

#include <QWidget>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <cmath>

namespace vloomdaw {

namespace {

/// How far into the window, in logical pixels, the edges resize it.
constexpr int kResizeBorder = 6;

HWND handleOf(QWidget* window) { return reinterpret_cast<HWND>(window->winId()); }

/// Width of the frame Windows would draw around a sizable window, in physical
/// pixels. A maximized window overhangs the screen by this much.
int frameThickness(HWND hwnd)
{
    const UINT dpi = GetDpiForWindow(hwnd);
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

} // namespace

void removeWindowsTitleBar(QWidget* window)
{
    // Qt then treats the whole window as client area and adds no frame
    // margins of its own; the styles below bring back what Windows needs for
    // resizing, snapping and the minimize / maximize animations.
    window->setWindowFlag(Qt::FramelessWindowHint);
    const HWND hwnd = handleOf(window); // creates the native window

    auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    style &= ~static_cast<DWORD>(WS_POPUP);
    style |= WS_OVERLAPPEDWINDOW;
    SetWindowLongPtrW(hwnd, GWL_STYLE, static_cast<LONG_PTR>(style));

    // With no non-client area left, DWM drops the window shadow. Extending
    // the frame one pixel into the client area brings it back; Qt paints
    // over that pixel.
    const MARGINS shadow{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &shadow);

    // Recalculate the client area through WM_NCCALCSIZE below.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
}

bool handleWindowsTitleBarMessage(
    QWidget* window, void* message, qintptr* result, const std::function<bool(QPoint)>& isCaption)
{
    const auto* msg = static_cast<const MSG*>(message);
    const HWND hwnd = msg->hwnd;

    switch (msg->message) {
    case WM_NCCALCSIZE: {
        if (msg->wParam == FALSE)
            return false;
        // No non-client area: the client rectangle is the window rectangle.
        // A maximized window is larger than the screen by its frame on every
        // side, so pull the client area back onto the screen.
        if (IsZoomed(hwnd) != FALSE) {
            auto& rect = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam)->rgrc[0];
            const int frame = frameThickness(hwnd);
            rect.left += frame;
            rect.top += frame;
            rect.right -= frame;
            rect.bottom -= frame;
        }
        *result = 0;
        return true;
    }

    case WM_NCHITTEST: {
        if (window->isFullScreen())
            return false;

        POINT pt{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        RECT rect{};
        GetWindowRect(hwnd, &rect);

        const qreal dpr = window->devicePixelRatioF();
        if (IsZoomed(hwnd) == FALSE) {
            const int border = static_cast<int>(std::lround(kResizeBorder * dpr));
            const bool left = pt.x < rect.left + border;
            const bool right = pt.x >= rect.right - border;
            const bool top = pt.y < rect.top + border;
            const bool bottom = pt.y >= rect.bottom - border;
            LRESULT hit = HTNOWHERE;
            if (top && left)
                hit = HTTOPLEFT;
            else if (top && right)
                hit = HTTOPRIGHT;
            else if (bottom && left)
                hit = HTBOTTOMLEFT;
            else if (bottom && right)
                hit = HTBOTTOMRIGHT;
            else if (left)
                hit = HTLEFT;
            else if (right)
                hit = HTRIGHT;
            else if (top)
                hit = HTTOP;
            else if (bottom)
                hit = HTBOTTOM;
            if (hit != HTNOWHERE) {
                *result = hit;
                return true;
            }
        }

        ScreenToClient(hwnd, &pt);
        const QPoint local(static_cast<int>(pt.x / dpr), static_cast<int>(pt.y / dpr));
        if (isCaption && isCaption(local)) {
            *result = HTCAPTION;
            return true;
        }
        *result = HTCLIENT;
        return true;
    }

    default:
        return false;
    }
}

} // namespace vloomdaw
