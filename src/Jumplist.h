// Jump list support: reading the shell's automatic/custom destination lists,
// executing/removing/pinning items and the custom-drawn popup window.
//
// COM interface layout follows Classic Shell 4.3.1 (as the original Delphi
// port did) because the destination-list interfaces are undocumented.

#pragma once

#include "ComPtr.h"
#include "Constants.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lb {

enum class JumpItemType : std::uint8_t
{
    Unknown = 0,
    Item = 1,
    Link = 2,
    Separator = 3,
};

enum class JumpGroupType : std::uint8_t
{
    Recent = 0,
    Frequent = 1,
    Tasks = 2,
    Custom = 3,
    Pinned = 4,
};

struct JumpItem
{
    JumpItemType type = JumpItemType::Unknown;
    std::uint32_t hash = 0;
    bool hidden = false;
    bool hasArguments = false;
    std::wstring name;
    ComPtr<IUnknown> object; // shell item or shell link; released with the item
};

struct JumpGroup
{
    JumpGroupType type = JumpGroupType::Recent;
    bool hidden = false;
    std::wstring name;
    std::vector<JumpItem> items;
};

struct JumpList
{
    std::vector<JumpGroup> groups;
    void clear() noexcept;
};

[[nodiscard]] bool getAppInfoForLink(PCIDLIST_ABSOLUTE pidl, std::wstring& appId);
[[nodiscard]] bool hasJumplist(const std::wstring& appId);
[[nodiscard]] bool getJumplist(const std::wstring& appId, JumpList& list, int maxCount);
[[nodiscard]] bool executeJumpItem(const JumpItem& item, HWND owner);
void removeJumpItem(const std::wstring& appId, JumpList& list, int groupIndex, int itemIndex);
void pinJumpItem(const std::wstring& appId, JumpList& list, int groupIndex, int itemIndex,
                 bool pin, int pinIndex);

void createLinkbarTasksJumplist();
void closeJumplist();
[[nodiscard]] bool showJumplist(HWND owner, PCIDLIST_ABSOLUTE pidl, int maxRecent,
                                const RECT& itemRect, PanelAlign align);

} // namespace lb
