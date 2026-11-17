// Win32 Shell integration: PIDLs, icons, verbs, context menus, file
// operations, shell notifications, work areas and OLE drag&drop.

#pragma once

#include "Constants.h"

#include <cstdint>
#include <string>
#include <vector>

struct _FILE_NOTIFY_INFORMATION;

namespace lb {

// RAII COM apartment (STA with OLE1 disabled, as the original does).
class ComGuard final
{
public:
    explicit ComGuard(bool apartmentThreaded = true) noexcept;
    ~ComGuard();
    ComGuard(const ComGuard&) = delete;
    ComGuard& operator=(const ComGuard&) = delete;
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
private:
    bool initialized_ = false;
};

[[nodiscard]] bool parseDisplayName(const std::wstring& path, PIDLIST_ABSOLUTE* pidl) noexcept;
[[nodiscard]] std::wstring displayName(PCIDLIST_ABSOLUTE pidl) noexcept;
[[nodiscard]] PIDLIST_ABSOLUTE parseWorkDir(const std::wstring& path) noexcept;

[[nodiscard]] HICON shExtractIcon(const std::wstring& path, int index, int size);
[[nodiscard]] HBITMAP loadIconFromPidl(PCIDLIST_ABSOLUTE pidl, int size);
void preMultiply(HBITMAP bitmap);

[[nodiscard]] bool checkShield(PCIDLIST_ABSOLUTE pidl);
[[nodiscard]] bool checkBitBucket(PCIDLIST_ABSOLUTE pidl);
[[nodiscard]] HBITMAP shieldOverlay(HDC dc, const std::wstring& path, int index, int size);

void openByDefaultVerb(HWND window, PCIDLIST_ABSOLUTE pidl);
void explorerMenuPopup(HWND window, PCIDLIST_ABSOLUTE pidl, POINT point,
                       bool extended, HMENU submenu);

[[nodiscard]] HRESULT shRename(HWND window, PCIDLIST_ABSOLUTE pidl, const std::wstring& newName);
void shDelete(HWND window, const std::wstring& path, bool allowUndo);

void newShortcut(const std::wstring& directory);
void openDirectory(const std::wstring& directory);
void sendShellEmail(HWND window, const std::wstring& recipient,
                    const std::wstring& subject, const std::wstring& body);

[[nodiscard]] ULONG registerBitBucketNotify(HWND window, UINT message);
void unregisterBitBucketNotify(ULONG cookie) noexcept;

void getUserWorkArea(std::vector<RECT>& rects);

// Starts an OLE drag of one file, with the supplied custom drag image.
void dragFile(HWND window, const std::wstring& fileName, HBITMAP dragImage, POINT offset);

[[nodiscard]] std::wstring uniquePath(const std::wstring& directory,
                                      const std::wstring& templateName);

} // namespace lb
