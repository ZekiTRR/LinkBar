#include "AppBar.h"
#include "Common.h"
#include "Localization.h"
#include "Os.h"

#include <commctrl.h>
#include <shellapi.h>
#include <vector>

namespace lb {
namespace {

constexpr UINT_PTR SUBCLASS_ID = 1;

std::vector<RECT> allMonitorRects()
{
    std::vector<RECT> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto* rects = reinterpret_cast<std::vector<RECT>*>(data);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        rects->push_back(info.rcMonitor);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

RECT monitorRect(int index)
{
    const auto rects = allMonitorRects();
    if (rects.empty())
    {
        return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    if (index < 0 || static_cast<std::size_t>(index) >= rects.size())
    {
        index = 0;
    }
    return rects[static_cast<std::size_t>(index)];
}

} // namespace

AppBar::AppBar(HWND owner, PanelAlign align, bool autoHide)
    : owner_(owner), align_(align), autoHide_(autoHide)
{
    const auto rects = allMonitorRects();
    monitorNum_ = static_cast<int>(rects.size()) > 1 ? 1 : 0;
    if (rects.size() <= 1)
    {
        monitorNum_ = 0;
    }

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &AppBar::hiddenWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"LINKBAR_HIDDEN_APPBAR";
    RegisterClassW(&windowClass);
    hiddenWindow_ = CreateWindowExW(WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"",
                                    WS_POPUP | WS_DISABLED, 0, 0, 0, 0,
                                    nullptr, nullptr, windowClass.hInstance, nullptr);
    if (hiddenWindow_ != nullptr)
    {
        SetWindowLongPtrW(hiddenWindow_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(owner_));
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = hiddenWindow_;
        data.uCallbackMessage = LM_AB_CALLBACK;
        SHAppBarMessage(ABM_NEW, &data);
    }
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    allowMessageThroughUipi(owner_, taskbarCreatedMessage_);
    SetWindowSubclass(owner_, &AppBar::subclassProc, SUBCLASS_ID,
                      reinterpret_cast<DWORD_PTR>(this));
}

AppBar::~AppBar()
{
    if (owner_ != nullptr)
    {
        RemoveWindowSubclass(owner_, &AppBar::subclassProc, SUBCLASS_ID);
    }
    if (hiddenWindow_ != nullptr)
    {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = hiddenWindow_;
        SHAppBarMessage(ABM_REMOVE, &data);
        DestroyWindow(hiddenWindow_);
    }
    unregisterAppBar();
}

LRESULT CALLBACK AppBar::hiddenWindowProc(HWND window, UINT message,
                                          WPARAM wparam, LPARAM lparam) noexcept
{
    if (message == LM_AB_CALLBACK && wparam == ABN_FULLSCREENAPP)
    {
        HWND owner = reinterpret_cast<HWND>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (IsWindow(owner))
        {
            SendMessageW(owner, LM_AB_FULLSCREENAPP, 0, lparam);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK AppBar::subclassProc(HWND window, UINT message,
                                      WPARAM wparam, LPARAM lparam,
                                      UINT_PTR, DWORD_PTR reference) noexcept
{
    auto* self = reinterpret_cast<AppBar*>(reference);
    if (self == nullptr)
    {
        return DefSubclassProc(window, message, wparam, lparam);
    }
    switch (message)
    {
    case LM_AB_TASKBARSTARTED:
        if (self->autoHide_)
        {
            self->setAlign(self->align_);
        }
        else
        {
            self->unregisterAppBar();
            self->registerAppBar();
        }
        self->setStayOnTop(self->stayOnTop_);
        return 0;
    case LM_AB_FULLSCREENAPP:
        self->fullScreenApp(lparam != 0);
        return 0;
    case LM_AB_CALLBACK:
        if (wparam == ABN_STATECHANGE || wparam == ABN_POSCHANGED)
        {
            self->appBarQuerySetPos();
        }
        return 0;
    case WM_WINDOWPOSCHANGED:
    {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        SHAppBarMessage(ABM_WINDOWPOSCHANGED, &data);
        break;
    }
    case WM_ACTIVATE:
    {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        SHAppBarMessage(ABM_ACTIVATE, &data);
        return 0;
    }
    default:
        break;
    }
    if (message == self->taskbarCreatedMessage_)
    {
        PostMessageW(window, LM_AB_TASKBARSTARTED, 0, 0);
        return 0;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

void AppBar::loaded()
{
    registerAppBar();
    appBarQuerySetPos();
}

void AppBar::registerAppBar()
{
    if (registered_)
    {
        return;
    }
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uCallbackMessage = LM_AB_CALLBACK;
    SHAppBarMessage(ABM_NEW, &data);
    registered_ = true;
}

void AppBar::unregisterAppBar()
{
    if (!registered_)
    {
        return;
    }
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    SHAppBarMessage(ABM_REMOVE, &data);
    registered_ = false;
}

void AppBar::hiddenSetMonitor(int monitorNum)
{
    if (hiddenWindow_ == nullptr)
    {
        return;
    }
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = hiddenWindow_;
    data.uEdge = ABE_TOP;
    data.rc = monitorRect(monitorNum);
    data.rc.right = data.rc.left;
    data.rc.bottom = data.rc.top;
    SHAppBarMessage(ABM_SETPOS, &data);
    MoveWindow(hiddenWindow_, data.rc.left, data.rc.top, 0, 0, FALSE);
}

void AppBar::appBarQuerySetPos()
{
    const auto rects = allMonitorRects();
    if (monitorNum_ < 0 || static_cast<std::size_t>(monitorNum_) >= rects.size())
    {
        monitorNum_ = 0;
    }
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uEdge = screenEdgeToEdge(align_);
    data.rc = monitorRect(monitorNum_);

    if (!autoHide_)
    {
        SHAppBarMessage(ABM_QUERYPOS, &data);
    }
    int width = rectWidth(data.rc);
    int height = rectHeight(data.rc);
    if (querySizing)
    {
        querySizing(vertical(), width, height);
    }
    switch (data.uEdge)
    {
    case ABE_LEFT: data.rc.right = data.rc.left + width; break;
    case ABE_RIGHT: data.rc.left = data.rc.right - width; break;
    case ABE_TOP: data.rc.bottom = data.rc.top + height; break;
    case ABE_BOTTOM: data.rc.top = data.rc.bottom - height; break;
    }
    if (!autoHide_)
    {
        SHAppBarMessage(ABM_SETPOS, &data);
    }
    if (querySized)
    {
        querySized(data.rc.left, data.rc.top, rectWidth(data.rc), rectHeight(data.rc));
    }
    hiddenSetMonitor(monitorNum_);
}

void AppBar::appBarSetAutoHide(bool enabled)
{
    APPBARDATA data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uEdge = screenEdgeToEdge(align_);
    data.lParam = enabled ? static_cast<LPARAM>(-1) : 0;
    UINT_PTR result = 0;
    if (os().eightOrAbove)
    {
        data.rc = monitorRect(monitorNum_);
        result = SHAppBarMessage(ABM_SETAUTOHIDEBAREX, &data);
    }
    else
    {
        result = SHAppBarMessage(ABM_SETAUTOHIDEBAR, &data);
    }

    if (enabled && result != 0)
    {
        autoHide_ = true;
        autoHideMonitorNum_ = monitorNum_;
        unregisterAppBar();
        if (queryAutoHide)
        {
            queryAutoHide(true);
        }
        SetWindowPos(owner_, stayOnTop_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        appBarQuerySetPos();
    }
    else
    {
        autoHide_ = false;
        registerAppBar();
        SetWindowPos(owner_, stayOnTop_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        appBarQuerySetPos();
        if (queryAutoHide)
        {
            queryAutoHide(false);
        }
        if (enabled)
        {
            const std::wstring text = loadStringResource(LB_FN_TOOLBAR,
                                                          LB_RS_TB_AUTOHIDEALREADYEXISTS);
            MessageBoxW(owner_, text.c_str(), APP_NAME_LINKBAR.data(), MB_ICONINFORMATION | MB_OK);
        }
    }
}

void AppBar::setAlign(PanelAlign value)
{
    if (autoHide_)
    {
        APPBARDATA data{};
        data.cbSize = sizeof(data);
        data.hWnd = owner_;
        data.uEdge = screenEdgeToEdge(align_);
        data.lParam = 0;
        if (os().eightOrAbove)
        {
            data.rc = monitorRect(autoHideMonitorNum_);
            SHAppBarMessage(ABM_SETAUTOHIDEBAREX, &data);
        }
        else
        {
            SHAppBarMessage(ABM_SETAUTOHIDEBAR, &data);
        }
    }
    align_ = value;
    if (autoHide_)
    {
        appBarSetAutoHide(true);
    }
    else
    {
        appBarQuerySetPos();
    }
}

void AppBar::setAutoHide(bool value)
{
    if (autoHide_ == value)
    {
        return;
    }
    appBarSetAutoHide(value);
}

void AppBar::setStayOnTop(bool value)
{
    stayOnTop_ = value;
    HWND parent = nullptr;
    if (!stayOnTop_)
    {
        HWND progman = FindWindowW(L"Progman", L"Program Manager");
        parent = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
    }
    SetWindowLongPtrW(owner_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(parent));
}

void AppBar::setMonitorNum(int value)
{
    const auto rects = allMonitorRects();
    if (value < 0 || static_cast<std::size_t>(value) >= rects.size())
    {
        value = 0;
    }
    monitorNum_ = value;
}

bool AppBar::vertical() const noexcept
{
    return isVertical(align_);
}

void AppBar::fullScreenApp(bool enabled)
{
    SetWindowPos(owner_, enabled ? HWND_BOTTOM : (stayOnTop_ ? HWND_TOPMOST : HWND_NOTOPMOST),
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

} // namespace lb
