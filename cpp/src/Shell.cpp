#include "Shell.h"
#include "Common.h"
#include "Localization.h"
#include "Os.h"
#include "StringUtil.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shobjidl_core.h>

namespace lb {
namespace {

constexpr UINT SHCNE_UPDATEIMAGE_VALUE = 0x00008000;
constexpr DWORD SHCNRF_INTERRUPT_LEVEL = 0x0001;
constexpr DWORD SHCNRF_SHELL_LEVEL = 0x0002;
constexpr UINT GIL_CHECKSHIELD_VALUE = 0x0200;
constexpr UINT GIL_SHIELD_VALUE = 0x0200;
constexpr UINT GIL_FORCENOSHIELD_VALUE = 0x0400;
constexpr UINT SIIGBF_ICONONLY_VALUE = 0x4;
constexpr UINT SIIGBF_BIGGERSIZEOK_VALUE = 0x1;
constexpr int SCRATCH_QCM_FIRST = 20;
constexpr int SCRATCH_QCM_LAST = 0x7B00;
constexpr UINT CMF_NORMAL_VALUE = 0x0;
constexpr UINT CMF_CANRENAME_VALUE = 0x10;
constexpr UINT CMF_EXTENDEDVERBS_VALUE = 0x100;
constexpr UINT CMF_DEFAULTONLY_VALUE = 0x1;

std::vector<RECT>* g_taskbarRects = nullptr;
IContextMenu2* g_contextMenu2 = nullptr;
IContextMenu3* g_contextMenu3 = nullptr;

using SHExtractIconsWFn = UINT(WINAPI*)(LPCWSTR, int, int, int, HICON*, UINT*, UINT, UINT);
using SHDoDragDropFn = HRESULT(WINAPI*)(HWND, IDataObject*, IDropSource*, DWORD, DWORD*);

SHExtractIconsWFn shExtractIconsW = nullptr;
SHDoDragDropFn shDoDragDrop = nullptr;

template<typename T>
class ComPtr final
{
public:
    ComPtr() noexcept = default;
    explicit ComPtr(T* value) noexcept : value_(value) {}
    ~ComPtr() { if (value_) value_->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept
    {
        if (this != &other)
        {
            if (value_) value_->Release();
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    [[nodiscard]] T* get() const noexcept { return value_; }
    [[nodiscard]] T** address() noexcept { return &value_; }
    [[nodiscard]] void** voidAddress() noexcept { return reinterpret_cast<void**>(&value_); }
    [[nodiscard]] T* operator->() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }
    void reset() noexcept { if (value_) { value_->Release(); value_ = nullptr; } }
private:
    T* value_ = nullptr;
};

template<typename T>
[[nodiscard]] ComPtr<T> uiObjectFromPidl(HWND window, PCIDLIST_ABSOLUTE pidl, REFIID iid)
{
    ComPtr<IShellFolder> folder;
    PCITEMID_CHILD child = nullptr;
    if (FAILED(SHBindToParent(pidl, IID_PPV_ARGS(folder.address()), &child)))
    {
        return {};
    }
    T* object = nullptr;
    if (FAILED(folder->GetUIObjectOf(window, 1, &child, iid, nullptr,
                                     reinterpret_cast<void**>(&object))))
    {
        return {};
    }
    return ComPtr<T>(object);
}

template<typename T>
[[nodiscard]] ComPtr<T> uiObjectFromFile(HWND window, const std::wstring& path, REFIID iid)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (!parseDisplayName(path, &pidl))
    {
        return {};
    }
    auto result = uiObjectFromPidl<T>(window, pidl, iid);
    CoTaskMemFree(pidl);
    return result;
}

bool matchTaskbarClass(HWND window) noexcept
{
    wchar_t className[256]{};
    if (GetClassNameW(window, className, static_cast<int>(std::size(className))) == 0)
    {
        return false;
    }
    return iequals(className, L"Shell_TrayWnd") ||
           iequals(className, L"Shell_SecondaryTrayWnd");
}

BOOL CALLBACK enumTaskbarProc(HWND window, LPARAM lparam) noexcept
{
    auto* remaining = reinterpret_cast<int*>(lparam);
    // Stored in the global below because the callback signature has only one payload.
    if (matchTaskbarClass(window) && g_taskbarRects != nullptr)
    {
        RECT rect{};
        GetWindowRect(window, &rect);
        g_taskbarRects->push_back(rect);
        --*remaining;
    }
    return *remaining > 0 ? TRUE : FALSE;
}

} // namespace



ComGuard::ComGuard(bool apartmentThreaded) noexcept
{
    initialized_ = SUCCEEDED(CoInitializeEx(nullptr,
        apartmentThreaded ? (COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)
                          : COINIT_MULTITHREADED));
}

ComGuard::~ComGuard()
{
    if (initialized_)
    {
        CoUninitialize();
    }
}

bool parseDisplayName(const std::wstring& path, PIDLIST_ABSOLUTE* pidl) noexcept
{
    return SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, pidl, 0, nullptr));
}

std::wstring displayName(PCIDLIST_ABSOLUTE pidl) noexcept
{
    PWSTR name = nullptr;
    if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &name)))
    {
        std::wstring result(name);
        CoTaskMemFree(name);
        return result;
    }
    return {};
}

PIDLIST_ABSOLUTE parseWorkDir(const std::wstring& path) noexcept
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    return parseDisplayName(path, &pidl) ? pidl : nullptr;
}

HICON shExtractIcon(const std::wstring& path, int index, int size)
{
    if (shExtractIconsW == nullptr)
    {
        if (HMODULE shell32 = GetModuleHandleW(L"shell32.dll"))
        {
            shExtractIconsW = reinterpret_cast<SHExtractIconsWFn>(
                GetProcAddress(shell32, "SHExtractIconsW"));
        }
    }
    if (shExtractIconsW != nullptr)
    {
        HICON icon = nullptr;
        UINT id = 0;
        const UINT result = shExtractIconsW(path.c_str(), index, size, size, &icon, &id, 1,
                                            LR_DEFAULTCOLOR);
        if (result == 0)
        {
            DestroyIcon(icon);
        }
        return icon;
    }
    HICON largeIcon = nullptr;
    HICON smallIcon = nullptr;
    const UINT count = ExtractIconExW(path.c_str(), index, &largeIcon, &smallIcon, 1);
    if (count == 0)
    {
        return nullptr;
    }
    DestroyIcon(smallIcon);
    return largeIcon;
}

void preMultiply(HBITMAP bitmap)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    if (GetDIBits(dc, bitmap, 0, 0, nullptr, &info, DIB_RGB_COLORS) == 0)
    {
        SelectObject(dc, previous);
        DeleteDC(dc);
        return;
    }
    auto* pixels = static_cast<std::uint8_t*>(
        std::malloc(static_cast<std::size_t>(info.bmiHeader.biSizeImage)));
    if (pixels != nullptr)
    {
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        info.bmiHeader.biHeight = std::abs(info.bmiHeader.biHeight);
        if (GetDIBits(dc, bitmap, 0, static_cast<UINT>(info.bmiHeader.biHeight), pixels,
                      &info, DIB_RGB_COLORS) != 0)
        {
            const std::size_t count = static_cast<std::size_t>(info.bmiHeader.biWidth) *
                                      static_cast<std::size_t>(info.bmiHeader.biHeight);
            for (std::size_t i = 0; i < count; ++i)
            {
                auto* pixel = pixels + i * 4;
                const int alpha = pixel[3];
                pixel[0] = static_cast<std::uint8_t>(pixel[0] * alpha / 255);
                pixel[1] = static_cast<std::uint8_t>(pixel[1] * alpha / 255);
                pixel[2] = static_cast<std::uint8_t>(pixel[2] * alpha / 255);
            }
            SetDIBits(dc, bitmap, 0, static_cast<UINT>(info.bmiHeader.biHeight), pixels,
                      &info, DIB_RGB_COLORS);
        }
        std::free(pixels);
    }
    SelectObject(dc, previous);
    DeleteDC(dc);
}

HBITMAP loadIconFromPidl(PCIDLIST_ABSOLUTE pidl, int size)
{
    ComGuard com;
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(factory.address()))))
    {
        return nullptr;
    }
    HBITMAP bitmap = nullptr;
    const SIZE requested{size, size};
    if (SUCCEEDED(factory->GetImage(requested,
                                    SIIGBF_ICONONLY_VALUE | SIIGBF_BIGGERSIZEOK_VALUE,
                                    &bitmap)) && os().eightPointOneOrAbove)
    {
        preMultiply(bitmap);
    }
    return bitmap;
}

bool checkShield(PCIDLIST_ABSOLUTE pidl)
{
    ComPtr<IExtractIcon> extract = uiObjectFromPidl<IExtractIcon>(nullptr, pidl, IID_IExtractIcon);
    if (!extract)
    {
        return false;
    }
    wchar_t location[MAX_PATH]{};
    int index = 0;
    UINT flags = 0;
    if (extract->GetIconLocation(GIL_CHECKSHIELD_VALUE, location, MAX_PATH, &index, &flags) != S_OK)
    {
        return false;
    }
    return (flags & GIL_SHIELD_VALUE) != 0 && (flags & GIL_FORCENOSHIELD_VALUE) == 0;
}

bool checkBitBucket(PCIDLIST_ABSOLUTE pidl)
{
    ComPtr<IKnownFolderManager> manager;
    if (FAILED(CoCreateInstance(CLSID_KnownFolderManager, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(manager.address()))))
    {
        return false;
    }
    PIDLIST_ABSOLUTE resolved = nullptr;
    ComPtr<IShellLinkW> link = uiObjectFromPidl<IShellLinkW>(nullptr, pidl, IID_IShellLinkW);
    if (link)
    {
        link->GetIDList(&resolved);
    }
    ComPtr<IKnownFolder> folder;
    const HRESULT result = manager->FindFolderFromIDList(
        resolved != nullptr ? resolved : pidl, folder.address());
    if (resolved != nullptr)
    {
        CoTaskMemFree(resolved);
    }
    if (FAILED(result) || !folder)
    {
        return false;
    }
    KNOWNFOLDERID id{};
    return SUCCEEDED(folder->GetId(&id)) && id == FOLDERID_RecycleBinFolder;
}

HBITMAP shieldOverlay(HDC dc, const std::wstring& path, int index, int size)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr)
    {
        return nullptr;
    }
    HGDIOBJ previous = SelectObject(dc, bitmap);
    RECT rect{0, 0, size, size};
    FillRect(dc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    HICON icon = shExtractIcon(path, index, size / 2);
    if (icon != nullptr)
    {
        DrawIconEx(dc, rectWidth(rect) / 2, rectHeight(rect) / 2, icon, size / 2, size / 2,
                   0, nullptr, DI_NORMAL);
        DestroyIcon(icon);
    }
    SelectObject(dc, previous);
    return bitmap;
}

namespace {

LRESULT CALLBACK menuCallbackProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    if (g_contextMenu3 != nullptr)
    {
        LRESULT result = 0;
        if (SUCCEEDED(g_contextMenu3->HandleMenuMsg2(message, wparam, lparam, &result)))
        {
            return result;
        }
    }
    else if (g_contextMenu2 != nullptr)
    {
        if (SUCCEEDED(g_contextMenu2->HandleMenuMsg(message, wparam, lparam)))
        {
            return 0;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

HWND createMenuCallbackWindow()
{
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &menuCallbackProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"LINKBAR_CM_CALLBACK_WND";
    RegisterClassW(&windowClass);
    return CreateWindowW(windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                         HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
}

void removeMultipleSeparators(HMENU menu)
{
    bool separator = true;
    int count = GetMenuItemCount(menu);
    int index = 0;
    while (index < count)
    {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE;
        UINT type = 0;
        if (GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &info))
        {
            type = info.fType;
        }
        if (type == MFT_SEPARATOR && separator)
        {
            DeleteMenu(menu, static_cast<UINT>(index), MF_BYPOSITION);
            count = GetMenuItemCount(menu);
        }
        else
        {
            ++index;
        }
        separator = (type == MFT_SEPARATOR);
    }
    if (count > 0 && separator)
    {
        DeleteMenu(menu, static_cast<UINT>(count - 1), MF_BYPOSITION);
    }
}

} // namespace

void explorerMenuPopup(HWND window, PCIDLIST_ABSOLUTE pidl, POINT point,
                       bool extended, HMENU submenu)
{
    ComGuard com(false);
    ComPtr<IContextMenu> menu = uiObjectFromPidl<IContextMenu>(window, pidl, IID_IContextMenu);
    if (!menu)
    {
        return;
    }
    HMENU popup = CreatePopupMenu();
    HWND callbackWindow = nullptr;
    HBITMAP iconBitmap = nullptr;
    if (popup != nullptr)
    {
        UINT flags = CMF_NORMAL_VALUE | CMF_CANRENAME_VALUE;
        if (extended)
        {
            flags |= CMF_EXTENDEDVERBS_VALUE;
        }
        if (SUCCEEDED(menu->QueryContextMenu(popup, 0, SCRATCH_QCM_FIRST, SCRATCH_QCM_LAST, flags)))
        {
            if (submenu != nullptr)
            {
                int insertBefore = -1;
                const int count = GetMenuItemCount(popup);
                for (int i = 0; i < count; ++i)
                {
                    const UINT id = GetMenuItemID(popup, i);
                    if (id < static_cast<UINT>(SCRATCH_QCM_FIRST) || id > static_cast<UINT>(SCRATCH_QCM_LAST))
                    {
                        continue;
                    }
                    char verb[256]{};
                    if (SUCCEEDED(menu->GetCommandString(id - SCRATCH_QCM_FIRST, GCS_VERBA,
                                                         nullptr, verb, sizeof(verb))) &&
                        iequals(verb, "properties"))
                    {
                        insertBefore = i;
                        break;
                    }
                }
                MENUITEMINFOW info{};
                info.cbSize = sizeof(info);
                info.fMask = MIIM_FTYPE;
                info.fType = MFT_SEPARATOR;
                InsertMenuItemW(popup, static_cast<UINT>(insertBefore), TRUE, &info);
                info = {};
                info.cbSize = sizeof(info);
                info.fMask = MIIM_SUBMENU | MIIM_STRING;
                info.hSubMenu = submenu;
                info.dwTypeData = const_cast<wchar_t*>(APP_NAME_LINKBAR.data());
                InsertMenuItemW(popup, static_cast<UINT>(insertBefore), TRUE, &info);
                const int iconSize = GetSystemMetrics(SM_CXSMICON);
                HICON icon = reinterpret_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                        MAKEINTRESOURCEW(1), IMAGE_ICON, iconSize, iconSize,
                                        LR_DEFAULTCOLOR));
                if (icon != nullptr)
                {
                    iconBitmap = bitmapFromIcon(icon, iconSize);
                    DestroyIcon(icon);
                    info = {};
                    info.cbSize = sizeof(info);
                    info.fMask = MIIM_BITMAP;
                    info.hbmpItem = iconBitmap;
                    SetMenuItemInfoW(popup, static_cast<UINT>(insertBefore), TRUE, &info);
                }
                removeMultipleSeparators(popup);
            }

            menu->QueryInterface(IID_PPV_ARGS(&g_contextMenu3));
            if (g_contextMenu3 == nullptr)
            {
                menu->QueryInterface(IID_PPV_ARGS(&g_contextMenu2));
            }
            callbackWindow = createMenuCallbackWindow();
            const UINT command = TrackPopupMenuEx(popup, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                                  point.x, point.y, callbackWindow, nullptr);
            g_contextMenu2 = nullptr;
            g_contextMenu3 = nullptr;
            if (iconBitmap != nullptr)
            {
                DeleteObject(iconBitmap);
            }

            if (command != 0)
            {
                if (command < static_cast<UINT>(SCRATCH_QCM_FIRST))
                {
                    PostMessageW(window, LM_CM_ITEMS, 0, command);
                }
                else
                {
                    const UINT verbId = command - SCRATCH_QCM_FIRST;
                    char verb[256]{};
                    if (SUCCEEDED(menu->GetCommandString(verbId, GCS_VERBA, nullptr,
                                                         verb, sizeof(verb))) &&
                        iequals(verb, "rename"))
                    {
                        PostMessageW(window, LM_CM_RENAME, 0, 0);
                    }
                    else
                    {
                        CMINVOKECOMMANDINFOEX info{};
                        info.cbSize = sizeof(info);
                        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE |
                                     CMIC_MASK_FLAG_LOG_USAGE | CMIC_MASK_NOASYNC;
                        if (GetKeyState(VK_CONTROL) < 0) info.fMask |= CMIC_MASK_CONTROL_DOWN;
                        if (GetKeyState(VK_SHIFT) < 0) info.fMask |= CMIC_MASK_SHIFT_DOWN;
                        info.hwnd = window;
                        info.nShow = SW_SHOWNORMAL;
                        info.lpVerb = MAKEINTRESOURCEA(verbId);
                        info.lpVerbW = MAKEINTRESOURCEW(verbId);
                        info.ptInvoke = point;
                        PostMessageW(window, LM_CM_INVOKE, 0, verbId);
                        EnableWindow(window, FALSE);
                        menu->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
                        EnableWindow(window, TRUE);
                    }
                }
            }
        }
        DestroyMenu(popup);
    }
    if (callbackWindow != nullptr)
    {
        DestroyWindow(callbackWindow);
    }
}

void openByDefaultVerb(HWND window, PCIDLIST_ABSOLUTE pidl)
{
    ComPtr<IContextMenu> menu = uiObjectFromPidl<IContextMenu>(window, pidl, IID_IContextMenu);
    if (!menu)
    {
        return;
    }
    HMENU popup = CreatePopupMenu();
    if (popup == nullptr)
    {
        return;
    }
    if (SUCCEEDED(menu->QueryContextMenu(popup, 0, 0, SCRATCH_QCM_LAST, CMF_DEFAULTONLY_VALUE)))
    {
        const UINT id = static_cast<UINT>(GetMenuDefaultItem(popup, 0, 0));
        if (id != static_cast<UINT>(-1))
        {
            CMINVOKECOMMANDINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = CMIC_MASK_FLAG_LOG_USAGE;
            info.hwnd = GetDesktopWindow();
            info.nShow = SW_NORMAL;
            info.lpVerb = MAKEINTRESOURCEA(id);
            menu->InvokeCommand(&info);
        }
    }
    DestroyMenu(popup);
}

HRESULT shRename(HWND window, PCIDLIST_ABSOLUTE pidl, const std::wstring& newName)
{
    ComPtr<IShellFolder> folder;
    PCITEMID_CHILD child = nullptr;
    HRESULT result = SHBindToParent(pidl, IID_PPV_ARGS(folder.address()), &child);
    if (SUCCEEDED(result))
    {
        PIDLIST_ABSOLUTE newPidl = nullptr;
        result = folder->SetNameOf(window, child, newName.c_str(),
                                   SHGDN_INFOLDER | SHGDN_FOREDITING, &newPidl);
        if (SUCCEEDED(result) && newPidl != nullptr)
        {
            CoTaskMemFree(newPidl);
        }
    }
    return result;
}

void shDelete(HWND window, const std::wstring& path, bool allowUndo)
{
    SHFILEOPSTRUCTW operation{};
    operation.hwnd = window;
    operation.wFunc = FO_DELETE;
    std::wstring from = path;
    from += L'\0';
    operation.pFrom = from.c_str();
    operation.fFlags = allowUndo ? FOF_ALLOWUNDO : 0;
    SHFileOperationW(&operation);
}

void newShortcut(const std::wstring& directory)
{
    const std::wstring localized = loadStringResource(LB_FN_NEWSHORTCUT, LB_RS_NSC_FILENAME);
    std::wstring templateName = localized + L".lnk";
    wchar_t fileName[MAX_PATH]{};
    if (!PathMakeUniqueName(fileName, MAX_PATH, L"scut.lnk", templateName.c_str(),
                             directory.c_str()))
    {
        return;
    }
    HANDLE file = CreateFileW(fileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }
    CloseHandle(file);

    wchar_t application[MAX_PATH]{};
    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\rundll32.exe", application, MAX_PATH);
    const std::wstring command = format(L"rundll32.exe appwiz.cpl,NewLinkHere %s", fileName);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring mutableCommand = command;
    if (CreateProcessW(application, mutableCommand.data(), nullptr, nullptr, FALSE,
                       NORMAL_PRIORITY_CLASS, nullptr, directory.c_str(), &startup, &process))
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    else
    {
        DeleteFileW(fileName);
    }
}

void openDirectory(const std::wstring& directory)
{
    ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void sendShellEmail(HWND, const std::wstring& recipient,
                    const std::wstring& subject, const std::wstring& body)
{
    std::wstring address = recipient.empty() ? L"mail" : recipient;
    std::wstring url = L"mailto:" + address + L"?subject=" + subject;
    if (!body.empty())
    {
        url += L"&body=" + body;
    }
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

ULONG registerBitBucketNotify(HWND window, UINT message)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetSpecialFolderLocation(nullptr, CSIDL_BITBUCKET, &pidl)))
    {
        return 0;
    }
    SHChangeNotifyEntry entry{};
    entry.pidl = pidl;
    entry.fRecursive = TRUE;
    const ULONG cookie = SHChangeNotifyRegister(window,
        SHCNRF_INTERRUPT_LEVEL | SHCNRF_SHELL_LEVEL, SHCNE_UPDATEIMAGE_VALUE,
        message, 1, &entry);
    CoTaskMemFree(pidl);
    return cookie;
}

void unregisterBitBucketNotify(ULONG cookie) noexcept
{
    if (cookie != 0)
    {
        SHChangeNotifyDeregister(cookie);
    }
}

void getUserWorkArea(std::vector<RECT>& rects)
{
    rects.clear();
    std::vector<RECT> monitors;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto* list = reinterpret_cast<std::vector<RECT>*>(data);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);
        list->push_back(info.rcMonitor);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&monitors));
    rects = monitors;
    if (rects.empty())
    {
        return;
    }

    APPBARDATA bar{};
    bar.cbSize = sizeof(bar);
    if ((SHAppBarMessage(ABM_GETSTATE, &bar) & ABS_AUTOHIDE) != 0)
    {
        return;
    }

    if (os().isSeven)
    {
        bar.hWnd = nullptr;
        if (SHAppBarMessage(ABM_GETTASKBARPOS, &bar) != 0)
        {
            const POINT center = centerPoint(bar.rc);
            for (RECT& rect : rects)
            {
                if (!pointInRect(rect, center))
                {
                    continue;
                }
                switch (bar.uEdge)
                {
                case ABE_LEFT: rect.left = bar.rc.right; break;
                case ABE_TOP: rect.top = bar.rc.bottom; break;
                case ABE_RIGHT: rect.right = bar.rc.left; break;
                case ABE_BOTTOM: rect.bottom = bar.rc.top; break;
                }
                break;
            }
        }
        return;
    }

    std::vector<RECT> taskbars;
    int remaining = static_cast<int>(rects.size());
    g_taskbarRects = &taskbars;
    EnumWindows(&enumTaskbarProc, reinterpret_cast<LPARAM>(&remaining));
    g_taskbarRects = nullptr;
    for (const RECT& taskbar : taskbars)
    {
        const POINT center = centerPoint(taskbar);
        for (RECT& rect : rects)
        {
            if (!pointInRect(rect, center))
            {
                continue;
            }
            if (taskbar.left != rect.left) rect.right = taskbar.left;
            else if (taskbar.right != rect.right) rect.left = taskbar.right;
            else if (taskbar.top != rect.top) rect.bottom = taskbar.top;
            else rect.top = taskbar.bottom;
            break;
        }
    }
}

void dragFile(HWND window, const std::wstring& fileName, HBITMAP dragImage, POINT offset)
{
    ComGuard com;
    auto data = uiObjectFromFile<IDataObject>(window, fileName, IID_IDataObject);
    if (!data)
    {
        return;
    }
    if (dragImage != nullptr)
    {
        SHDRAGIMAGE image{};
        BITMAP bitmap{};
        GetObjectW(dragImage, sizeof(bitmap), &bitmap);
        image.sizeDragImage.cx = bitmap.bmWidth;
        image.sizeDragImage.cy = bitmap.bmHeight;
        image.ptOffset = offset;
        image.crColorKey = CLR_NONE;
        image.hbmpDragImage = dragImage;
        ComPtr<IDragSourceHelper2> helper;
        if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_ALL,
                                       IID_PPV_ARGS(helper.address()))))
        {
            helper->SetFlags(DSH_ALLOWDROPDESCRIPTIONTEXT);
            helper->InitializeFromBitmap(&image, data.get());
        }
    }
    if (shDoDragDrop == nullptr)
    {
        if (HMODULE shell32 = GetModuleHandleW(L"shell32.dll"))
        {
            shDoDragDrop = reinterpret_cast<SHDoDragDropFn>(
                GetProcAddress(shell32, "SHDoDragDrop"));
        }
    }
    DWORD effect = DROPEFFECT_NONE;
    if (shDoDragDrop != nullptr)
    {
        shDoDragDrop(nullptr, data.get(), nullptr, DROPEFFECT_LINK | DROPEFFECT_COPY | DROPEFFECT_MOVE,
                     &effect);
    }
    if ((effect & DROPEFFECT_MOVE) != 0)
    {
        DeleteFileW(fileName.c_str());
    }
}

std::wstring uniquePath(const std::wstring& directory, const std::wstring& templateName)
{
    wchar_t result[MAX_PATH]{};
    if (PathMakeUniqueName(result, MAX_PATH, L"Linkbar.lbr", templateName.c_str(),
                            directory.c_str()))
    {
        return std::wstring(result);
    }
    return {};
}

} // namespace lb
