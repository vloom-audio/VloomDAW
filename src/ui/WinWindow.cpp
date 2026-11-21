// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Vloom Audio contributors
#include "ui/WinWindow.hpp"

#include "ui/FilePaths.hpp"

#include <QDir>
#include <QFileInfo>
#include <QWidget>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <cmath>
#include <dwmapi.h>
#include <shlobj.h>
#include <string>
#include <windows.h>
#include <windowsx.h>

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

namespace {

bool setHkcuClassString(const wchar_t* subkey, const wchar_t* valueName, const std::wstring& data)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
            nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD byteCount = 0;
    bool matches = false;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &byteCount) == ERROR_SUCCESS
        && type == REG_SZ && byteCount >= sizeof(wchar_t) && byteCount <= 8192) {
        std::wstring existing(byteCount / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(
                key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(existing.data()), &byteCount)
            == ERROR_SUCCESS) {
            while (!existing.empty() && existing.back() == L'\0')
                existing.pop_back();
            matches = (existing == data);
        }
    }

    bool changed = false;
    if (!matches) {
        const auto bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
        if (RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(data.c_str()), bytes)
            == ERROR_SUCCESS) {
            changed = true;
        }
    }

    RegCloseKey(key);
    return changed;
}

} // namespace

void setWindowsProjectFileAssociation(const QString& exePath, bool enabled)
{
    static constexpr const wchar_t* kProgId = L"VloomDAW.Project";
    if (!enabled) {
        HKEY extKey = nullptr;
        bool changed = false;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.vloom", 0, KEY_READ | KEY_WRITE, &extKey)
            == ERROR_SUCCESS) {
            wchar_t buf[128]{};
            DWORD bytes = sizeof(buf);
            DWORD type = 0;
            if (RegQueryValueExW(extKey, nullptr, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &bytes)
                    == ERROR_SUCCESS
                && type == REG_SZ && std::wstring_view(buf) == kProgId) {
                if (RegDeleteValueW(extKey, nullptr) == ERROR_SUCCESS)
                    changed = true;
            }
            RegCloseKey(extKey);
        }
        if (RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\VloomDAW.Project") == ERROR_SUCCESS)
            changed = true;
        if (changed)
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        return;
    }

    if (!isLocalPath(exePath))
        return;
    const QFileInfo info(exePath);
    if (!info.exists())
        return;

    const std::wstring nativeExe = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    const std::wstring docIcon = L"\"" + nativeExe + L"\",1";
    const std::wstring appIcon = L"\"" + nativeExe + L"\",0";
    const std::wstring openCmd = L"\"" + nativeExe + L"\" \"%1\"";

    bool changed = false;
    changed |= setHkcuClassString(L"Software\\Classes\\.vloom", nullptr, kProgId);
    changed |= setHkcuClassString(L"Software\\Classes\\.vloom\\OpenWithProgids", kProgId, L"");
    changed |= setHkcuClassString(L"Software\\Classes\\VloomDAW.Project", nullptr, L"VloomDAW Project");
    changed |= setHkcuClassString(L"Software\\Classes\\VloomDAW.Project\\DefaultIcon", nullptr, docIcon);
    changed |= setHkcuClassString(
        L"Software\\Classes\\VloomDAW.Project\\shell\\open", nullptr, L"Open with VloomDAW");
    changed |= setHkcuClassString(L"Software\\Classes\\VloomDAW.Project\\shell\\open", L"Icon", appIcon);
    changed
        |= setHkcuClassString(L"Software\\Classes\\VloomDAW.Project\\shell\\open\\command", nullptr, openCmd);

    if (changed)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

bool windowsPrefersReducedMotion()
{
    BOOL animate = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) == FALSE)
        return false;
    return animate == FALSE;
}

} // namespace vloomdaw
