#include "Common.h"

#include <algorithm>
#include <cstdio>
#include <shellapi.h>

#ifndef MSGFLT_ALLOW
#define MSGFLT_ALLOW 1
#endif
#ifndef MSGFLT_ADD
#define MSGFLT_ADD 1
#endif

namespace lb {

POINT makePoint(std::uint32_t lparam) noexcept
{
    // GET_X_LPARAM/GET_Y_LPARAM: signed 16-bit values for multi-monitor support.
    return POINT{static_cast<short>(lparam & 0xFFFFU),
                 static_cast<short>((lparam >> 16) & 0xFFFFU)};
}

POINT currentMessagePoint() noexcept
{
    return makePoint(static_cast<std::uint32_t>(GetMessagePos()));
}

POINT centerPoint(const RECT& rect) noexcept
{
    return POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
}

int rectWidth(const RECT& rect) noexcept
{
    return rect.right - rect.left;
}

int rectHeight(const RECT& rect) noexcept
{
    return rect.bottom - rect.top;
}

bool pointInRect(const RECT& rect, const POINT& point) noexcept
{
    return point.x >= rect.left && point.x < rect.right &&
           point.y >= rect.top && point.y < rect.bottom;
}

RECT makeRect(int left, int top, int width, int height) noexcept
{
    return RECT{left, top, left + width, top + height};
}

RECT boundsRect(int left, int top, int width, int height) noexcept
{
    return makeRect(left, top, width, height);
}

bool isVertical(PanelAlign align) noexcept
{
    return align == PanelAlign::Left || align == PanelAlign::Right;
}

bool isHorizontal(PanelAlign align) noexcept
{
    return align == PanelAlign::Top || align == PanelAlign::Bottom;
}

UINT screenEdgeToEdge(PanelAlign align) noexcept
{
    switch (align)
    {
    case PanelAlign::Top: return ABE_TOP;
    case PanelAlign::Bottom: return ABE_BOTTOM;
    case PanelAlign::Left: return ABE_LEFT;
    case PanelAlign::Right: return ABE_RIGHT;
    }
    return ABE_TOP;
}

void reduceSysMenu(HWND window) noexcept
{
    // Note: system menus of popup windows on modern Windows reject item
    // removal (DeleteMenu/RemoveMenu return FALSE). Keep only Close/Move when
    // removal is supported, and never loop if the shell refuses the change.
    HMENU menu = GetSystemMenu(window, FALSE);
    if (menu == nullptr)
    {
        return;
    }
    for (int index = GetMenuItemCount(menu) - 1; index >= 0; --index)
    {
        const UINT id = GetMenuItemID(menu, index);
        if (id == SC_CLOSE || id == SC_MOVE)
        {
            continue;
        }
        if (!RemoveMenu(menu, static_cast<UINT>(index), MF_BYPOSITION))
        {
            break;
        }
    }
}

void preventSizing(LPARAM& result) noexcept
{
    const LPARAM hit = result;
    if (hit == HTCAPTION || hit == HTCLOSE || hit == HTNOWHERE ||
        hit == static_cast<LPARAM>(HTERROR))
    {
        return;
    }
    if (hit == HTTOP || hit == HTTOPLEFT || hit == HTTOPRIGHT)
    {
        result = HTCAPTION;
    }
    else
    {
        result = HTCLIENT;
    }
}

unsigned systemDpi() noexcept
{
    HDC dc = GetDC(nullptr);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return dpi > 0 ? static_cast<unsigned>(dpi) : 96U;
}

unsigned windowDpi(HWND window) noexcept
{
    if (window != nullptr)
    {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
        {
            auto function = reinterpret_cast<GetDpiForWindowFn>(
                GetProcAddress(user32, "GetDpiForWindow"));
            if (function != nullptr)
            {
                const UINT dpi = function(window);
                if (dpi > 0)
                {
                    return dpi;
                }
            }
        }
    }
    return systemDpi();
}

int scaleDimension(int value, unsigned dpi) noexcept
{
    if (dpi == 0)
    {
        dpi = systemDpi();
    }
    return MulDiv(value, static_cast<int>(dpi), 96);
}

void allowMessageThroughUipi(HWND window, UINT message) noexcept
{
    using ChangeWindowMessageFilterExFn = BOOL(WINAPI*)(HWND, UINT, DWORD, void*);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr)
    {
        return;
    }
    auto function = reinterpret_cast<ChangeWindowMessageFilterExFn>(
        GetProcAddress(user32, "ChangeWindowMessageFilterEx"));
    if (function != nullptr)
    {
        function(window, message, MSGFLT_ALLOW, nullptr);
        return;
    }
    using ChangeWindowMessageFilterFn = BOOL(WINAPI*)(UINT, DWORD);
    auto legacy = reinterpret_cast<ChangeWindowMessageFilterFn>(
        GetProcAddress(user32, "ChangeWindowMessageFilter"));
    if (legacy != nullptr)
    {
        legacy(message, MSGFLT_ADD);
    }
}

HBITMAP bitmapFromIcon(HICON icon, int size)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    RECT rect{0, 0, size, size};
    FillRect(dc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
    SelectObject(dc, previous);
    DeleteDC(dc);
    return bitmap;
}

HWND setForegroundWindowInternal(HWND window)
{
    if (!IsWindow(window))
    {
        return nullptr;
    }
    HWND previous = GetForegroundWindow();
    SetForegroundWindow(window);
    if (GetForegroundWindow() == window)
    {
        return previous;
    }

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_MENU;
    SendInput(1, &input, sizeof(input));
    SetForegroundWindow(window);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
    return previous;
}

} // namespace lb
