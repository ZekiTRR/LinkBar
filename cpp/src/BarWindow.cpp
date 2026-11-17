#include "BarWindow.h"

#include "Common.h"
#include "Dialogs.h"
#include "IniFile.h"
#include "Jumplist.h"
#include "Localization.h"
#include "Os.h"
#include "Shell.h"
#include "StringUtil.h"
#include "Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <commctrl.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include "GdiplusCompat.h"

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) (static_cast<int>(static_cast<short>(LOWORD(lp))))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) (static_cast<int>(static_cast<short>(HIWORD(lp))))
#endif

namespace lb {
namespace {

constexpr wchar_t WINDOW_CLASS_NAME[] = L"Linkbar";
BLENDFUNCTION PREMULTIPLIED_BLEND{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
POINT POINT_ZERO{0, 0};
constexpr wchar_t WINDOW_CAPTION[] = L"Linkbar";

enum MenuCommand : UINT
{
    MC_NEW_SHORTCUT = 1,
    MC_NEW_SEPARATOR = 2,
    MC_NEW_LINKBAR = 3,
    MC_REMOVE_BAR = 4,
    MC_OPEN_WORKDIR = 5,
    MC_LOCK_BAR = 6,
    MC_SORT = 7,
    MC_PROPERTIES = 8,
    MC_CLOSE = 9,
    MC_CLOSE_ALL = 10,
};

POINT messagePoint() noexcept
{
    return makePoint(static_cast<std::uint32_t>(GetMessagePos()));
}

double distance(const POINT& a, const POINT& b) noexcept
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

bool windowIsBar(HWND window) noexcept
{
    wchar_t className[256]{};
    return GetClassNameW(window, className, static_cast<int>(std::size(className))) > 0 &&
           iequals(className, WINDOW_CLASS_NAME);
}

BOOL CALLBACK stopWatchProc(HWND window, LPARAM) noexcept
{
    if (windowIsBar(window))
    {
        PostMessageW(window, LM_STOPDIRWATCH, 0, 0);
    }
    return TRUE;
}

BOOL CALLBACK closeProc(HWND window, LPARAM) noexcept
{
    if (windowIsBar(window))
    {
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

} // namespace

void BarWindow::registerWindowClass()
{
    WNDCLASSW windowClass{};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &BarWindow::windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hIcon = LoadIconW(windowClass.hInstance, MAKEINTRESOURCEW(1));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClassW(&windowClass);
}

void BarWindow::closeAll()
{
    const int result = MessageBoxW(nullptr,
        l10n(L"Message.CloseAll", L"Close all linkbars?").c_str(),
        WINDOW_CAPTION, MB_OKCANCEL | MB_ICONQUESTION);
    if (result == IDOK)
    {
        EnumWindows(&stopWatchProc, 0);
        EnumWindows(&closeProc, 0);
    }
}

LRESULT CALLBACK BarWindow::windowProc(HWND window, UINT message,
                                       WPARAM wparam, LPARAM lparam) noexcept
{
    BarWindow* self = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<BarWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(window, message, wparam, lparam);
    }
    else
    {
        self = reinterpret_cast<BarWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    if (self == nullptr)
    {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    return self->handleMessage(message, wparam, lparam);
}

BarWindow::BarWindow(const std::wstring& settingsFile)
    : settingsFile_(settingsFile), referenceCount_(1)
{
    createBitmaps();
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, WINDOW_CLASS_NAME,
                              WINDOW_CAPTION, WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (window_ != nullptr)
    {
        SetWindowLongPtrW(window_, GWL_STYLE, WS_POPUP);
    }
    localize();
    tooltip_ = std::make_unique<Tooltip>(window_);
    loadSettings();
    updateBackgroundColor();
    if (os().isTen)
    {
        setWindowAttribute10(window_, transparencyMode_, backgroundColor_);
    }
    else
    {
        setWindowAttribute78(window_);
    }
    themeInitData(window_, isLightStyle_);
    loadItems();
    updateItemSizes();

    appBar_ = std::make_unique<AppBar>(window_, align_, false);
    appBar_->querySizing = [this](bool vertical, int& w, int& h) { querySizing(vertical, w, h); };
    appBar_->querySized = [this](int x, int y, int w, int h) { querySized(x, y, w, h); };
    appBar_->queryAutoHide = [this](bool enabled) { autoHide_ = enabled; };
    appBar_->setStayOnTop(stayOnTop_);
    appBar_->setMonitorNum(monitorNum_);
    if (!autoHide_)
    {
        appBar_->loaded();
    }
    else
    {
        setAutoHide(true);
    }
    bitBucketNotify_ = registerBitBucketNotify(window_, LM_SHELLNOTIFY);
    if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&dropHelper_))))
    {
        RegisterDragDrop(window_, this);
    }

    created_ = true;
    updateBlur();
    createLinkbarTasksJumplist();
    delayedAutoHide(1000);
}

BarWindow::~BarWindow()
{
    created_ = false;
    closeJumplist();
    RevokeDragDrop(window_);
    if (dropHelper_ != nullptr) dropHelper_->Release();
    unregisterBitBucketNotify(bitBucketNotify_);
    unregisterHotkey();
    directoryWatch_.stop();
    appBar_.reset();
    if (!removed_)
    {
        saveLinks();
    }
    themeCloseData();
    tooltip_.reset();
    CoTaskMemFree(workDirPidl_);
    CoTaskMemFree(dropPidl_);
    if (settingsWindow_ != nullptr && IsWindow(settingsWindow_))
    {
        DestroyWindow(settingsWindow_);
        settingsWindow_ = nullptr;
    }
    if (contextMenuTemplate_ != nullptr)
    {
        DestroyMenu(contextMenuTemplate_);
    }
    if (window_ != nullptr)
    {
        DestroyWindow(window_);
    }
}

int BarWindow::run()
{
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

STDMETHODIMP BarWindow::QueryInterface(REFIID riid, void** ppv)
{
    if (riid == IID_IUnknown || riid == IID_IDropTarget)
    {
        *ppv = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) BarWindow::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&referenceCount_));
}

STDMETHODIMP_(ULONG) BarWindow::Release()
{
    const ULONG result = static_cast<ULONG>(InterlockedDecrement(&referenceCount_));
    if (result == 0)
    {
        delete this;
    }
    return result;
}

LRESULT BarWindow::handleMessage(UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    switch (message)
    {
    case WM_NCHITTEST:
    {
        LPARAM result = DefWindowProcW(window_, message, wparam, lparam);
        preventSizing(result);
        return result;
    }
    case WM_CREATE:
        return 0;
    case WM_THEMECHANGED:
        if (created_)
        {
            themeInitData(window_, isLightStyle_);
        }
        return 0;
    case WM_SETTINGCHANGE:
    {
        const wchar_t* setting = reinterpret_cast<const wchar_t*>(lparam);
        if (created_)
        {
            updateItemSizes();
            recreateMainBitmap(bitmapPanel_.width(), bitmapPanel_.height());
            updateWindow();
            if (setting != nullptr && iequals(setting, L"ImmersiveColorSet") && tooltip_)
            {
                tooltip_->themeChanged();
            }
        }
        return 0;
    }
    case WM_SYSCOLORCHANGE:
        if (created_)
        {
            setHotIndex(ITEM_NONE);
            recreateMainBitmap(bitmapPanel_.width(), bitmapPanel_.height());
            recreateButtonBitmap(buttonSize_.cx, buttonSize_.cy);
            updateWindow();
        }
        return 0;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        if (created_)
        {
            updateBackgroundColor();
            if (os().isTen)
            {
                setWindowAccentPolicy10(window_, transparencyMode_, backgroundColor_);
            }
            setHotIndex(ITEM_NONE);
            recreateMainBitmap(bitmapPanel_.width(), bitmapPanel_.height());
            recreateButtonBitmap(buttonSize_.cx, buttonSize_.cy);
            updateWindow();
        }
        return 0;
    case WM_DWMCOMPOSITIONCHANGED:
        if (created_)
        {
            themeInitData(window_, isLightStyle_);
            updateItemSizes();
            updateBlur();
        }
        return 0;
    case WM_KILLFOCUS:
        canAutoHide_ = (settingsWindow_ == nullptr || !IsWindow(settingsWindow_));
        doAutoHide();
        return 0;
    case WM_DISPLAYCHANGE:
    {
        monitorNum_ = 0;
        HMONITOR monitor = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
        if (monitor != nullptr)
        {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            GetMonitorInfoW(monitor, &info);
            std::vector<RECT> rects;
            EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM data) -> BOOL {
                auto* list = reinterpret_cast<std::vector<RECT>*>(data);
                MONITORINFO mi{};
                mi.cbSize = sizeof(mi);
                GetMonitorInfoW(m, &mi);
                list->push_back(mi.rcMonitor);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&rects));
            for (std::size_t i = 0; i < rects.size(); ++i)
            {
                if (EqualRect(&rects[i], &info.rcMonitor))
                {
                    monitorNum_ = static_cast<int>(i);
                    break;
                }
            }
        }
        if (appBar_)
        {
            appBar_->setMonitorNum(monitorNum_);
            appBar_->appBarPosChanged();
        }
        return 0;
    }
    case WM_TIMER:
        if (wparam == TIMER_AUTO_SHOW)
        {
            KillTimer(window_, TIMER_AUTO_SHOW);
            doAutoShow();
            return 0;
        }
        if (wparam == TIMER_AUTO_HIDE)
        {
            KillTimer(window_, TIMER_AUTO_HIDE);
            doAutoHide();
            return 0;
        }
        if (wparam == TIMER_UPDATE)
        {
            KillTimer(window_, TIMER_UPDATE);
            updateTimerEnabled_ = false;
            for (int i = static_cast<int>(items_.count()) - 1; i >= 0; --i)
            {
                Item& item = items_.at(static_cast<std::size_t>(i));
                if (item.needLoad)
                {
                    if (item.loadFromFile(item.fileName))
                    {
                        items_.loadIcon(item);
                    }
                    else
                    {
                        deleteItem(i);
                    }
                    item.needLoad = false;
                }
            }
            if (sortAlphabetically_)
            {
                items_.sort();
            }
            lastModifiedHash_ = 0;
            renamedOldHash_ = 0;
            setDropPosition(POINT{-1, -1});
            if (appBar_)
            {
                appBar_->appBarPosChanged();
            }
            return 0;
        }
        return 0;
    case WM_HOTKEY:
        if (wparam == LB_HOTKEY_ID && lparam != 0 &&
            static_cast<std::uint16_t>((lparam >> 16) & 0xFFFF) == hotkey_.keyCode &&
            static_cast<std::uint16_t>(lparam & 0xFFFF) == hotkey_.modifiers &&
            autoHide_ && autoHiden_)
        {
            previousForeground_ = setForegroundWindowInternal(window_);
            canAutoHide_ = false;
            doAutoShow();
        }
        return 0;
    case LM_CM_RENAME:
        renameItem(itemPopup_);
        return 0;
    case LM_CM_ITEMS:
        popupMenuCommand(static_cast<int>(lparam));
        return 0;
    case LM_CM_INVOKE:
        closeJumplist();
        return 0;
    case LM_CM_DELETE:
        if (isItemIndex(itemPopup_))
        {
            deleteItem(itemPopup_);
            updateWindowSize();
        }
        return 0;
    case LM_STOPDIRWATCH:
        directoryWatch_.stop();
        dirWatchActive_ = false;
        return 0;
    case LM_SHELLNOTIFY:
        updateBitBucketIcons();
        return 0;
    case LM_JUMPLISTCLOSED:
        lockHotIndex_ = false;
        if (WindowFromPoint(messagePoint()) != window_)
        {
            setHotIndex(ITEM_NONE);
        }
        lockAutoHide_ = false;
        canAutoHide_ = true;
        doAutoHide();
        return 0;
    case LM_DOAUTOHIDE:
        canAutoHide_ = GetFocus() == window_;
        doAutoHide();
        return 0;
    case WM_MOUSEMOVE:
        if (!mouseTracking_)
        {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window_;
            TrackMouseEvent(&track);
            mouseTracking_ = true;
            if (autoHide_ && autoHiden_ && autoShowMode_ == AutoShowMode::MouseHover)
            {
                delayedAutoShow();
            }
        }
        if (!autoHiden_)
        {
            mouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        }
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(window_);
        mouseDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        mouseUp(VK_LBUTTON, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_RBUTTONUP:
        mouseUp(VK_RBUTTON, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        break; // let DefWindowProc generate WM_CONTEXTMENU
    case WM_MOUSELEAVE:
        mouseTracking_ = false;
        setHotIndex(ITEM_NONE);
        if (autoShowMode_ == AutoShowMode::MouseHover || canAutoHide_)
        {
            delayedAutoHide(TIMER_AUTO_HIDE_DELAY);
        }
        return 0;
    case WM_KEYDOWN:
        if (autoHide_ && autoHiden_)
        {
            return 0;
        }
        keyDown(static_cast<UINT>(wparam));
        return 0;
    case WM_CONTEXTMENU:
    {
        if (autoHiden_)
        {
            return 0;
        }
        const WPARAM source = wparam;
        POINT client{};
        if (source == static_cast<WPARAM>(-1))
        {
            itemPopup_ = hotIndex_;
            client = isItemIndex(itemPopup_)
                     ? centerPoint(items_.at(static_cast<std::size_t>(itemPopup_)).rect)
                     : POINT{0, 0};
        }
        else
        {
            client = POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window_, &client);
            itemPopup_ = itemIndexAt(client);
        }
        setHotIndex(itemPopup_);
        const bool extended = GetKeyState(VK_SHIFT) < 0;
        RECT itemRect = isItemIndex(itemPopup_)
                        ? items_.at(static_cast<std::size_t>(itemPopup_)).rect : RECT{};
        if (!extended && isItemIndex(itemPopup_) &&
            jumplistShowMode_ != JumplistShowMode::Disabled &&
            showJumplist(window_, items_.at(static_cast<std::size_t>(itemPopup_)).pidl,
                         jumplistRecentMax_, itemRect, align_))
        {
            lockHotIndex_ = true;
            lockAutoHide_ = true;
            return 0;
        }
        showContextMenu(client, extended);
        return 0;
    }
    case WM_SIZE:
        if (created_)
        {
            updateBlur();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

std::wstring BarWindow::panelName() const
{
    std::wstring path = workDir_;
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
    {
        path.pop_back();
    }
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

void BarWindow::localize()
{
    // Menu captions are built directly from localized strings in buildMenu().
}

void BarWindow::loadSettings()
{
    IniFile ini(settingsFile_);
    workDir_ = expandPath(ini.readString(INI_SECTION_MAIN, INI_DIR_LINKS, DEF_DIR_LINKS));
    if (!workDir_.empty() && workDir_.back() != L'\\')
    {
        workDir_ += L'\\';
    }
    autoHide_ = ini.readBool(INI_SECTION_MAIN, INI_AUTOHIDE, DEF_AUTOHIDE);
    autoHideTransparency_ = ini.readBool(INI_SECTION_MAIN, INI_AUTOHIDE_TRANSPARENCY,
                                         DEF_AUTOHIDE_TRANSPARENCY);
    autoShowDelay_ = ini.readInt(INI_SECTION_MAIN, INI_AUTOSHOW_DELAY, DEF_AUTOSHOW_DELAY, 0, 60000);
    autoShowMode_ = static_cast<AutoShowMode>(ini.readInt(INI_SECTION_MAIN,
        INI_AUTOHIDE_SHOWMODE, static_cast<int>(DEF_AUTOHIDE_SHOWMODE), 0, 2));
    backgroundColor_ = static_cast<std::uint32_t>(ini.readInt(INI_SECTION_MAIN,
        INI_BKGCOLOR, static_cast<int>(DEF_BKGCOLOR)));
    corner1GapWidth_ = ini.readInt(INI_SECTION_MAIN, INI_CORNER1GAP_WIDTH, DEF_CORNERGAP_WIDTH);
    corner2GapWidth_ = ini.readInt(INI_SECTION_MAIN, INI_CORNER2GAP_WIDTH, DEF_CORNERGAP_WIDTH);
    enableAeroGlass_ = ini.readBool(INI_SECTION_MAIN, INI_ENABLE_AG, DEF_ENABLE_AG);
    glowSize_ = ini.readInt(INI_SECTION_MAIN, INI_GLOWSIZE, DEF_GLOWSIZE,
                            GLOW_SIZE_MIN, GLOW_SIZE_MAX);
    tooltipShow_ = ini.readBool(INI_SECTION_MAIN, INI_TOOLTIP_SHOW, DEF_TOOLTIP_SHOW);
    const auto hotkeyValue = static_cast<std::uint32_t>(ini.readInt(INI_SECTION_MAIN,
        INI_AUTOHIDE_HOTKEY, static_cast<int>(DEF_AUTOHIDE_HOTKEY)));
    hotkey_.modifiers = static_cast<std::uint16_t>(hotkeyValue >> 16);
    hotkey_.keyCode = static_cast<std::uint16_t>(hotkeyValue & 0xFFFF);
    iconSize_ = ini.readInt(INI_SECTION_MAIN, INI_ICON_SIZE, DEF_ICON_SIZE,
                            ICON_SIZE_MIN, ICON_SIZE_MAX);
    isLightStyle_ = ini.readBool(INI_SECTION_MAIN, INI_ISLIGHT, DEF_ISLIGHT);
    itemMargin_.cx = ini.readInt(INI_SECTION_MAIN, INI_MARGINX, DEF_MARGINX, MARGIN_MIN, MARGIN_MAX);
    itemMargin_.cy = ini.readInt(INI_SECTION_MAIN, INI_MARGINY, DEF_MARGINY, MARGIN_MIN, MARGIN_MAX);
    separatorWidth_ = ini.readInt(INI_SECTION_MAIN, INI_SEPARATOR_WIDTH, DEF_SEPARATOR_WIDTH,
                                  SEPARATOR_WIDTH_MIN, SEPARATOR_WIDTH_MAX);
    separatorStyle_ = static_cast<SeparatorStyle>(ini.readInt(INI_SECTION_MAIN,
        INI_SEPARATOR_STYLE, static_cast<int>(DEF_SEPARATOR_STYLE), 0, 1));
    itemOrder_ = static_cast<ItemOrder>(ini.readInt(INI_SECTION_MAIN,
        INI_ITEM_ORDER, static_cast<int>(DEF_ITEM_ORDER), 0, 1));
    layout_ = static_cast<PanelLayout>(ini.readInt(INI_SECTION_MAIN,
        INI_ITEMS_ALIGN, static_cast<int>(DEF_ITEMS_ALIGN), 0, 2));
    jumplistRecentMax_ = ini.readInt(INI_SECTION_MAIN, INI_JUMPLIST_RECENTMAX,
        DEF_JUMPLIST_RECENTMAX, JUMPLIST_RECENTMAX_MIN, JUMPLIST_RECENTMAX_MAX);
    jumplistShowMode_ = static_cast<JumplistShowMode>(ini.readInt(INI_SECTION_MAIN,
        INI_JUMPLIST_SHOWMODE, static_cast<int>(DEF_JUMPLIST_SHOWMODE), 0, 1));
    lockBar_ = ini.readBool(INI_SECTION_MAIN, INI_LOCK_BAR, DEF_LOCK_BAR);
    transparencyMode_ = static_cast<TransparencyMode>(ini.readInt(INI_SECTION_MAIN,
        INI_TRANSPARENCYMODE, static_cast<int>(DEF_TRANSPARENCYMODE), 0, 3));
    look_ = static_cast<Look>(ini.readInt(INI_SECTION_MAIN, INI_COLORMODE,
        static_cast<int>(DEF_COLORMODE), 0, 3));
    monitorNum_ = ini.readInt(INI_SECTION_MAIN, INI_MONITORNUM, 0, 0, 100);
    align_ = static_cast<PanelAlign>(ini.readInt(INI_SECTION_MAIN, INI_EDGE,
        static_cast<int>(DEF_EDGE), 0, 3));
    sortAlphabetically_ = ini.readBool(INI_SECTION_MAIN, INI_SORT_AB, DEF_SORT_AB);
    textColor_ = static_cast<std::uint32_t>(ini.readInt(INI_SECTION_MAIN, INI_TXTCOLOR,
        static_cast<int>(DEF_TXTCOLOR))) & 0x00FFFFFFU;
    textLayout_ = static_cast<TextLayout>(ini.readInt(INI_SECTION_MAIN, INI_TEXT_LAYOUT,
        static_cast<int>(DEF_TEXT_LAYOUT), 0, 4));
    textOffset_ = ini.readInt(INI_SECTION_MAIN, INI_TEXT_OFFSET, DEF_TEXT_OFFSET,
                              TEXT_OFFSET_MIN, TEXT_OFFSET_MAX);
    textWidth_ = ini.readInt(INI_SECTION_MAIN, INI_TEXT_WIDTH, DEF_TEXT_WIDTH,
                             TEXT_WIDTH_MIN, TEXT_WIDTH_MAX);
    useBackgroundColor_ = ini.readBool(INI_SECTION_MAIN, INI_USEBKGCOLOR, DEF_USEBKGCOLOR);
    useTextColor_ = ini.readBool(INI_SECTION_MAIN, INI_USETXTCOLOR, DEF_USETXTCOLOR);
    stayOnTop_ = ini.readBool(INI_SECTION_MAIN, INI_STAYONTOP, DEF_STAYONTOP);

    gripSize_ = GRIP_SIZE;
    hotIndex_ = ITEM_NONE;
    pressedIndex_ = ITEM_NONE;
    itemDropPosition_ = ITEM_NONE;
    itemPopup_ = ITEM_NONE;
    dragIndex_ = ITEM_NONE;
    mouseLeftDown_ = false;
    mouseDragLinkbar_ = false;
    mouseDragItem_ = false;
    draggingItem_ = false;
    globals().layout = layout_;
    globals().look = look_;
    globals().aeroGlassEnabled = enableAeroGlass_;
    dragAlign_ = align_;
    registerHotkey();
}

void BarWindow::saveSettings()
{
    IniFile ini(settingsFile_);
    ini.writeInt(INI_SECTION_MAIN, INI_MONITORNUM, monitorNum_);
    ini.writeInt(INI_SECTION_MAIN, INI_EDGE, static_cast<int>(align_));
    ini.writeBool(INI_SECTION_MAIN, INI_AUTOHIDE, autoHide_);
    ini.writeBool(INI_SECTION_MAIN, INI_AUTOHIDE_TRANSPARENCY, autoHideTransparency_);
    ini.writeInt(INI_SECTION_MAIN, INI_AUTOHIDE_SHOWMODE, static_cast<int>(autoShowMode_));
    ini.writeInt(INI_SECTION_MAIN, INI_AUTOHIDE_HOTKEY,
                 static_cast<int>((static_cast<std::uint32_t>(hotkey_.modifiers) << 16) |
                                  hotkey_.keyCode));
    ini.writeInt(INI_SECTION_MAIN, INI_ICON_SIZE, iconSize_);
    ini.writeInt(INI_SECTION_MAIN, INI_MARGINX, itemMargin_.cx);
    ini.writeInt(INI_SECTION_MAIN, INI_MARGINY, itemMargin_.cy);
    ini.writeInt(INI_SECTION_MAIN, INI_TEXT_LAYOUT, static_cast<int>(textLayout_));
    ini.writeInt(INI_SECTION_MAIN, INI_TEXT_OFFSET, textOffset_);
    ini.writeInt(INI_SECTION_MAIN, INI_TEXT_WIDTH, textWidth_);
    ini.writeInt(INI_SECTION_MAIN, INI_ITEM_ORDER, static_cast<int>(itemOrder_));
    ini.writeInt(INI_SECTION_MAIN, INI_ITEMS_ALIGN, static_cast<int>(layout_));
    ini.writeBool(INI_SECTION_MAIN, INI_LOCK_BAR, lockBar_);
    ini.writeBool(INI_SECTION_MAIN, INI_ISLIGHT, isLightStyle_);
    ini.writeBool(INI_SECTION_MAIN, INI_ENABLE_AG, enableAeroGlass_);
    ini.writeInt(INI_SECTION_MAIN, INI_AUTOSHOW_DELAY, autoShowDelay_);
    ini.writeBool(INI_SECTION_MAIN, INI_SORT_AB, sortAlphabetically_);
    ini.writeBool(INI_SECTION_MAIN, INI_USEBKGCOLOR, useBackgroundColor_);
    ini.writeHex(INI_SECTION_MAIN, INI_BKGCOLOR, backgroundColor_, 8);
    ini.writeBool(INI_SECTION_MAIN, INI_USETXTCOLOR, useTextColor_);
    ini.writeHex(INI_SECTION_MAIN, INI_TXTCOLOR, textColor_, 6);
    ini.writeInt(INI_SECTION_MAIN, INI_GLOWSIZE, glowSize_);
    ini.writeBool(INI_SECTION_MAIN, INI_STAYONTOP, stayOnTop_);
    ini.writeInt(INI_SECTION_MAIN, INI_JUMPLIST_SHOWMODE, static_cast<int>(jumplistShowMode_));
    ini.writeInt(INI_SECTION_MAIN, INI_JUMPLIST_RECENTMAX, jumplistRecentMax_);
    ini.writeInt(INI_SECTION_MAIN, INI_COLORMODE, static_cast<int>(look_));
    ini.writeInt(INI_SECTION_MAIN, INI_TRANSPARENCYMODE, static_cast<int>(transparencyMode_));
    ini.writeInt(INI_SECTION_MAIN, INI_CORNER1GAP_WIDTH, corner1GapWidth_);
    ini.writeInt(INI_SECTION_MAIN, INI_CORNER2GAP_WIDTH, corner2GapWidth_);
    ini.writeInt(INI_SECTION_MAIN, INI_SEPARATOR_WIDTH, separatorWidth_);
    ini.writeInt(INI_SECTION_MAIN, INI_SEPARATOR_STYLE, static_cast<int>(separatorStyle_));
    ini.writeBool(INI_SECTION_MAIN, INI_TOOLTIP_SHOW, tooltipShow_);
    ini.flush();
}

void BarWindow::saveLinks()
{
    items_.saveListFile(workDir_);
}

void BarWindow::loadItems()
{
    workDirPidl_ = parseWorkDir(workDir_);
    items_.loadFromListFile(workDir_, iconSize_, sortAlphabetically_);
    if (!dirWatchActive_)
    {
        directoryWatch_.start(workDir_,
            [this](WatchAction action, const std::wstring& name) { dirWatchChange(action, name); },
            [this](DWORD code, const std::wstring& message) { dirWatchError(code, message); });
        dirWatchActive_ = true;
    }
}

void BarWindow::updateBackgroundColor()
{
    LookMode mode{look_, transparencyMode_};
    systemBackgroundColor_ = taskbarColor(mode);
}

void BarWindow::updateBlur()
{
    if (os().isTen)
    {
        if (autoHide_ && autoHiden_ && autoHideTransparency_)
        {
            setWindowAccentPolicy10(window_, TransparencyMode::Disabled, 0);
        }
        else
        {
            setWindowAccentPolicy10(window_, transparencyMode_, backgroundColor_);
        }
        return;
    }
    bool enabled = !(autoHide_ && autoHiden_ && autoHideTransparency_);
    if (os().isEightAndEightPointOne && !enableAeroGlass_)
    {
        enabled = false;
    }
    ::lb::updateBlur(window_, enabled);
}

void BarWindow::createBitmaps()
{
}

void BarWindow::updateItemSizes()
{
    int textHeight = 0;
    if (textLayout_ != TextLayout::None)
    {
        HDC dc = GetDC(window_);
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HGDIOBJ previous = SelectObject(dc, font);
        RECT measure{};
        DrawTextW(dc, L"Wp", 2, &measure, DT_SINGLELINE | DT_NOCLIP | DT_CALCRECT);
        textHeight = rectHeight(measure);
        SelectObject(dc, previous);
        ReleaseDC(window_, dc);
    }
    int w = 0;
    int h = 0;
    switch (textLayout_)
    {
    case TextLayout::Left:
    case TextLayout::Right:
        w = itemMargin_.cx + iconSize_ + textOffset_ + textWidth_ + itemMargin_.cx;
        h = itemMargin_.cy + std::max(iconSize_, textHeight) + itemMargin_.cy;
        iconOffset_.x = textLayout_ == TextLayout::Right ? itemMargin_.cx
                                                         : itemMargin_.cx + textWidth_ + textOffset_;
        iconOffset_.y = (h - iconSize_) / 2;
        if (textLayout_ == TextLayout::Right)
        {
            textRect_ = makeRect(itemMargin_.cx + iconSize_ + textOffset_, (h - textHeight) / 2,
                                 textWidth_, textHeight);
        }
        else
        {
            textRect_ = makeRect(itemMargin_.cx, (h - textHeight) / 2, textWidth_, textHeight);
        }
        break;
    case TextLayout::Top:
    case TextLayout::Bottom:
        w = itemMargin_.cx + std::max(iconSize_, textWidth_) + itemMargin_.cx;
        h = itemMargin_.cy + iconSize_ + textOffset_ + textHeight + itemMargin_.cy;
        iconOffset_.x = (w - iconSize_) / 2;
        iconOffset_.y = textLayout_ == TextLayout::Bottom
                        ? itemMargin_.cy : itemMargin_.cy + textHeight + textOffset_;
        if (textLayout_ == TextLayout::Bottom)
        {
            textRect_ = makeRect(textOffset_, itemMargin_.cy + iconSize_ + textOffset_,
                                 w - 2 * textOffset_, textHeight);
        }
        else
        {
            textRect_ = makeRect(textOffset_, itemMargin_.cy, w - 2 * textOffset_, textHeight);
        }
        break;
    default:
        w = itemMargin_.cx + iconSize_ + itemMargin_.cx;
        h = itemMargin_.cy + iconSize_ + itemMargin_.cy;
        iconOffset_ = POINT{itemMargin_.cx, itemMargin_.cy};
        break;
    }
    buttonSize_ = SIZE{w, h};
    recreateButtonBitmap(w, h);
}

void BarWindow::querySizing(bool vertical, int& width, int& height)
{
    items_.sizes.button = buttonSize_;
    items_.sizes.separator = separatorWidth_;
    items_.sizes.margin = gripSize_;
    items_.updateLines(vertical, width, height);
    if (vertical)
    {
        width = buttonSize_.cx * static_cast<int>(items_.lines().size());
    }
    else
    {
        height = buttonSize_.cy * static_cast<int>(items_.lines().size());
    }
}

void BarWindow::querySized(int x, int y, int width, int height)
{
    beforeAutoHideBound_ = boundsRect(x, y, width, height);
    recreateMainBitmap(width, height);
    RECT target = (autoHide_ && autoHiden_) ? afterAutoHideBound_ : beforeAutoHideBound_;
    MoveWindow(window_, target.left, target.top, rectWidth(target), rectHeight(target), FALSE);
    updateWindow(target);
}

void BarWindow::updateWindowSize()
{
    items_.sizes.button = buttonSize_;
    items_.sizes.separator = separatorWidth_;
    items_.sizes.margin = gripSize_;
    RECT bounds{};
    GetWindowRect(window_, &bounds);
    items_.updateLines(isVertical(align_), rectWidth(bounds), rectHeight(bounds));

    int left = bounds.left;
    int top = bounds.top;
    int width = rectWidth(bounds);
    int height = rectHeight(bounds);
    if (isVertical(align_))
    {
        width = buttonSize_.cx * static_cast<int>(items_.lines().size());
        if (align_ == PanelAlign::Right)
        {
            left = bounds.left + rectWidth(bounds) - width;
        }
    }
    else
    {
        height = buttonSize_.cy * static_cast<int>(items_.lines().size());
        if (align_ == PanelAlign::Bottom)
        {
            top = bounds.top + rectHeight(bounds) - height;
        }
    }
    recreateMainBitmap(width, height);
    MoveWindow(window_, left, top, width, height, FALSE);
    updateWindow(boundsRect(left, top, width, height));
}

void BarWindow::drawBackground(DibBitmap& bitmap, const RECT& clip)
{
    DrawBackgroundParams params;
    params.bitmap = &bitmap;
    params.align = align_;
    params.clip = clip;
    params.isLight = isLightStyle_;
    params.backgroundColor = useBackgroundColor_ ? backgroundColor_ : systemBackgroundColor_;
    themeDrawBackground(params);
}

void BarWindow::drawCaption(DibBitmap& bitmap, int index, bool forDrag)
{
    if (textLayout_ == TextLayout::None)
    {
        return;
    }
    const UINT flags = TEXTALIGN[static_cast<std::size_t>(textLayout_)] |
                       DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP;
    HDC dc = bitmap.dc();
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HGDIOBJ previousFont = SelectObject(dc, font);

    if (index == ITEM_NONE)
    {
        for (const auto& item : items_)
        {
            if (item->isSeparator())
            {
                continue;
            }
            RECT rect = textRect_;
            OffsetRect(&rect, item->rect.left, item->rect.top);
            drawGlassText(dc, item->caption, rect, flags, glowSize_, textColor_);
        }
    }
    else if (isItemIndex(index))
    {
        const Item& item = items_.at(static_cast<std::size_t>(index));
        if (!item.isSeparator())
        {
            RECT rect = textRect_;
            if (pressedIndex_ == index)
            {
                OffsetRect(&rect, 1, 1);
            }
            if (forDrag)
            {
                drawGlassText(dc, item.caption, rect, flags, glowSize_, textColor_);
            }
            else
            {
                // Draw into a temporary, button-sized surface to avoid shadow bleed.
                DibBitmap temporary;
                temporary.setSize(buttonSize_.cx, buttonSize_.cy);
                drawGlassText(temporary.dc(), item.caption, rect, flags, glowSize_, textColor_);
                AlphaBlend(dc, item.rect.left, item.rect.top,
                           buttonSize_.cx, buttonSize_.cy,
                           temporary.dc(), 0, 0, buttonSize_.cx, buttonSize_.cy,
                           premultipliedBlend());
            }
        }
    }
    SelectObject(dc, previousFont);
}

void BarWindow::drawItem(DibBitmap& bitmap, int index, bool selected, bool pressed,
                         bool drawBg, bool forDrag)
{
    if (index == ITEM_NONE || !isItemIndex(index))
    {
        return;
    }
    const Item& item = items_.at(static_cast<std::size_t>(index));
    if (item.isSeparator())
    {
        return;
    }
    RECT rect = item.rect;
    if (drawBg && IsThemeActive())
    {
        drawBackground(bitmap, rect);
    }
    if (forDrag)
    {
        OffsetRect(&rect, -rect.left, -rect.top);
    }
    if (pressed)
    {
        themeDrawButton(bitmap, rect, true);
    }
    else if (selected && bitmapButton_.handle() != nullptr)
    {
        AlphaBlend(bitmap.dc(), rect.left, rect.top, rectWidth(rect), rectHeight(rect),
                   bitmapButton_.dc(), 0, 0, rectWidth(rect), rectHeight(rect),
                   premultipliedBlend());
    }
    drawCaption(bitmap, index, forDrag);
    const int delta = pressed ? 1 : 0;
    items_.draw(bitmap.dc(), item, rect.left + iconOffset_.x + delta,
                rect.top + iconOffset_.y + delta);
}

void BarWindow::drawItems(int width, int height)
{
    items_.sizes.button = buttonSize_;
    items_.sizes.separator = separatorWidth_;
    items_.sizes.margin = gripSize_;
    items_.updateLines(isVertical(align_), width, height);
    drawCaption(bitmapPanel_, ITEM_NONE);
    for (const auto& item : items_)
    {
        if (item->isSeparator() && separatorStyle_ != SeparatorStyle::Space)
        {
            themeDrawSeparator(bitmapPanel_, align_, item->rect);
        }
        else
        {
            items_.draw(bitmapPanel_.dc(), *item,
                        item->rect.left + iconOffset_.x, item->rect.top + iconOffset_.y);
        }
    }
}

void BarWindow::recreateMainBitmap(int width, int height)
{
    bitmapPanel_.setSize(width, height);
    bitmapPanel_.clear();
    drawBackground(bitmapPanel_, makeRect(0, 0, width, height));
    drawItems(width, height);
}

void BarWindow::recreateButtonBitmap(int width, int height)
{
    bitmapButton_.setSize(width, height);
    bitmapButton_.clear();
    themeDrawButton(bitmapButton_, bitmapButton_.bound(), false);
    bitmapSelected_.setSize(width, height);
    bitmapDropPosition_.setSize(width, height);
}

void BarWindow::updateWindow()
{
    RECT bounds{};
    GetWindowRect(window_, &bounds);
    updateWindow(bounds);
}

void BarWindow::updateWindow(const RECT& bounds)
{
    const int width = rectWidth(bounds);
    const int height = rectHeight(bounds);
    if (width <= 0 || height <= 0 || bitmapPanel_.handle() == nullptr)
    {
        return;
    }
    POINT destination{bounds.left, bounds.top};
    RECT current{};
    GetWindowRect(window_, &current);
    POINT* destinationPointer = EqualRect(&bounds, &current) ? nullptr : &destination;
    const SIZE size{width, height};
    POINT source{0, 0};
    switch (align_)
    {
    case PanelAlign::Left: source = POINT{bitmapPanel_.width() - width, 0}; break;
    case PanelAlign::Top: source = POINT{0, bitmapPanel_.height() - height}; break;
    default: break;
    }

    if (autoHiden_)
    {
        int gap1 = corner1GapWidth_;
        int gap2 = corner2GapWidth_;
        if (gap1 > 0 || gap2 > 0)
        {
            const int span = isVertical(align_) ? height : width;
            if (gap1 > span - gap2)
            {
                gap1 = gap2 = 0;
            }
        }
        if (autoHideTransparency_)
        {
            DibBitmap temporary;
            temporary.setSize(width, height);
            HDC dc = temporary.dc();
            if (gap1 > 0 || gap2 > 0)
            {
                RECT clip = isVertical(align_)
                            ? makeRect(0, gap1, width, height - gap1 - gap2)
                            : makeRect(gap1, 0, width - gap1 - gap2, height);
                IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
            }
            // Alpha = 1 keeps the window hittable while looking fully transparent.
            auto* pixels = static_cast<std::uint32_t*>(temporary.bits());
            const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            for (std::size_t i = 0; i < total; ++i)
            {
                pixels[i] = 0x01000000U;
            }
            if (gap1 > 0 || gap2 > 0)
            {
                const bool vertical = isVertical(align_);
                const int from = vertical ? gap1 : gap1;
                const int to = vertical ? height - gap2 : width - gap2;
                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        const bool inside = vertical ? (y >= from && y < to)
                                                     : (x >= from && x < to);
                        if (!inside)
                        {
                            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                   static_cast<std::size_t>(x)] = 0;
                        }
                    }
                }
            }
            (void)dc;
            UpdateLayeredWindow(window_, nullptr, &destination, const_cast<SIZE*>(&size),
                                dc, &source, 0, &PREMULTIPLIED_BLEND, ULW_ALPHA);
            return;
        }
        if (gap1 > 0 || gap2 > 0)
        {
            DibBitmap temporary;
            temporary.setSize(width, height);
            if (isVertical(align_))
            {
                BitBlt(temporary.dc(), 0, gap1, width, height - gap1 - gap2,
                       bitmapPanel_.dc(), source.x, gap1, SRCCOPY);
            }
            else
            {
                BitBlt(temporary.dc(), gap1, 0, width - gap1 - gap2, height,
                       bitmapPanel_.dc(), gap1, source.y, SRCCOPY);
            }
            UpdateLayeredWindow(window_, nullptr, &destination, const_cast<SIZE*>(&size),
                                temporary.dc(), &POINT_ZERO, 0, &PREMULTIPLIED_BLEND, ULW_ALPHA);
            return;
        }
        UpdateLayeredWindow(window_, nullptr, &destination, const_cast<SIZE*>(&size),
                            bitmapPanel_.dc(), &source, 0, &PREMULTIPLIED_BLEND, ULW_ALPHA);
        return;
    }
    UpdateLayeredWindow(window_, nullptr, destinationPointer, const_cast<SIZE*>(&size),
                        bitmapPanel_.dc(), &source, 0, &PREMULTIPLIED_BLEND, ULW_ALPHA);
}

bool BarWindow::isItemIndex(int index) const noexcept
{
    return index >= 0 && static_cast<std::size_t>(index) < items_.count();
}

int BarWindow::itemIndexAt(const POINT& point, int lastIndex) const
{
    if (point.x < 0 || point.y < 0 || items_.empty())
    {
        return ITEM_NONE;
    }
    if (isItemIndex(lastIndex) && pointInRect(items_.at(static_cast<std::size_t>(lastIndex)).rect, point))
    {
        return lastIndex;
    }
    for (std::size_t i = 0; i < items_.count(); ++i)
    {
        if (pointInRect(items_.at(i).rect, point))
        {
            return static_cast<int>(i);
        }
    }
    return ITEM_NONE;
}

void BarWindow::deleteItem(int index)
{
    if (!isItemIndex(index))
    {
        return;
    }
    items_.erase(static_cast<std::size_t>(index));
    if (hotIndex_ == index) hotIndex_ = ITEM_NONE;
    if (pressedIndex_ == index) pressedIndex_ = ITEM_NONE;
}

void BarWindow::checkItem(int index)
{
    if (!isItemIndex(index))
    {
        return;
    }
    const std::wstring file = items_.at(static_cast<std::size_t>(index)).fileName;
    if (GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        return;
    }
    const std::wstring message = l10n(L"Message.FileNotFound", L"File does not exists") + L"\r\n" +
        file + L"\r\n" + l10n(L"Message.DeleteShortcut", L"Delete shortcut?");
    if (MessageBoxW(window_, message.c_str(), WINDOW_CAPTION, MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
    {
        deleteItem(index);
        updateWindowSize();
    }
}

void BarWindow::executeItem(int index)
{
    if (isItemIndex(index) && !items_.at(static_cast<std::size_t>(index)).isSeparator())
    {
        openByDefaultVerb(window_, items_.at(static_cast<std::size_t>(index)).pidl);
    }
}

void BarWindow::clickItem(int x, int y)
{
    executeItem(itemIndexAt(POINT{x, y}));
}

void BarWindow::setHotIndex(int value)
{
    if (!isItemIndex(value))
    {
        value = ITEM_NONE;
    }
    if (lockHotIndex_ || value == hotIndex_)
    {
        return;
    }
    if (hotIndex_ >= 0 && isItemIndex(hotIndex_))
    {
        const RECT rect = items_.at(static_cast<std::size_t>(hotIndex_)).rect;
        BitBlt(bitmapPanel_.dc(), rect.left, rect.top, rectWidth(rect), rectHeight(rect),
               bitmapSelected_.dc(), 0, 0, SRCCOPY);
    }
    hotIndex_ = value;
    if (hotIndex_ >= 0 && isItemIndex(hotIndex_))
    {
        const RECT rect = items_.at(static_cast<std::size_t>(hotIndex_)).rect;
        BitBlt(bitmapSelected_.dc(), 0, 0, rectWidth(rect), rectHeight(rect),
               bitmapPanel_.dc(), rect.left, rect.top, SRCCOPY);
        drawItem(bitmapPanel_, hotIndex_, true, false);
    }
    updateWindow();

    if (tooltipShow_ && hotIndex_ >= 0 && isItemIndex(hotIndex_) &&
        !items_.at(static_cast<std::size_t>(hotIndex_)).isSeparator())
    {
        const RECT rect = items_.at(static_cast<std::size_t>(hotIndex_)).rect;
        const POINT center = centerPoint(rect);
        POINT point{};
        HorzAlign horizontal = HorzAlign::Left;
        VertAlign vertical = VertAlign::Bottom;
        switch (align_)
        {
        case PanelAlign::Left:
            point = POINT{rect.right + TOOLTIP_OFFSET, center.y};
            horizontal = HorzAlign::Left; vertical = VertAlign::Center; break;
        case PanelAlign::Top:
            point = POINT{center.x, rect.bottom + TOOLTIP_OFFSET};
            horizontal = HorzAlign::Center; vertical = VertAlign::Bottom; break;
        case PanelAlign::Right:
            point = POINT{rect.left - TOOLTIP_OFFSET, center.y};
            horizontal = HorzAlign::Right; vertical = VertAlign::Center; break;
        case PanelAlign::Bottom:
            point = POINT{center.x, rect.top - TOOLTIP_OFFSET};
            horizontal = HorzAlign::Center; vertical = VertAlign::Top; break;
        }
        ClientToScreen(window_, &point);
        tooltip_->activate(point, items_.at(static_cast<std::size_t>(hotIndex_)).caption,
                           horizontal, vertical);
    }
    else
    {
        tooltip_->cancel();
    }
}

void BarWindow::setPressedIndex(int value)
{
    if (pressedIndex_ == value)
    {
        return;
    }
    tooltip_->cancel();
    pressedIndex_ = value;
    if (pressedIndex_ != hotIndex_)
    {
        setHotIndex(pressedIndex_);
    }
    drawItem(bitmapPanel_, hotIndex_, true, pressedIndex_ != ITEM_NONE);
    updateWindow();
}

void BarWindow::mouseDown(int x, int y)
{
    if (autoHide_ && autoHiden_)
    {
        return;
    }
    mouseDown_ = POINT{x, y};
    mouseLeftDown_ = true;
    lockHotIndex_ = false;
    setPressedIndex(itemIndexAt(POINT{x, y}));
}

void BarWindow::mouseMove(int x, int y)
{
    static int previousX = -2147483647;
    static int previousY = -2147483647;
    if (x == previousX && y == previousY)
    {
        return;
    }
    previousX = x;
    previousY = y;
    if (autoHiden_ || isDragDrop_ || itemDropPosition_ != ITEM_NONE)
    {
        return;
    }
    if (!mouseLeftDown_)
    {
        setHotIndex(itemIndexAt(POINT{x, y}, hotIndex_));
        return;
    }
    if (mouseDragLinkbar_)
    {
        dragLinkbar(x, y);
    }
    else if (mouseDragItem_)
    {
        if (!draggingItem_)
        {
            draggingItem_ = true;
            startItemDrag();
        }
    }
    else if (!lockBar_)
    {
        if (pressedIndex_ == ITEM_NONE)
        {
            if (distance(POINT{x, y}, mouseDown_) > PANEL_DRAG_THRESHOLD)
            {
                mouseDragLinkbar_ = true;
                mouseDragItem_ = false;
                getUserWorkArea(monitorsWorkArea_);
                GetWindowRect(window_, &beforeDragBounds_);
                dragMonitorNum_ = monitorNum_;
            }
        }
        else if (isItemIndex(pressedIndex_) &&
                 !pointInRect(items_.at(static_cast<std::size_t>(pressedIndex_)).rect, POINT{x, y}))
        {
            mouseDragItem_ = true;
            mouseDragLinkbar_ = false;
        }
    }
}

void BarWindow::mouseUp(UINT button, int x, int y)
{
    if (autoHiden_)
    {
        if ((autoShowMode_ == AutoShowMode::MouseClickLeft && button == VK_LBUTTON) ||
            (autoShowMode_ == AutoShowMode::MouseClickRight && button == VK_RBUTTON))
        {
            RECT client{};
            GetClientRect(window_, &client);
            if (pointInRect(client, POINT{x, y}))
            {
                canAutoHide_ = false;
                doAutoShow();
            }
        }
        return;
    }
    canAutoHide_ = false;
    mouseUp_ = POINT{x, y};
    if (button != VK_LBUTTON)
    {
        return;
    }
    mouseLeftDown_ = false;
    if (mouseDragLinkbar_)
    {
        mouseDragLinkbar_ = false;
        monitorNum_ = dragMonitorNum_;
        setAlign(dragAlign_);
        writeProfileInt(settingsFile_, INI_MONITORNUM, monitorNum_);
        writeProfileInt(settingsFile_, INI_EDGE, static_cast<int>(align_));
    }
    else if (mouseDragItem_)
    {
        mouseDragItem_ = false;
        draggingItem_ = false;
        dragIndex_ = ITEM_NONE;
    }
    else if (pressedIndex_ != ITEM_NONE)
    {
        setPressedIndex(ITEM_NONE);
        clickItem(x, y);
    }
    setPressedIndex(ITEM_NONE);
}

void BarWindow::dragLinkbar(int x, int y)
{
    POINT point{x, y};
    ClientToScreen(window_, &point);
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    const RECT monitorRect = info.rcMonitor;
    const int localX = point.x - monitorRect.left;
    const int localY = point.y - monitorRect.top;
    const double k = static_cast<double>(rectWidth(monitorRect)) /
                     static_cast<double>(rectHeight(monitorRect));
    PanelAlign edge;
    if (localX < localY * k)
    {
        edge = localX < (rectHeight(monitorRect) - localY) * k ? PanelAlign::Left
                                                               : PanelAlign::Bottom;
    }
    else
    {
        edge = (rectWidth(monitorRect) - localX) < localY * k ? PanelAlign::Right
                                                              : PanelAlign::Top;
    }
    int monitorNumber = 0;
    std::vector<RECT> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM data) -> BOOL {
        auto* list = reinterpret_cast<std::vector<RECT>*>(data);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi); GetMonitorInfoW(m, &mi);
        list->push_back(mi.rcMonitor);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    for (std::size_t i = 0; i < monitors.size(); ++i)
    {
        if (EqualRect(&monitors[i], &monitorRect))
        {
            monitorNumber = static_cast<int>(i);
            break;
        }
    }
    if (edge == dragAlign_ && monitorNumber == dragMonitorNum_)
    {
        return;
    }
    dragAlign_ = edge;
    dragMonitorNum_ = monitorNumber;

    RECT rect;
    if (dragAlign_ == align_ && dragMonitorNum_ == monitorNum_)
    {
        rect = beforeDragBounds_;
    }
    else
    {
        rect = autoHide_ ? monitorRect : info.rcWork;
        if (isHorizontal(dragAlign_) && dragMonitorNum_ >= 0 &&
            static_cast<std::size_t>(dragMonitorNum_) < monitorsWorkArea_.size())
        {
            rect.left = monitorsWorkArea_[static_cast<std::size_t>(dragMonitorNum_)].left;
            rect.right = monitorsWorkArea_[static_cast<std::size_t>(dragMonitorNum_)].right;
        }
        if (dragMonitorNum_ == monitorNum_ && isVertical(dragAlign_))
        {
            if (align_ == PanelAlign::Top) rect.top -= rectHeight(beforeDragBounds_);
            if (align_ == PanelAlign::Bottom) rect.bottom += rectHeight(beforeDragBounds_);
        }
        int width = rectWidth(rect);
        int height = rectHeight(rect);
        querySizing(isVertical(edge), width, height);
        if (dragAlign_ == PanelAlign::Right) rect.left = rect.right - width;
        if (dragAlign_ == PanelAlign::Bottom) rect.top = rect.bottom - height;
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
    }
    querySized(rect.left, rect.top, rectWidth(rect), rectHeight(rect));
}

void BarWindow::startItemDrag()
{
    dragIndex_ = itemIndexAt(mouseDown_);
    if (!isItemIndex(dragIndex_))
    {
        return;
    }
    setHotIndex(ITEM_NONE);
    std::wstring file = window_ != nullptr ? exeFileName() : std::wstring{};
    if (!items_.at(static_cast<std::size_t>(dragIndex_)).isSeparator())
    {
        file = items_.at(static_cast<std::size_t>(dragIndex_)).fileName;
    }
    POINT offset{};
    HBITMAP image = createDragImage(offset);
    dragFile(window_, file, image, offset);
    DeleteObject(image);
    mouseUp(VK_LBUTTON, -1, -1);
}

HBITMAP BarWindow::createDragImage(POINT& offset)
{
    if (!isItemIndex(dragIndex_))
    {
        return nullptr;
    }
    const RECT rect = items_.at(static_cast<std::size_t>(dragIndex_)).rect;
    const int width = rectWidth(rect);
    const int height = rectHeight(rect);
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    DibBitmap image;
    image.setSize(width, height);
    image.clear();
    drawItem(image, dragIndex_, false, true, false, true);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    HDC screen = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP handle = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (handle != nullptr)
    {
        HDC target = CreateCompatibleDC(nullptr);
        HGDIOBJ previous = SelectObject(target, handle);
        BitBlt(target, 0, 0, width, height, image.dc(), 0, 0, SRCCOPY);
        SelectObject(target, previous);
        DeleteDC(target);
    }
    offset = POINT{mouseDown_.x - rect.left, mouseDown_.y - rect.top};
    return handle;
}

void BarWindow::keyDown(UINT key)
{
    if (items_.empty())
    {
        return;
    }
    switch (key)
    {
    case VK_SPACE:
    case VK_RETURN:
        if (isItemIndex(hotIndex_))
        {
            tooltip_->cancel();
            executeItem(hotIndex_);
        }
        return;
    case VK_ESCAPE:
        if (hotIndex_ == ITEM_NONE)
        {
            canAutoHide_ = true;
            doAutoHide();
        }
        else
        {
            setHotIndex(ITEM_NONE);
        }
        return;
    case VK_F2:
        if (isItemIndex(hotIndex_))
        {
            tooltip_->cancel();
            renameItem(hotIndex_);
        }
        return;
    case VK_DELETE:
        if (isItemIndex(hotIndex_))
        {
            tooltip_->cancel();
            shDelete(window_, items_.at(static_cast<std::size_t>(hotIndex_)).fileName,
                     GetKeyState(VK_SHIFT) >= 0);
        }
        return;
    case VK_TAB:
        setHotIndex(hotIndex_ + 1);
        return;
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    {
        if (hotIndex_ == ITEM_NONE)
        {
            setHotIndex(key == VK_LEFT || key == VK_UP
                        ? static_cast<int>(items_.count()) - 1 : 0);
            return;
        }
        const int count = static_cast<int>(items_.count());
        if (items_.lines().size() == 1)
        {
            setHotIndex(key == VK_LEFT || key == VK_UP ? std::max(hotIndex_ - 1, 0)
                                                       : std::min(hotIndex_ + 1, count - 1));
            return;
        }
        const bool vertical = isVertical(align_);
        const bool ltr = itemOrder_ == ItemOrder::LeftToRight;
        switch (key)
        {
        case VK_LEFT:
            if (ltr) setHotIndex(std::max(hotIndex_ - 1, 0));
            else if (vertical)
            {
                const int line = std::max(0, items_.getLineIndex(hotIndex_) - 1);
                setHotIndex(std::max(hotIndex_ - items_.lines()[static_cast<std::size_t>(line)], 0));
            }
            else setHotIndex(std::max(hotIndex_ - static_cast<int>(items_.lines().size()), 0));
            break;
        case VK_RIGHT:
            if (ltr) setHotIndex(std::min(hotIndex_ + 1, count - 1));
            else if (vertical)
            {
                const int line = std::min(static_cast<int>(items_.lines().size()) - 1,
                                          items_.getLineIndex(hotIndex_) + 1);
                setHotIndex(std::min(hotIndex_ + items_.lines()[static_cast<std::size_t>(line)], count - 1));
            }
            else setHotIndex(std::min(hotIndex_ + static_cast<int>(items_.lines().size()), count - 1));
            break;
        case VK_UP:
            if (!ltr) setHotIndex(std::max(hotIndex_ - 1, 0));
            else if (vertical) setHotIndex(std::max(hotIndex_ - static_cast<int>(items_.lines().size()), 0));
            else
            {
                const int line = std::max(0, items_.getLineIndex(hotIndex_) - 1);
                setHotIndex(std::max(hotIndex_ - items_.lines()[static_cast<std::size_t>(line)], 0));
            }
            break;
        case VK_DOWN:
            if (!ltr) setHotIndex(std::min(hotIndex_ + 1, count - 1));
            else if (vertical) setHotIndex(std::min(hotIndex_ + static_cast<int>(items_.lines().size()), count - 1));
            else
            {
                const int line = std::min(static_cast<int>(items_.lines().size()) - 1,
                                          items_.getLineIndex(hotIndex_) + 1);
                setHotIndex(std::min(hotIndex_ + items_.lines()[static_cast<std::size_t>(line)], count - 1));
            }
            break;
        }
        return;
    }
    default:
        return;
    }
}

HMENU BarWindow::buildMenu(bool extended)
{
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr)
    {
        return nullptr;
    }
    struct Entry
    {
        MenuCommand command;
        const wchar_t* key;
        const wchar_t* fallback;
        int tag;
        bool checked;
        bool separator;
    };
    const std::array<Entry, 13> entries{{
        {MC_NEW_SHORTCUT, L"Menu.Shortcut", L"Shortcut", 0, false, false},
        {MC_NEW_SEPARATOR, L"Menu.Separator", L"Separator", 0, false, false},
        {MC_NEW_LINKBAR, L"Menu.Linkbar", L"Linkbar", 0, false, false},
        {MenuCommand(0), L"", L"", 0, false, true},
        {MC_REMOVE_BAR, L"Menu.Delete", L"Delete the linkbar...", 0, false, false},
        {MC_OPEN_WORKDIR, L"Menu.Open", L"Open working directory", 10, false, false},
        {MenuCommand(0), L"", L"", 0, false, true},
        {MC_LOCK_BAR, L"Menu.Lock", L"Lock the linkbar", 0, lockBar_, false},
        {MC_SORT, L"Menu.Sort", L"Sort alphabetically", 0, sortAlphabetically_, false},
        {MC_PROPERTIES, L"Menu.Properties", L"&Settings", 0, false, false},
        {MenuCommand(0), L"", L"", 0, false, true},
        {MC_CLOSE, L"Menu.Close", L"Close", 20, false, false},
        {MC_CLOSE_ALL, L"Menu.CloseAll", L"Close all", 10, false, false},
    }};
    for (const Entry& entry : entries)
    {
        if (entry.tag == 20 && extended) continue;
        if (entry.tag == 10 && !extended) continue;
        if (entry.separator)
        {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING;
        if (entry.checked) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, entry.command, l10n(entry.key, entry.fallback).c_str());
    }
    return menu;
}

void BarWindow::showContextMenu(const POINT& point, bool extended)
{
    HMENU menu = buildMenu(extended);
    if (menu == nullptr)
    {
        return;
    }
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_BITMAP | MIIM_STATE;
    info.fState = MFS_DEFAULT;
    info.hbmpItem = HBMMENU_POPUP_CLOSE;
    SetMenuItemInfoW(menu, extended ? MC_CLOSE_ALL : MC_CLOSE, FALSE, &info);

    POINT screen = point;
    ClientToScreen(window_, &screen);
    lockHotIndex_ = true;
    lockAutoHide_ = true;
    tooltip_->cancel();
    if (itemPopup_ == ITEM_NONE)
    {
        const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                                              screen.x, screen.y, window_, nullptr);
        DestroyMenu(menu);
        if (command != 0)
        {
            PostMessageW(window_, LM_CM_ITEMS, 0, command);
        }
    }
    else if (isItemIndex(itemPopup_))
    {
        explorerMenuPopup(window_, items_.at(static_cast<std::size_t>(itemPopup_)).pidl,
                          screen, extended, menu);
    }
    lockHotIndex_ = false;
    if (WindowFromPoint(messagePoint()) != window_)
    {
        setHotIndex(ITEM_NONE);
    }
    lockAutoHide_ = false;
}

void BarWindow::popupMenuCommand(int command)
{
    switch (command)
    {
    case MC_NEW_SHORTCUT: newShortcut(); break;
    case MC_NEW_SEPARATOR: newSeparator(); break;
    case MC_NEW_LINKBAR: createNewBar(); break;
    case MC_REMOVE_BAR: removeBar(); break;
    case MC_OPEN_WORKDIR: openWorkDirectory(); break;
    case MC_LOCK_BAR: toggleLock(); break;
    case MC_SORT: toggleSort(); break;
    case MC_PROPERTIES: openSettings(); break;
    case MC_CLOSE: directoryWatch_.stop(); DestroyWindow(window_); break;
    case MC_CLOSE_ALL: closeAllBars(); break;
    default: break;
    }
}

void BarWindow::newShortcut()
{
    lb::newShortcut(workDir_);
}

void BarWindow::newSeparator()
{
    auto item = std::make_unique<Item>();
    item->kind = ItemKind::Separator;
    item->caption = L"|";
    item->fileName = L"|";
    if (isItemIndex(itemPopup_))
    {
        items_.insert(static_cast<std::size_t>(itemPopup_), std::move(item));
    }
    else
    {
        items_.add(std::move(item));
    }
    updateWindowSize();
}

void BarWindow::createNewBar() const
{
    std::wstring command = commandParam(CLK_NEW);
    if (!Translations::instance().locale().empty())
    {
        command = commandParam(CLK_LANG, Translations::instance().locale()) + command;
    }
    createProcess(exeFileName(), command);
}

void BarWindow::openWorkDirectory()
{
    openDirectory(workDir_);
}

void BarWindow::renameItem(int index)
{
    if (!isItemIndex(index))
    {
        return;
    }
    lockAutoHide_ = true;
    openRenameDialog(window_, items_.at(static_cast<std::size_t>(index)).pidl);
    lockAutoHide_ = false;
}

void BarWindow::toggleLock()
{
    lockBar_ = !lockBar_;
    writeProfileBool(settingsFile_, INI_LOCK_BAR, lockBar_);
}

void BarWindow::toggleSort()
{
    setSortAlphabetically(!sortAlphabetically_);
}

void BarWindow::openSettings()
{
    openSettingsDialog(this);
}

void BarWindow::closeAllBars() const
{
    closeAll();
}

void BarWindow::removeBar()
{
    const std::wstring title = format(L"You remove the linkbar \"%s\"", panelName().c_str());
    const std::wstring text = format(L"Working directory: %s", workDir_.c_str());
    const std::wstring prompt = title + L"\n" + text + L"\n\nDelete working directory";
    const std::wstring caption = L" " + std::wstring(APP_NAME_LINKBAR);
    const int result = MessageBoxW(window_, prompt.c_str(), caption.c_str(),
                                   MB_OKCANCEL | MB_ICONQUESTION);
    if (result != IDOK)
    {
        return;
    }
    removed_ = true;
    DeleteFileW(settingsFile_.c_str());
    PostQuitMessage(0);
}

void BarWindow::doAutoHide()
{
    if (!autoHide_ || lockAutoHide_)
    {
        return;
    }
    const POINT point = messagePoint();
    if (WindowFromPoint(point) == window_)
    {
        return;
    }
    if (canAutoHide_ && !autoHiden_)
    {
        autoHiden_ = true;
        setHotIndex(ITEM_NONE);
        RECT rect = beforeAutoHideBound_;
        const int size = scaleDimension(AUTOHIDE_SIZE, windowDpi(window_));
        switch (align_)
        {
        case PanelAlign::Top: rect.bottom = rect.top + size; break;
        case PanelAlign::Left: rect.right = rect.left + size; break;
        case PanelAlign::Right: rect.left = rect.right - size; break;
        case PanelAlign::Bottom: rect.top = rect.bottom - size; break;
        }
        afterAutoHideBound_ = rect;
        MoveWindow(window_, rect.left, rect.top, rectWidth(rect), rectHeight(rect), FALSE);
        updateWindow(rect);
    }
}

void BarWindow::doAutoShow()
{
    const POINT point = messagePoint();
    if (autoHide_ && autoHiden_ && (!canAutoHide_ || WindowFromPoint(point) == window_))
    {
        autoHiden_ = false;
        MoveWindow(window_, beforeAutoHideBound_.left, beforeAutoHideBound_.top,
                   rectWidth(beforeAutoHideBound_), rectHeight(beforeAutoHideBound_), FALSE);
        updateWindow(beforeAutoHideBound_);
    }
}

void BarWindow::delayedAutoHide(std::uint32_t delay)
{
    if (!autoHide_)
    {
        return;
    }
    if (delay == 0)
    {
        doAutoHide();
    }
    else
    {
        SetTimer(window_, TIMER_AUTO_HIDE, delay, nullptr);
    }
}

void BarWindow::delayedAutoShow()
{
    if (!autoHide_)
    {
        return;
    }
    if (autoShowDelay_ == 0)
    {
        doAutoShow();
    }
    else
    {
        SetTimer(window_, TIMER_AUTO_SHOW, static_cast<UINT_PTR>(autoShowDelay_), nullptr);
    }
}

void BarWindow::setAlign(PanelAlign value)
{
    align_ = value;
    dragAlign_ = value;
    if (appBar_)
    {
        appBar_->setMonitorNum(monitorNum_);
        appBar_->setAlign(value);
    }
}

void BarWindow::setStayOnTop(bool value)
{
    stayOnTop_ = value;
    if (appBar_)
    {
        appBar_->setStayOnTop(value);
    }
}

void BarWindow::setAutoHide(bool value)
{
    if (appBar_)
    {
        appBar_->setAutoHide(value);
        autoHide_ = appBar_->autoHide();
    }
    else
    {
        autoHide_ = value;
    }
    if (autoHide_)
    {
        registerHotkey();
    }
    else
    {
        unregisterHotkey();
    }
}

void BarWindow::setIconSize(int value)
{
    if (iconSize_ == value)
    {
        return;
    }
    iconSize_ = std::clamp(value, ICON_SIZE_MIN, ICON_SIZE_MAX);
    items_.setIconSize(iconSize_);
}

void BarWindow::setLook(Look value)
{
    if (!os().isTen)
    {
        return;
    }
    globals().look = value;
    bitmapButton_.clear();
    themeDrawButton(bitmapButton_, bitmapButton_.bound(), false);
    look_ = value;
    updateBackgroundColor();
    setWindowAccentPolicy10(window_, transparencyMode_, backgroundColor_);
}

void BarWindow::setTransparencyMode(TransparencyMode value)
{
    if (!os().isTen)
    {
        return;
    }
    transparencyMode_ = value;
    updateBackgroundColor();
    setWindowAccentPolicy10(window_, transparencyMode_, backgroundColor_);
}

void BarWindow::setUseBackgroundColor(bool value)
{
    if (useBackgroundColor_ == value)
    {
        return;
    }
    useBackgroundColor_ = value;
    updateBackgroundColor();
}

void BarWindow::setSortAlphabetically(bool value)
{
    if (sortAlphabetically_ == value)
    {
        return;
    }
    sortAlphabetically_ = value;
    writeProfileBool(settingsFile_, INI_SORT_AB, value);
    if (value)
    {
        items_.sort();
        recreateMainBitmap(bitmapPanel_.width(), bitmapPanel_.height());
        updateWindow();
    }
}

void BarWindow::registerHotkey()
{
    unregisterHotkey();
    if (hotkey_.keyCode == 0 || !autoHide_)
    {
        return;
    }
    RegisterHotKey(window_, LB_HOTKEY_ID,
                   MOD_NOREPEAT | hotkey_.modifiers, hotkey_.keyCode);
}

void BarWindow::unregisterHotkey()
{
    UnregisterHotKey(window_, LB_HOTKEY_ID);
}

void BarWindow::updateBitBucketIcons()
{
    items_.bitBucketUpdateIcon();
    if (autoHiden_)
    {
        recreateMainBitmap(rectWidth(beforeAutoHideBound_), rectHeight(beforeAutoHideBound_));
    }
    else
    {
        recreateMainBitmap(bitmapPanel_.width(), bitmapPanel_.height());
        updateWindow();
    }
}

RECT BarWindow::itemDropRect(int index, int part) const
{
    RECT rect = items_.at(static_cast<std::size_t>(index)).rect;
    if (part == -1)
    {
        if (isVertical(align_))
        {
            rect.top -= DROP_INDICATOR_SIZE / 2;
            rect.left += rectWidth(rect) / DROP_INDICATOR_PADDING_DIV;
            rect.right -= rectWidth(rect) / DROP_INDICATOR_PADDING_DIV;
            rect.bottom = rect.top + DROP_INDICATOR_SIZE;
        }
        else
        {
            rect.left -= DROP_INDICATOR_SIZE / 2;
            rect.top += rectHeight(rect) / DROP_INDICATOR_PADDING_DIV;
            rect.bottom -= rectHeight(rect) / DROP_INDICATOR_PADDING_DIV;
            rect.right = rect.left + DROP_INDICATOR_SIZE;
        }
    }
    else if (part == 1)
    {
        if (isVertical(align_))
        {
            rect.top = rect.bottom - DROP_INDICATOR_SIZE / 2;
            rect.left += rectWidth(rect) / DROP_INDICATOR_PADDING_DIV;
            rect.right -= rectWidth(rect) / DROP_INDICATOR_PADDING_DIV;
            rect.bottom = rect.top + DROP_INDICATOR_SIZE;
        }
        else
        {
            rect.left = rect.right - DROP_INDICATOR_SIZE / 2;
            rect.top += rectHeight(rect) / DROP_INDICATOR_PADDING_DIV;
            rect.bottom -= rectHeight(rect) / DROP_INDICATOR_PADDING_DIV;
            rect.right = rect.left + DROP_INDICATOR_SIZE;
        }
    }
    return rect;
}

void BarWindow::setDropPosition(const POINT& point)
{
    int index = itemIndexAt(point);
    int part = 0;
    if (index == ITEM_NONE)
    {
        part = -1;
    }
    else
    {
        part = items_.at(static_cast<std::size_t>(index)).getDropPart(
            point, isVertical(align_), draggingItem_);
        if (part == 1 && index != dragIndex_)
        {
            ++index;
            part = -1;
        }
        if (index > static_cast<int>(items_.count()) - 1)
        {
            index = ITEM_NONE;
        }
    }
    CoTaskMemFree(dropPidl_);
    dropPidl_ = nullptr;
    if (draggingItem_ || index == ITEM_NONE || part != 0)
    {
        dropPidl_ = workDirPidl_ != nullptr ? ILClone(workDirPidl_) : nullptr;
    }
    else if (isItemIndex(index))
    {
        dropPidl_ = ILClone(items_.at(static_cast<std::size_t>(index)).pidl);
    }
    if (index == itemDropPosition_ && part == lastDropPart_)
    {
        return;
    }
    if (itemDropPosition_ != ITEM_NONE)
    {
        BitBlt(bitmapPanel_.dc(), lastDropRect_.left, lastDropRect_.top,
               rectWidth(lastDropRect_), rectHeight(lastDropRect_),
               bitmapDropPosition_.dc(), 0, 0, SRCCOPY);
    }
    itemDropPosition_ = index;
    lastDropPart_ = part;
    if (!isItemIndex(index))
    {
        updateWindow();
        return;
    }
    const RECT rect = itemDropRect(index, part);
    lastDropRect_ = rect;
    BitBlt(bitmapDropPosition_.dc(), 0, 0, rectWidth(rect), rectHeight(rect),
           bitmapPanel_.dc(), rect.left, rect.top, SRCCOPY);
    if (part == 0)
    {
        drawBackground(bitmapPanel_, rect);
        themeDrawHover(bitmapPanel_, align_, rect);
        drawItem(bitmapPanel_, index, false, false, false);
    }
    else
    {
        HDC dc = bitmapPanel_.dc();
        RECT outer = rect;
        RECT inner = rect;
        InflateRect(&inner, -1, -1);
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &outer, black);
        FillRect(dc, &inner, white);
        DeleteObject(black);
        DeleteObject(white);
    }
    updateWindow();
}

void BarWindow::dirWatchChange(WatchAction action, const std::wstring& name)
{
    const std::wstring extension = PathFindExtensionW(name.c_str());
    bool supported = false;
    for (const auto allowed : ES_ARRAY)
    {
        if (iequals(extension, allowed))
        {
            supported = true;
            break;
        }
    }
    if (!supported)
    {
        return;
    }
    if (updateTimerEnabled_)
    {
        KillTimer(window_, TIMER_UPDATE);
    }
    const std::uint32_t hash = strHash(name);
    switch (action)
    {
    case WatchAction::Added:
    {
        auto item = std::make_unique<Item>();
        item->hash = hash;
        item->fileName = workDir_ + name;
        item->needLoad = true;
        if (itemDropPosition_ == ITEM_NONE) items_.add(std::move(item));
        else items_.insert(static_cast<std::size_t>(itemDropPosition_), std::move(item));
        break;
    }
    case WatchAction::Removed:
        if (Item* item = items_.findByHash(hash))
        {
            for (std::size_t i = 0; i < items_.count(); ++i)
            {
                if (&items_.at(i) == item)
                {
                    deleteItem(static_cast<int>(i));
                    break;
                }
            }
        }
        break;
    case WatchAction::Modified:
        if (lastModifiedHash_ != hash)
        {
            lastModifiedHash_ = hash;
            if (Item* item = items_.findByHash(hash)) item->needLoad = true;
        }
        break;
    case WatchAction::RenamedOld:
        renamedOldHash_ = hash;
        break;
    case WatchAction::RenamedNew:
        if (Item* item = items_.findByHash(renamedOldHash_))
        {
            item->fileName = workDir_ + name;
            item->needLoad = true;
            renamedOldHash_ = 0;
        }
        break;
    }
    updateTimerEnabled_ = true;
    SetTimer(window_, TIMER_UPDATE, 100, nullptr);
}

void BarWindow::dirWatchError(DWORD code, const std::wstring&)
{
    if (static_cast<int>(code) <= 0)
    {
        return;
    }
    directoryWatch_.stop();
    dirWatchActive_ = false;
    Sleep(100);
    if (GetFileAttributesW(workDir_.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        removed_ = true;
        DeleteFileW(settingsFile_.c_str());
        PostQuitMessage(0);
    }
}

STDMETHODIMP BarWindow::DragEnter(IDataObject* dataObject, DWORD keyState,
                                  POINTL point, DWORD* effect)
{
    isDragDrop_ = true;
    dragObject_ = dataObject;
    if (dragObject_ != nullptr) dragObject_->AddRef();
    dragKeyState_ = keyState;
    POINT client{static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
    ScreenToClient(window_, &client);
    if (autoHide_ && autoHiden_)
    {
        canAutoHide_ = false;
        doAutoShow();
    }
    setDropPosition(client);
    if (dropHelper_ != nullptr)
    {
        POINT helperPoint{static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
        dropHelper_->DragEnter(window_, dataObject, &helperPoint, *effect);
    }
    return dropPidl_ != nullptr ? S_OK : S_FALSE;
}

STDMETHODIMP BarWindow::DragOver(DWORD keyState, POINTL point, DWORD* effect)
{
    dragKeyState_ = keyState;
    POINT client{static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
    ScreenToClient(window_, &client);
    if (autoHide_ && autoHiden_)
    {
        return S_OK;
    }
    setDropPosition(client);
    if (dropHelper_ != nullptr)
    {
        POINT helperPoint{static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
        dropHelper_->DragOver(&helperPoint, *effect);
    }
    return dropPidl_ != nullptr ? S_OK : S_FALSE;
}

STDMETHODIMP BarWindow::DragLeave()
{
    if (dropHelper_ != nullptr)
    {
        dropHelper_->DragLeave();
    }
    if (shellDropTarget_ != nullptr)
    {
        shellDropTarget_->DragLeave();
        shellDropTarget_->Release();
        shellDropTarget_ = nullptr;
    }
    CoTaskMemFree(shellTargetPidl_);
    shellTargetPidl_ = nullptr;
    if (dragObject_ != nullptr)
    {
        dragObject_->Release();
        dragObject_ = nullptr;
    }
    setDropPosition(POINT{-1, -1});
    isDragDrop_ = false;
    if (autoHide_ && !IsWindowVisible(window_))
    {
        canAutoHide_ = true;
        SetTimer(window_, TIMER_AUTO_HIDE, TIMER_AUTO_HIDE_DELAY, nullptr);
    }
    return S_OK;
}

STDMETHODIMP BarWindow::Drop(IDataObject* dataObject, DWORD keyState,
                             POINTL point, DWORD* effect)
{
    if (dropHelper_ != nullptr)
    {
        POINT helperPoint{static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
        dropHelper_->Drop(dataObject, &helperPoint, *effect);
    }
    POINT client{static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
    ScreenToClient(window_, &client);
    if (draggingItem_)
    {
        setSortAlphabetically(false);
        if (itemDropPosition_ == ITEM_NONE)
        {
            if (items_.count() > 0 &&
                (items_.at(0).rect.left < client.x || items_.at(0).rect.top < client.y))
            {
                items_.move(static_cast<std::size_t>(dragIndex_), 0);
            }
            else if (!items_.empty())
            {
                items_.move(static_cast<std::size_t>(dragIndex_), items_.count() - 1);
            }
        }
        else if (isItemIndex(itemDropPosition_))
        {
            const int target = itemDropPosition_ <= dragIndex_
                               ? itemDropPosition_ : itemDropPosition_ - 1;
            items_.move(static_cast<std::size_t>(dragIndex_), static_cast<std::size_t>(target));
        }
        setDropPosition(POINT{-1, -1});
        updateWindowSize();
    }
    else
    {
        updateTimerEnabled_ = true;
        SetTimer(window_, TIMER_UPDATE, 100, nullptr);
    }
    if (dragObject_ != nullptr)
    {
        dragObject_->Release();
        dragObject_ = nullptr;
    }
    isDragDrop_ = false;
    (void)keyState;
    return S_OK;
}

} // namespace lb
