// Win32 Shell integration: PIDLs, icons, verbs, context menus, file
// operations, shell notifications, work areas and OLE drag&drop.

#pragma once

#include "ComPtr.h"
#include "Constants.h"
#include "DibBitmap.h"

#include <cstdint>
#include <string>
#include <vector>

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
    bool ole_ = false;
};

[[nodiscard]] bool parseDisplayName(const std::wstring& path, PIDLIST_ABSOLUTE* pidl) noexcept;
[[nodiscard]] std::wstring displayName(PCIDLIST_ABSOLUTE pidl) noexcept;
[[nodiscard]] PIDLIST_ABSOLUTE parseWorkDir(const std::wstring& path) noexcept;

[[nodiscard]] HICON shExtractIcon(const std::wstring& path, int index, int size);

// Raw 32-bpp icon as produced by the shell (straight alpha). Caller owns the handle.
[[nodiscard]] HBITMAP loadIconFromPidl(PCIDLIST_ABSOLUTE pidl, int size);
// Same icon as a premultiplied, exactly size x size surface for compositing.
[[nodiscard]] DibBitmap loadItemIcon(PCIDLIST_ABSOLUTE pidl, int size);

[[nodiscard]] bool checkShield(PCIDLIST_ABSOLUTE pidl);
[[nodiscard]] bool checkBitBucket(PCIDLIST_ABSOLUTE pidl);
// Premultiplied shield badge drawn over the bottom-right quarter of an icon.
[[nodiscard]] DibBitmap shieldOverlay(int size);

void openByDefaultVerb(HWND window, PCIDLIST_ABSOLUTE pidl);
void explorerMenuPopup(HWND window, PCIDLIST_ABSOLUTE pidl, POINT point,
                       bool extended, HMENU submenu);

[[nodiscard]] HRESULT shRename(HWND window, PCIDLIST_ABSOLUTE pidl, const std::wstring& newName);
void shDelete(HWND window, const std::wstring& path, bool allowUndo);
void deleteDirectory(HWND window, const std::wstring& directory);

// Copies or moves the shell items of an OLE data object into `destination`.
[[nodiscard]] bool transferDroppedItems(HWND window, IDataObject* data,
                                        const std::wstring& destination, bool move);

void newShortcut(const std::wstring& directory);
void openDirectory(const std::wstring& directory);

[[nodiscard]] ULONG registerBitBucketNotify(HWND window, UINT message);
void unregisterBitBucketNotify(ULONG cookie) noexcept;

void getUserWorkArea(std::vector<RECT>& rects);

// Starts an OLE drag of one file, with the supplied custom drag image.
void dragFile(HWND window, const std::wstring& fileName, HBITMAP dragImage, POINT offset);

} // namespace lb
