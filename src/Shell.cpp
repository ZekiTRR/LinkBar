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
    if (apartmentThreaded)
    {
        // OLE drag&drop (RegisterDragDrop / DoDragDrop) does not work with a
        // plain CoInitializeEx: the thread has to be initialized with OLE.
        ole_ = true;
        initialized_ = SUCCEEDED(OleInitialize(nullptr));
    }
    else
    {
        initialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    }
}

ComGuard::~ComGuard()
{
    if (!initialized_)
    {
        return;
    }
    if (ole_)
    {
        OleUninitialize();
    }
    else
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
    if (FAILED(factory->GetImage(requested, SIIGBF_ICONONLY_VALUE | SIIGBF_BIGGERSIZEOK_VALUE,
                                 &bitmap)))
    {
        return nullptr;
    }
    return bitmap;
}

DibBitmap loadItemIcon(PCIDLIST_ABSOLUTE pidl, int size)
{
    DibBitmap icon;
    if (HBITMAP bitmap = loadIconFromPidl(pidl, size))
    {
        icon.assignFrom(bitmap, size, size);
        DeleteObject(bitmap);
        icon.premultiply();
    }
    return icon;
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

DibBitmap shieldOverlay(int size)
{
    // The shield icon is rendered over an opaque black and an opaque white
    // backdrop. The color under black is the premultiplied color, and the
    // difference between the two renders gives the alpha of every pixel.
    SHSTOCKICONINFO info{};
    info.cbSize = sizeof(info);
    if (FAILED(SHGetStockIconInfo(SIID_SHIELD, SHGSI_ICONLOCATION, &info)))
    {
        return DibBitmap();
    }
    const int half = size / 2;
    const auto render = [&](std::uint32_t backdrop) {
        DibBitmap surface;
        surface.setSize(size, size);
        surface.fillRect(surface.bound(), backdrop);
        if (HICON icon = shExtractIcon(info.szPath, info.iIcon, half))
        {
            DrawIconEx(surface.dc(), size - half, size - half, icon, half, half, 0, nullptr, DI_NORMAL);
            DestroyIcon(icon);
        }
        GdiFlush();
        return surface;
    };
    const DibBitmap onBlack = render(0xFF000000U);
    const DibBitmap onWhite = render(0xFFFFFFFFU);

    DibBitmap overlay;
    overlay.setSize(size, size);
    const auto* black = static_cast<const std::uint32_t*>(onBlack.bits());
    const auto* white = static_cast<const std::uint32_t*>(onWhite.bits());
    auto* target = static_cast<std::uint32_t*>(overlay.bits());
    if (black == nullptr || white == nullptr || target == nullptr)
    {
        return DibBitmap();
    }
    const std::size_t count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    for (std::size_t i = 0; i < count; ++i)
    {
        const std::uint32_t b = black[i] & 0x00FFFFFFU;
        const std::uint32_t w = white[i] & 0x00FFFFFFU;
        const int difference = static_cast<int>(((w >> 16) & 0xFF) - ((b >> 16) & 0xFF)) +
                               static_cast<int>(((w >> 8) & 0xFF) - ((b >> 8) & 0xFF)) +
                               static_cast<int>((w & 0xFF) - (b & 0xFF));
        const int alpha = std::clamp(255 - difference / 3, 0, 255);
        target[i] = (static_cast<std::uint32_t>(alpha) << 24) | b;
    }
    return overlay;
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
        // The submenu is owned by the caller and must not leak when nothing is shown.
        DestroyMenu(submenu);
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
                // The LinkBar entry goes right before "Properties" (as the original does),
                // or at the very end when the shell does not provide that verb.
                const int itemCount = GetMenuItemCount(popup);
                const int linkbarPosition = insertBefore >= 0 ? insertBefore : itemCount + 1;
                MENUITEMINFOW separatorInfo{};
                separatorInfo.cbSize = sizeof(separatorInfo);
                separatorInfo.fMask = MIIM_FTYPE;
                separatorInfo.fType = MFT_SEPARATOR;
                InsertMenuItemW(popup, static_cast<UINT>(insertBefore >= 0 ? insertBefore : itemCount),
                                TRUE, &separatorInfo);
                MENUITEMINFOW linkbarInfo{};
                linkbarInfo.cbSize = sizeof(linkbarInfo);
                linkbarInfo.fMask = MIIM_SUBMENU | MIIM_STRING;
                linkbarInfo.hSubMenu = submenu;
                linkbarInfo.dwTypeData = const_cast<wchar_t*>(APP_NAME_LINKBAR.data());
                InsertMenuItemW(popup, static_cast<UINT>(insertBefore >= 0 ? insertBefore : itemCount + 1),
                                TRUE, &linkbarInfo);
                const int iconSize = GetSystemMetrics(SM_CXSMICON);
                HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                        MAKEINTRESOURCEW(1), IMAGE_ICON, iconSize, iconSize,
                                        LR_DEFAULTCOLOR));
                if (icon != nullptr)
                {
                    iconBitmap = bitmapFromIcon(icon, iconSize);
                    DestroyIcon(icon);
                    MENUITEMINFOW bitmapInfo{};
                    bitmapInfo.cbSize = sizeof(bitmapInfo);
                    bitmapInfo.fMask = MIIM_BITMAP;
                    bitmapInfo.hbmpItem = iconBitmap;
                    SetMenuItemInfoW(popup, static_cast<UINT>(linkbarPosition), TRUE, &bitmapInfo);
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

bool transferDroppedItems(HWND window, IDataObject* data,
                          const std::wstring& destination, bool move)
{
    if (data == nullptr || destination.empty())
    {
        return false;
    }
    ComPtr<IShellItemArray> items;
    if (FAILED(SHCreateShellItemArrayFromDataObject(data, IID_PPV_ARGS(items.address()))) || !items)
    {
        return false;
    }
    DWORD count = 0;
    if (FAILED(items->GetCount(&count)) || count == 0)
    {
        return false;
    }

    // A trailing separator is not accepted by every shell parser.
    std::wstring folder = destination;
    while (folder.size() > 3 && (folder.back() == L'\\' || folder.back() == L'/'))
    {
        folder.pop_back();
    }
    ComPtr<IShellItem> target;
    if (FAILED(SHCreateItemFromParsingName(folder.c_str(), nullptr,
                                           IID_PPV_ARGS(target.address()))) || !target)
    {
        return false;
    }

    ComPtr<IFileOperation> operation;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(operation.address()))) || !operation)
    {
        return false;
    }
    operation->SetOwnerWindow(window);
    operation->SetOperationFlags(FOF_ALLOWUNDO | FOF_RENAMEONCOLLISION | FOF_NOCONFIRMMKDIR);
    const HRESULT queued = move ? operation->MoveItems(items.get(), target.get())
                                : operation->CopyItems(items.get(), target.get());
    if (FAILED(queued))
    {
        return false;
    }
    return SUCCEEDED(operation->PerformOperations());
}

void deleteDirectory(HWND window, const std::wstring& directory)
{
    // pFrom must be double null-terminated; the shell moves the folder to the
    // Recycle Bin when it fits, so FOF_ALLOWUNDO keeps the removal recoverable.
    std::wstring from = directory;
    while (!from.empty() && (from.back() == L'\\' || from.back() == L'/'))
    {
        from.pop_back();
    }
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};
    operation.hwnd = window;
    operation.wFunc = FO_DELETE;
    operation.pFrom = from.c_str();
    operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&operation);
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

} // namespace lb
