#include "Hint.h"
#include "DarkTheme.h"
#include "Common.h"

#include <uxtheme.h>

namespace lb {

Tooltip::Tooltip(HWND parent)
{
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr,
                              TTS_NOPREFIX | TTS_ALWAYSTIP,
                              0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window_ == nullptr)
    {
        return;
    }
    SetWindowTheme(window_, L"Explorer", nullptr);
    allowDarkModeForWindow(window_, TRUE);
    themeChanged();
    SendMessageW(window_, TTM_SETMAXTIPWIDTH, 0, 400);

    tool_ = {};
    tool_.cbSize = sizeof(tool_);
    tool_.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
    tool_.uId = reinterpret_cast<UINT_PTR>(this);
    SendMessageW(window_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool_));
}

Tooltip::~Tooltip()
{
    if (window_ != nullptr)
    {
        DestroyWindow(window_);
    }
}

void Tooltip::cancel() noexcept
{
    if (window_ != nullptr)
    {
        SendMessageW(window_, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool_));
    }
}

void Tooltip::hide() noexcept
{
    if (window_ != nullptr)
    {
        SendMessageW(window_, TTM_POP, 0, 0);
    }
}

void Tooltip::themeChanged() noexcept
{
    if (window_ != nullptr)
    {
        SendMessageW(window_, WM_THEMECHANGED, 0, 0);
    }
}

void Tooltip::activate(const POINT& position, const std::wstring& text,
                       HorzAlign horizontal, VertAlign vertical)
{
    if (window_ == nullptr || text.empty())
    {
        return;
    }
    POINT point = position;
    tool_.lpszText = const_cast<wchar_t*>(text.c_str());
    SendMessageW(window_, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool_));
    SendMessageW(window_, TTM_TRACKPOSITION, 0, MAKELONG(point.x, point.y));
    SendMessageW(window_, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool_));

    RECT windowRect{};
    GetWindowRect(window_, &windowRect);
    if (horizontal == HorzAlign::Right)
    {
        point.x -= rectWidth(windowRect);
    }
    else if (horizontal == HorzAlign::Center)
    {
        point.x -= rectWidth(windowRect) / 2;
    }
    if (vertical == VertAlign::Top)
    {
        point.y -= rectHeight(windowRect);
    }
    else if (vertical == VertAlign::Center)
    {
        point.y -= rectHeight(windowRect) / 2;
    }

    RECT fitted = windowRect;
    fitted.left = point.x;
    fitted.top = point.y;
    fitted.right = point.x + rectWidth(windowRect);
    fitted.bottom = point.y + rectHeight(windowRect);

    HMONITOR monitor = MonitorFromPoint(position, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfoW(monitor, &monitorInfo);
    const RECT& screen = monitorInfo.rcMonitor;

    POINT result{fitted.left, fitted.top};
    if (!pointInRect(screen, POINT{fitted.left, fitted.top}) ||
        !pointInRect(screen, POINT{fitted.right, fitted.bottom}))
    {
        result = POINT{fitted.left, fitted.top};
        if (fitted.right > screen.right)
        {
            result.x = screen.right - rectWidth(windowRect);
        }
        if (fitted.bottom > screen.bottom)
        {
            result.y = screen.bottom - rectHeight(windowRect);
        }
        result.x = std::max(result.x, screen.left);
        result.y = std::max(result.y, screen.top);
    }

    SendMessageW(window_, TTM_TRACKPOSITION, 0, MAKELONG(result.x, result.y));
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace lb
