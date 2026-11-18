#include "Jumplist.h"

#include "Common.h"
#include "DibBitmap.h"
#include "Hint.h"
#include "Localization.h"
#include "Os.h"
#include "Shell.h"
#include "StringUtil.h"
#include "Theme.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <propsys.h>
#include <propkey.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shobjidl_core.h>
#include <uxtheme.h>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) (static_cast<int>(static_cast<short>(LOWORD(lp))))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) (static_cast<int>(static_cast<short>(HIWORD(lp))))
#endif

namespace lb {
namespace {

// ---------------------------------------------------------------------------
// Undocumented jump list interfaces (Classic Shell layout)
// ---------------------------------------------------------------------------

constexpr std::uint32_t FNV_HASH0 = 2166136261U;
constexpr int JL_LT_PINNED = 0;
constexpr int JL_LT_FREQUENT = 1;
constexpr int JL_LT_RECENT = 2;
constexpr int JL_CT_CUSTOM = 0;
constexpr int JL_CT_NORMAL = 1;
constexpr int JL_CT_TASKS = 2;

const GUID CLSID_ApplicationResolverGuid =
    {0x660B90C8, 0x73A9, 0x4B58, {0x8C, 0xAE, 0x35, 0x5B, 0x7F, 0x55, 0x34, 0x1B}};
const GUID IID_ApplicationResolverW7 =
    {0x46A6EEFF, 0x908E, 0x4DC6, {0x92, 0xA6, 0x64, 0xBE, 0x91, 0x77, 0xB4, 0x1C}};
const GUID IID_ApplicationResolverW8 =
    {0xDE25675A, 0x72DE, 0x44B4, {0x93, 0x73, 0x05, 0x17, 0x04, 0x50, 0xC1, 0x40}};
const GUID IID_IDestinationListValue =
    {0x03F1EED2, 0x8676, 0x430B, {0xAB, 0xE1, 0x76, 0x5C, 0x1D, 0x8F, 0xE1, 0x47}};
const GUID IID_IDestinationList10a =
    {0xFEBD543D, 0x1F7B, 0x4B38, {0x94, 0x0B, 0x59, 0x33, 0xBD, 0x2C, 0xB2, 0x1B}};
const GUID IID_IDestinationList10b =
    {0x507101CD, 0xF6AD, 0x46C8, {0x8E, 0x20, 0xEE, 0xB9, 0xE6, 0xBA, 0xC4, 0x7F}};
const GUID CLSID_AutomaticDestinationListGuid =
    {0xF0AE1542, 0xF497, 0x484B, {0xA1, 0x75, 0xA2, 0x0D, 0xB0, 0x91, 0x44, 0xBA}};
const GUID IID_IAutomaticDestinationListValue =
    {0xBC10DCE3, 0x62F2, 0x4BC6, {0xAF, 0x37, 0xDB, 0x46, 0xED, 0x78, 0x73, 0xC4}};
const GUID IID_IAutomaticDestinationList10b =
    {0xE9C5EF8D, 0xFD41, 0x4F72, {0xBA, 0x87, 0xEB, 0x03, 0xBA, 0xD5, 0x81, 0x7C}};

struct AppDestCategory
{
    INT_PTR type = 0;
    union
    {
        LPCWSTR name;
        INT_PTR subType;
    } data{};
    INT_PTR count = 0;
    INT_PTR padding[10]{};
};

struct IApplicationResolver : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetAppIDForShortcut(IShellItem* item, LPWSTR* appId) = 0;
};

struct IDestinationList : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetMinItems() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetApplicationID(LPCWSTR appId) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSlotCount() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCategoryCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCategory(UINT index, int flags,
                                                   AppDestCategory* category) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteCategory() = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumerateCategoryDestinations(UINT index, REFIID riid,
                                                                    void** object) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDestination(IUnknown* item) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResolveDestination() = 0;
};

struct IAutomaticDestinationList : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Initialize(LPCWSTR appId, LPCWSTR path, LPCWSTR unknown) = 0;
    virtual HRESULT STDMETHODCALLTYPE HasList(BOOL* result) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetList(int type, UINT maxCount, REFIID riid,
                                              void** object) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddUsagePoint() = 0;
    virtual HRESULT STDMETHODCALLTYPE PinItem(IUnknown* item, int pinIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsPinned() = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDestination(IUnknown* item) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetUsageData() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUsageData() = 0;
    virtual HRESULT STDMETHODCALLTYPE ResolveDestination() = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearList(int type) = 0;
};

struct IAutomaticDestinationList10b : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Initialize(LPCWSTR appId, LPCWSTR path, LPCWSTR unknown) = 0;
    virtual HRESULT STDMETHODCALLTYPE HasList(BOOL* result) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetList(int type, UINT maxCount, UINT flags, REFIID riid,
                                              void** object) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddUsagePoint() = 0;
    virtual HRESULT STDMETHODCALLTYPE PinItem(IUnknown* item, int pinIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsPinned() = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDestination(IUnknown* item) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetUsageData() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetUsageData() = 0;
    virtual HRESULT STDMETHODCALLTYPE ResolveDestination() = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearList(int type) = 0;
};

// ---------------------------------------------------------------------------
// COM helpers
// ---------------------------------------------------------------------------

std::uint32_t fnvHash(const void* data, std::size_t length, std::uint32_t hash)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < length; ++i)
    {
        hash = (hash ^ bytes[i]) * 16777619U;
    }
    return hash;
}

std::uint32_t fnvHash(std::wstring_view text, std::uint32_t hash)
{
    return fnvHash(text.data(), text.size() * sizeof(wchar_t), hash);
}

std::wstring propertyStoreString(IPropertyStore* store, REFPROPERTYKEY key)
{
    PROPVARIANT value{};
    PropVariantInit(&value);
    std::wstring result;
    if (SUCCEEDED(store->GetValue(key, &value)))
    {
        if (value.vt == VT_LPWSTR && value.pwszVal != nullptr)
        {
            result = value.pwszVal;
        }
        else if (value.vt == VT_BSTR && value.bstrVal != nullptr)
        {
            result = value.bstrVal;
        }
        else if (value.vt == VT_LPSTR && value.pszVal != nullptr)
        {
            result = utf8ToWide(value.pszVal);
        }
    }
    PropVariantClear(&value);
    return result;
}

std::uint32_t calculateLinkHash(IShellLinkW* link)
{
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(link->GetIDList(&pidl)) || pidl == nullptr)
    {
        return 0;
    }
    std::uint32_t hash = FNV_HASH0;
    PWSTR name = nullptr;
    if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &name)) && name != nullptr)
    {
        CharUpperW(name);
        hash = fnvHash(std::wstring_view(name), hash);
        CoTaskMemFree(name);
    }
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(store.address()))))
    {
        const std::wstring arguments = propertyStoreString(store.get(), PKEY_Link_Arguments);
        if (!arguments.empty())
        {
            hash = fnvHash(arguments, hash);
        }
    }
    CoTaskMemFree(pidl);
    return hash;
}

ComPtr<IDestinationList> getCustomList(const std::wstring& appId)
{
    ComPtr<IUnknown> unknown;
    if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(unknown.address()))))
    {
        return {};
    }
    ComPtr<IDestinationList> list;
    if (os().tenOrAbove)
    {
        if (FAILED(unknown->QueryInterface(IID_IDestinationList10a, list.voidAddress())))
        {
            if (FAILED(unknown->QueryInterface(IID_IDestinationList10b, list.voidAddress())))
            {
                unknown->QueryInterface(IID_IDestinationListValue, list.voidAddress());
            }
        }
    }
    else
    {
        unknown->QueryInterface(IID_IDestinationListValue, list.voidAddress());
    }
    if (list && SUCCEEDED(list->SetApplicationID(appId.c_str())))
    {
        return list;
    }
    return {};
}

class AutomaticList final
{
public:
    explicit AutomaticList(const std::wstring& appId)
    {
        ComPtr<IUnknown> unknown;
        if (FAILED(CoCreateInstance(CLSID_AutomaticDestinationListGuid, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(unknown.address()))))
        {
            return;
        }
        if (FAILED(unknown->QueryInterface(IID_IAutomaticDestinationListValue, base_.voidAddress())))
        {
            if (os().tenOrAbove)
            {
                unknown->QueryInterface(IID_IAutomaticDestinationList10b, v10b_.voidAddress());
            }
        }
        if (base_)
        {
            if (FAILED(base_->Initialize(appId.c_str(), nullptr, nullptr)))
            {
                base_.reset();
            }
        }
        else if (v10b_)
        {
            if (FAILED(v10b_->Initialize(appId.c_str(), nullptr, nullptr)))
            {
                v10b_.reset();
            }
        }
    }

    [[nodiscard]] bool valid() const noexcept { return base_ || v10b_; }

    [[nodiscard]] bool hasList()
    {
        BOOL result = FALSE;
        if (base_)
        {
            if (FAILED(base_->HasList(&result)) || !result) return false;
        }
        else if (v10b_)
        {
            if (FAILED(v10b_->HasList(&result)) || !result) return false;
        }
        else
        {
            return false;
        }
        ComPtr<IObjectCollection> collection = getList(JL_LT_RECENT, 1);
        UINT count = 0;
        if (collection && SUCCEEDED(collection->GetCount(&count)) && count > 0)
        {
            return true;
        }
        collection = getList(JL_LT_PINNED, 1);
        return collection && SUCCEEDED(collection->GetCount(&count)) && count > 0;
    }

    [[nodiscard]] ComPtr<IObjectCollection> getList(int type, UINT maxCount)
    {
        IObjectCollection* collection = nullptr;
        HRESULT result = E_FAIL;
        if (base_)
        {
            result = base_->GetList(type, maxCount, IID_IObjectCollection,
                                    reinterpret_cast<void**>(&collection));
        }
        else if (v10b_)
        {
            result = v10b_->GetList(type, maxCount, 1, IID_IObjectCollection,
                                    reinterpret_cast<void**>(&collection));
        }
        if (FAILED(result))
        {
            return {};
        }
        return ComPtr<IObjectCollection>(collection);
    }

    void pinItem(IUnknown* item, int index)
    {
        if (base_) base_->PinItem(item, index);
        else if (v10b_) v10b_->PinItem(item, index);
    }

    [[nodiscard]] bool removeDestination(IUnknown* item)
    {
        if (base_) return SUCCEEDED(base_->RemoveDestination(item));
        if (v10b_) return SUCCEEDED(v10b_->RemoveDestination(item));
        return false;
    }

private:
    ComPtr<IAutomaticDestinationList> base_;
    ComPtr<IAutomaticDestinationList10b> v10b_;
};

IApplicationResolver* applicationResolver()
{
    static ComPtr<IApplicationResolver> resolver;
    static ULONGLONG lastCheck = 0;
    const ULONGLONG now = GetTickCount64();
    if (!resolver || now - lastCheck > 60000)
    {
        lastCheck = now;
        resolver.reset();
        ComPtr<IUnknown> unknown;
        if (SUCCEEDED(CoCreateInstance(CLSID_ApplicationResolverGuid, nullptr, CLSCTX_ALL,
                                       IID_PPV_ARGS(unknown.address()))))
        {
            const GUID& iid = os().eightOrAbove ? IID_ApplicationResolverW8
                                                 : IID_ApplicationResolverW7;
            unknown->QueryInterface(iid, resolver.voidAddress());
        }
    }
    return resolver.get();
}

void addJumpItem(JumpGroup& group, IUnknown* object,
                 const std::vector<ComPtr<IShellItem>>& ignoreItems,
                 const std::vector<std::uint32_t>& ignoreLinks)
{
    if (object == nullptr)
    {
        return;
    }
    JumpItem item;
    ComPtr<IUnknown> holder(object);
    object->AddRef();

    ComPtr<IShellItem> shellItem;
    if (SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(shellItem.address()))) && shellItem)
    {
        for (const auto& ignored : ignoreItems)
        {
            int order = 0;
            if (SUCCEEDED(shellItem->Compare(ignored.get(),
                    static_cast<DWORD>(SICHINT_CANONICAL | SICHINT_TEST_FILESYSPATH_IF_NOT_EQUAL),
                    &order)) && order == 0)
            {
                return;
            }
        }
        item.type = JumpItemType::Item;
        PWSTR name = nullptr;
        if (FAILED(shellItem->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) || name == nullptr)
        {
            return;
        }
        item.name = name;
        CoTaskMemFree(name);
        PWSTR parsing = nullptr;
        if (SUCCEEDED(shellItem->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsing)) &&
            parsing != nullptr)
        {
            CharUpperW(parsing);
            item.hash = fnvHash(std::wstring_view(parsing), FNV_HASH0);
            CoTaskMemFree(parsing);
        }
        item.object = std::move(holder);
        group.items.push_back(std::move(item));
        return;
    }

    ComPtr<IShellLinkW> link;
    if (SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(link.address()))) && link)
    {
        const std::uint32_t hash = calculateLinkHash(link.get());
        if (std::find(ignoreLinks.begin(), ignoreLinks.end(), hash) != ignoreLinks.end())
        {
            return;
        }
        item.type = JumpItemType::Link;
        ComPtr<IPropertyStore> store;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(store.address()))))
        {
            PROPVARIANT value{};
            PropVariantInit(&value);
            if (group.type == JumpGroupType::Tasks &&
                SUCCEEDED(store->GetValue(PKEY_AppUserModel_IsDestListSeparator, &value)) &&
                value.vt == VT_BOOL && value.boolVal != VARIANT_FALSE)
            {
                item.type = JumpItemType::Separator;
                item.name = L"-";
            }
            else
            {
                const std::wstring title = propertyStoreString(store.get(), PKEY_Title);
                if (!title.empty())
                {
                    wchar_t expanded[512]{};
                    if (SUCCEEDED(SHLoadIndirectString(title.c_str(), expanded,
                                                       static_cast<UINT>(std::size(expanded)), nullptr)))
                    {
                        item.name = expanded;
                    }
                    else
                    {
                        item.name = title;
                    }
                }
            }
            PropVariantClear(&value);
        }

        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(link->GetIDList(&pidl)) && pidl != nullptr)
        {
            if (item.name.empty())
            {
                PWSTR name = nullptr;
                if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &name)) &&
                    name != nullptr)
                {
                    item.name = name;
                    CoTaskMemFree(name);
                }
            }
            PWSTR parsing = nullptr;
            if (SUCCEEDED(SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &parsing)) &&
                parsing != nullptr)
            {
                CharUpperW(parsing);
                item.hash = fnvHash(std::wstring_view(parsing), FNV_HASH0);
                CoTaskMemFree(parsing);
            }
            CoTaskMemFree(pidl);
            if (store)
            {
                const std::wstring arguments = propertyStoreString(store.get(), PKEY_Link_Arguments);
                if (!arguments.empty())
                {
                    item.hash = fnvHash(arguments, item.hash);
                    item.hasArguments = true;
                }
            }
        }
        if (!item.name.empty())
        {
            item.object = std::move(holder);
            group.items.push_back(std::move(item));
        }
        return;
    }
}

void addCollection(JumpGroup& group, IObjectCollection* collection,
                   const std::vector<ComPtr<IShellItem>>& ignoreItems,
                   const std::vector<std::uint32_t>& ignoreLinks)
{
    UINT count = 0;
    if (collection == nullptr || FAILED(collection->GetCount(&count)))
    {
        return;
    }
    for (UINT i = 0; i < count; ++i)
    {
        IUnknown* object = nullptr;
        if (SUCCEEDED(collection->GetAt(i, IID_IUnknown, reinterpret_cast<void**>(&object))) &&
            object != nullptr)
        {
            addJumpItem(group, object, ignoreItems, ignoreLinks);
            object->Release();
        }
    }
}

// ---------------------------------------------------------------------------
// Visual jump list window
// ---------------------------------------------------------------------------

constexpr int TIMER_TOOLTIP_SHOW = 4;
constexpr int TIMER_TOOLTIP_HIDE = 5;
constexpr int INDEX_NONE = -1;
constexpr int INDEX_PIN = 0x8000;
constexpr int MASK_PIN = 0x7FFF;
constexpr wchar_t W10_SYMBOL_PIN = 0xE718;
constexpr wchar_t W10_SYMBOL_UNPIN = 0xE77A;

enum class VtStyle { Item, Group, Separator, Footer };

struct VtItem
{
    VtStyle style = VtStyle::Item;
    int group = -1;
    int item = -1;
    RECT rect{};
    int icon = -1;
    bool latestPinned = false;
    bool pinnable = false;
    std::wstring caption;

    [[nodiscard]] bool selectable() const noexcept
    {
        return style == VtStyle::Item || style == VtStyle::Footer;
    }
};

struct JlColors
{
    std::uint32_t body = 0;
    std::uint32_t footer = 0;
    std::uint32_t selected = 0;
    std::uint32_t hot = 0;
    std::uint32_t separator = 0;
    COLORREF text = 0;
    COLORREF textSelected = 0;
    COLORREF textGroup = 0;
};

class JumpListWindow final
{
public:
    static JumpListWindow* instance;

    ~JumpListWindow();

    [[nodiscard]] bool create(HWND owner, PCIDLIST_ABSOLUTE pidl, int maxRecent,
                              const RECT& itemRect, PanelAlign align);
    void close();

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam) noexcept;
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    [[nodiscard]] bool build();
    void layout();
    void redraw();
    void paint(HDC dc);
    void drawItem(HDC dc, int index, bool selected);
    [[nodiscard]] int itemAt(int x, int y) const;
    [[nodiscard]] int hotItem() const noexcept
    {
        return hotIndex_ == INDEX_NONE ? INDEX_NONE : hotIndex_ & MASK_PIN;
    }
    [[nodiscard]] bool pinSelected() const noexcept { return (hotIndex_ & INDEX_PIN) != 0; }

    void setHot(int value);
    void click();
    void keyboard(UINT key);
    void showMenu(int x, int y);
    void executeSelected();
    void pinSelectedItem(bool pin);
    void removeSelected();
    void updateTooltip();
    [[nodiscard]] int iconFor(const JumpItem& item);
    [[nodiscard]] int iconForPidl(PCIDLIST_ABSOLUTE pidl);
    void applyTheme();
    [[nodiscard]] std::wstring description(int index) const;

    HWND window_ = nullptr;
    HWND owner_ = nullptr;
    PIDLIST_ABSOLUTE appExe_ = nullptr;
    std::wstring appId_;
    JumpList list_;
    int maxCount_ = 0;
    RECT itemRect_{};
    PanelAlign align_ = PanelAlign::Top;

    HIMAGELIST imageList_ = nullptr;
    HFONT font_ = nullptr;
    HFONT boldFont_ = nullptr;
    HFONT symbolFont_ = nullptr;
    std::unique_ptr<Tooltip> tooltip_;

    std::vector<VtItem> visual_;
    DibBitmap background_;
    RECT bodyRect_{};
    RECT footerRect_{};

    int iconSize_ = 16;
    int listWidth_ = 269;
    int itemWidth_ = 255;
    int itemHeight_ = 24;
    int separatorHeight_ = 24;
    int itemSpacing_ = 2;
    SIZE itemPadding_{2, 2};
    SIZE itemMargin_{7, 7};
    int textOffset_ = 20;
    int textGroupOffset_ = 0;
    int pinButtonWidth_ = 20;
    int formOffset_ = 0;

    JlColors colors_{};
    int hotIndex_ = INDEX_NONE;
    bool hotByMouse_ = false;
    bool menuVisible_ = false;
    bool tracking_ = false;
    int lastPinHash_ = 0;
    int tempX_ = 0;
    int tempY_ = 0;
};

JumpListWindow* JumpListWindow::instance = nullptr;
std::unique_ptr<JumpListWindow> activeJumplist;
std::vector<std::unique_ptr<JumpListWindow>> retiredJumplists;

JumpListWindow::~JumpListWindow()
{
    if (tooltip_)
    {
        tooltip_.reset();
    }
    if (imageList_ != nullptr)
    {
        ImageList_Destroy(imageList_);
    }
    if (font_ != nullptr) DeleteObject(font_);
    if (boldFont_ != nullptr) DeleteObject(boldFont_);
    if (symbolFont_ != nullptr) DeleteObject(symbolFont_);
    CoTaskMemFree(appExe_);
    list_.clear();
    if (instance == this)
    {
        instance = nullptr;
    }
}

void applyThemeColors(JlColors& colors)
{
    if (os().isTen)
    {
        const Look look = globals().look;
        if (look == Look::Light)
        {
            colors.body = 0xFFD5D5D5U;
            colors.footer = 0xFFE4E4E4U;
            colors.selected = 0xD4FFFFFFU;
            colors.hot = 0x99FFFFFFU;
            colors.separator = 0x66000000U;
            colors.text = RGB(0, 0, 0);
            colors.textSelected = RGB(0, 0, 0);
            colors.textGroup = RGB(40, 40, 40);
        }
        else
        {
            const std::uint32_t body = (look == Look::Accent)
                ? immersiveColor(L"ImmersiveSystemAccentDark1")
                : immersiveColor(L"ImmersiveDarkChromeMedium");
            const std::uint32_t footer = (look == Look::Accent)
                ? immersiveColor(L"ImmersiveSystemAccentDark2")
                : immersiveColor(L"ImmersiveDarkChromeTaskbarBase");
            colors.body = swapRedBlue(body);
            colors.footer = swapRedBlue(footer);
            colors.selected = 0x30FFFFFFU;
            colors.hot = 0x19FFFFFFU;
            colors.separator = 0x66FFFFFFU;
            colors.text = RGB(255, 255, 255);
            colors.textSelected = RGB(255, 255, 255);
            colors.textGroup = RGB(200, 200, 200);
        }
    }
    else
    {
        colors.body = GetSysColor(COLOR_MENU);
        colors.footer = GetSysColor(COLOR_MENU);
        colors.selected = GetSysColor(COLOR_HIGHLIGHT);
        colors.hot = GetSysColor(COLOR_HIGHLIGHT);
        colors.separator = GetSysColor(COLOR_3DSHADOW);
        colors.text = GetSysColor(COLOR_MENUTEXT);
        colors.textSelected = GetSysColor(COLOR_HIGHLIGHTTEXT);
        colors.textGroup = GetSysColor(COLOR_MENUTEXT);
    }
}

int scaleForDpi(int value) noexcept
{
    return scaleDimension(value);
}

bool JumpListWindow::create(HWND owner, PCIDLIST_ABSOLUTE pidl, int maxRecent,
                            const RECT& itemRect, PanelAlign align)
{
    owner_ = owner;
    appExe_ = ILClone(pidl);
    maxCount_ = maxRecent;
    itemRect_ = itemRect;
    align_ = align;
    const bool appInfo = getAppInfoForLink(pidl, appId_);
    if (!appInfo || appId_.empty() || !hasJumplist(appId_))
    {
        return false;
    }

    static bool classRegistered = false;
    if (!classRegistered)
    {
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &JumpListWindow::windowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = L"LINKBAR_JUMPLIST";
        RegisterClassW(&windowClass);
        classRegistered = true;
    }

    iconSize_ = GetSystemMetrics(SM_CXSMICON);
    listWidth_ = scaleForDpi(269);

    if (os().isTen)
    {
        itemMargin_ = SIZE{1 + scaleForDpi(1), scaleForDpi(8)};
        itemPadding_ = SIZE{scaleForDpi(12), scaleForDpi(7)};
        itemHeight_ = iconSize_ + 2 * itemPadding_.cy;
        separatorHeight_ = scaleForDpi(11);
        itemSpacing_ = 0;
        textGroupOffset_ = itemPadding_.cx;
        pinButtonWidth_ = itemHeight_;
        textOffset_ = iconSize_ + 2 * itemPadding_.cx;
        formOffset_ = 0;
    }
    else
    {
        itemMargin_ = SIZE{7, 7};
        itemPadding_ = SIZE{2, 2};
        itemHeight_ = iconSize_ + 4;
        separatorHeight_ = itemHeight_;
        itemSpacing_ = 2;
        textGroupOffset_ = 0;
        pinButtonWidth_ = iconSize_ + scaleForDpi(10);
        textOffset_ = itemHeight_ + scaleForDpi(3);
        formOffset_ = compositionEnabled() ? scaleForDpi(3) : 0;
    }
    itemWidth_ = listWidth_ - 2 * itemMargin_.cx;

    applyThemeColors(colors_);

    imageList_ = ImageList_Create(iconSize_, iconSize_, ILC_COLOR32 | ILC_MASK, 8, 8);
    SHSTOCKICONINFO stock{};
    stock.cbSize = sizeof(stock);
    if (SUCCEEDED(SHGetStockIconInfo(SIID_DOCNOASSOC, SHGSI_ICON, &stock)))
    {
        ImageList_AddIcon(imageList_, stock.hIcon);
        DestroyIcon(stock.hIcon);
    }
    if (os().isTen)
    {
        if (SUCCEEDED(SHGetStockIconInfo(SIID_DOCNOASSOC, SHGSI_ICON, &stock)))
        {
            ImageList_AddIcon(imageList_, stock.hIcon);
            ImageList_AddIcon(imageList_, stock.hIcon);
            DestroyIcon(stock.hIcon);
        }
    }
    else
    {
        HMODULE module = LoadLibraryExW(L"imageres.dll", nullptr, LOAD_LIBRARY_AS_DATAFILE);
        if (module != nullptr)
        {
            HICON icon = reinterpret_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(5100),
                IMAGE_ICON, iconSize_, iconSize_, LR_DEFAULTCOLOR));
            if (icon != nullptr)
            {
                ImageList_AddIcon(imageList_, icon);
                DestroyIcon(icon);
            }
            icon = reinterpret_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(5101),
                IMAGE_ICON, iconSize_, iconSize_, LR_DEFAULTCOLOR));
            if (icon != nullptr)
            {
                ImageList_AddIcon(imageList_, icon);
                DestroyIcon(icon);
            }
            FreeLibrary(module);
        }
    }

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
    font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    LOGFONTW bold = metrics.lfMessageFont;
    bold.lfWeight = FW_BOLD;
    boldFont_ = CreateFontIndirectW(&bold);
    symbolFont_ = CreateFontW(-iconSize_, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe MDL2 Assets");

    if (!build())
    {
        return false;
    }

    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"LINKBAR_JUMPLIST",
                              L"", WS_POPUP, 0, 0, listWidth_, footerRect_.bottom,
                              owner, nullptr, GetModuleHandleW(nullptr), this);
    if (window_ == nullptr)
    {
        return false;
    }
    tooltip_ = std::make_unique<Tooltip>(window_);
    layout();
    return true;
}

void JumpListWindow::close()
{
    if (window_ != nullptr && IsWindow(window_))
    {
        DestroyWindow(window_);
    }
    window_ = nullptr;
}

bool JumpListWindow::build()
{
    list_.clear();
    if (!getJumplist(appId_, list_, maxCount_))
    {
        return false;
    }
    visual_.clear();
    RECT current{};
    for (std::size_t g = 0; g < list_.groups.size(); ++g)
    {
        JumpGroup& group = list_.groups[g];
        if (group.hidden)
        {
            continue;
        }
        VtItem header;
        header.style = VtStyle::Group;
        header.group = static_cast<int>(g);
        if (IsRectEmpty(&current))
        {
            current = makeRect(itemMargin_.cx, itemSpacing_, itemWidth_, itemHeight_);
        }
        else
        {
            current.top += current.bottom - current.top + itemSpacing_;
            current.bottom = current.top + itemHeight_;
        }
        header.rect = current;
        visual_.push_back(header);

        for (std::size_t i = 0; i < group.items.size(); ++i)
        {
            JumpItem& item = group.items[i];
            if (item.hidden)
            {
                continue;
            }
            if (item.type == JumpItemType::Separator)
            {
                VtItem separator;
                separator.style = VtStyle::Separator;
                separator.group = static_cast<int>(g);
                current.top = current.bottom + itemSpacing_;
                current.bottom = current.top + separatorHeight_;
                separator.rect = current;
                visual_.push_back(separator);
                continue;
            }
            VtItem entry;
            entry.style = VtStyle::Item;
            entry.group = static_cast<int>(g);
            entry.item = static_cast<int>(i);
            entry.icon = iconFor(item);
            entry.latestPinned = lastPinHash_ != 0 && item.hash == static_cast<std::uint32_t>(lastPinHash_);
            entry.pinnable = group.type != JumpGroupType::Tasks;
            current.top = current.bottom + itemSpacing_;
            current.bottom = current.top + itemHeight_;
            entry.rect = current;
            visual_.push_back(entry);
        }
    }

    if (visual_.empty())
    {
        return false;
    }
    bool hasItem = false;
    for (const VtItem& entry : visual_)
    {
        if (entry.style == VtStyle::Item)
        {
            hasItem = true;
            break;
        }
    }
    if (!hasItem)
    {
        return false;
    }

    bodyRect_ = makeRect(0, 0, listWidth_, current.bottom + itemMargin_.cy);
    footerRect_ = makeRect(0, bodyRect_.bottom, listWidth_,
        itemSpacing_ + itemMargin_.cy + itemHeight_ + itemMargin_.cy);

    VtItem footer;
    footer.style = VtStyle::Footer;
    footer.rect = makeRect(itemMargin_.cx, footerRect_.top + itemSpacing_ + itemMargin_.cy,
                           itemWidth_, itemHeight_);
    footer.icon = 0;
    footer.caption = L"???";
    ComPtr<IShellItem> appItem;
    if (SUCCEEDED(SHCreateItemFromIDList(appExe_, IID_PPV_ARGS(appItem.address()))) && appItem)
    {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (SUCCEEDED(SHGetIDListFromObject(appItem.get(), &pidl)) && pidl != nullptr)
        {
            footer.icon = iconForPidl(pidl);
            CoTaskMemFree(pidl);
        }
        PWSTR name = nullptr;
        if (SUCCEEDED(appItem->GetDisplayName(SIGDN_PARENTRELATIVEEDITING, &name)) && name != nullptr)
        {
            footer.caption = name;
            CoTaskMemFree(name);
        }
    }
    visual_.push_back(footer);

    background_.setSize(listWidth_, footerRect_.bottom);
    redraw();
    return true;
}

void JumpListWindow::layout()
{
    const int width = listWidth_;
    const int height = footerRect_.bottom;
    int left = 0;
    int top = 0;
    switch (align_)
    {
    case PanelAlign::Left:
        left = itemRect_.right + formOffset_;
        top = itemRect_.top - 1;
        break;
    case PanelAlign::Top:
        left = centerPoint(itemRect_).x - width / 2;
        top = itemRect_.bottom + formOffset_;
        break;
    case PanelAlign::Right:
        left = itemRect_.left - width - formOffset_;
        top = itemRect_.top - 1;
        break;
    case PanelAlign::Bottom:
        left = centerPoint(itemRect_).x - width / 2;
        top = itemRect_.top - height - formOffset_;
        break;
    }

    HMONITOR monitor = MonitorFromPoint(centerPoint(itemRect_), MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor, &info);
    if (left + width > info.rcMonitor.right)
    {
        left = info.rcMonitor.right - width;
    }
    left = std::max<int>(left, static_cast<int>(info.rcMonitor.left));
    if (top + height > info.rcMonitor.bottom)
    {
        top = info.rcMonitor.bottom - height;
    }
    top = std::max<int>(top, static_cast<int>(info.rcMonitor.top));

    SetWindowPos(window_, HWND_TOPMOST, left, top, width, height,
                 SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    SetForegroundWindow(window_);
    SetFocus(window_);
    redraw();
    InvalidateRect(window_, nullptr, FALSE);
}

int JumpListWindow::iconForPidl(PCIDLIST_ABSOLUTE pidl)
{
    HBITMAP bitmap = loadIconFromPidl(pidl, iconSize_);
    if (bitmap == nullptr)
    {
        return 0;
    }
    const int index = ImageList_Add(imageList_, bitmap, nullptr);
    DeleteObject(bitmap);
    return index >= 0 ? index : 0;
}

int JumpListWindow::iconFor(const JumpItem& item)
{
    if (item.type == JumpItemType::Link && item.object)
    {
        ComPtr<IShellLinkW> link;
        if (SUCCEEDED(item.object->QueryInterface(IID_PPV_ARGS(link.address()))) && link)
        {
            wchar_t location[MAX_PATH]{};
            int iconIndex = 0;
            if (SUCCEEDED(link->GetIconLocation(location, MAX_PATH, &iconIndex)) &&
                location[0] != L'\0')
            {
                if (iconIndex < 0)
                {
                    iconIndex = 0;
                }
                HICON icon = shExtractIcon(location, iconIndex, iconSize_);
                if (icon != nullptr)
                {
                    const int index = ImageList_AddIcon(imageList_, icon);
                    DestroyIcon(icon);
                    return index >= 0 ? index : 0;
                }
            }
        }
    }
    if (item.object)
    {
        ComPtr<IShellItem> shellItem;
        if (SUCCEEDED(item.object->QueryInterface(IID_PPV_ARGS(shellItem.address()))) && shellItem)
        {
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (SUCCEEDED(SHGetIDListFromObject(shellItem.get(), &pidl)) && pidl != nullptr)
            {
                const int index = iconForPidl(pidl);
                CoTaskMemFree(pidl);
                return index;
            }
        }
        ComPtr<IShellLinkW> link;
        if (SUCCEEDED(item.object->QueryInterface(IID_PPV_ARGS(link.address()))) && link)
        {
            PIDLIST_ABSOLUTE pidl = nullptr;
            if (SUCCEEDED(link->GetIDList(&pidl)) && pidl != nullptr)
            {
                const int index = iconForPidl(pidl);
                CoTaskMemFree(pidl);
                return index;
            }
        }
    }
    return 0;
}

void JumpListWindow::redraw()
{
    if (background_.handle() == nullptr)
    {
        return;
    }
    HDC dc = background_.dc();
    const int width = background_.width();
    const int height = background_.height();
    RECT all{0, 0, width, height};
    HBRUSH body = CreateSolidBrush(colors_.body & 0x00FFFFFFU);
    FillRect(dc, &all, body);
    DeleteObject(body);
    HBRUSH footerBrush = CreateSolidBrush(colors_.footer & 0x00FFFFFFU);
    FillRect(dc, &footerRect_, footerBrush);
    DeleteObject(footerBrush);

    for (std::size_t i = 0; i < visual_.size(); ++i)
    {
        drawItem(dc, static_cast<int>(i), hotItem() == static_cast<int>(i));
    }

    if (os().isTen)
    {
        RECT border = all;
        switch (align_)
        {
        case PanelAlign::Left: border.left -= 1; break;
        case PanelAlign::Top: border.top -= 1; break;
        case PanelAlign::Right: border.right += 1; break;
        case PanelAlign::Bottom: border.bottom += 1; break;
        }
        FrameRect(dc, &border, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
}

void JumpListWindow::drawItem(HDC dc, int index, bool selected)
{
    if (index < 0 || static_cast<std::size_t>(index) >= visual_.size())
    {
        return;
    }
    const VtItem& item = visual_[static_cast<std::size_t>(index)];
    if (item.style == VtStyle::Separator)
    {
        if (os().isTen)
        {
            RECT line = makeRect(item.rect.left + itemPadding_.cx,
                                 centerPoint(item.rect).y,
                                 rectWidth(item.rect) - itemPadding_.cx * 2,
                                 scaleForDpi(1));
            HBRUSH brush = CreateSolidBrush(colors_.separator & 0x00FFFFFFU);
            FillRect(dc, &line, brush);
            DeleteObject(brush);
        }
        return;
    }

    if (item.style == VtStyle::Group)
    {
        const wchar_t* name = list_.groups[static_cast<std::size_t>(item.group)].name.c_str();
        HFONT previous = static_cast<HFONT>(SelectObject(dc, boldFont_));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, colors_.textGroup);
        RECT rect = item.rect;
        rect.left += textGroupOffset_;
        DrawTextW(dc, name, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(dc, previous);
        return;
    }

    // Background of a selectable item.
    if (selected)
    {
        HBRUSH brush = CreateSolidBrush(colors_.selected & 0x00FFFFFFU);
        FillRect(dc, &item.rect, brush);
        DeleteObject(brush);
    }

    // Pin button.
    RECT itemRect = item.rect;
    if (item.pinnable && (item.style == VtStyle::Item))
    {
        RECT pinRect = itemRect;
        pinRect.left = itemRect.right - pinButtonWidth_;
        const bool pinActive = selected && pinSelected();
        if ((selected || pinActive) && os().isTen)
        {
            const std::uint32_t color = (pinActive ? colors_.selected : colors_.hot);
            HBRUSH brush = CreateSolidBrush(color & 0x00FFFFFFU);
            FillRect(dc, &pinRect, brush);
            DeleteObject(brush);
        }
        else if ((selected || pinActive) && !os().isTen)
        {
            HBRUSH brush = CreateSolidBrush(colors_.hot);
            FrameRect(dc, &pinRect, brush);
            DeleteObject(brush);
        }
        if (pinActive)
        {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, colors_.textSelected);
            const bool pinned = list_.groups[static_cast<std::size_t>(item.group)].type ==
                                JumpGroupType::Pinned;
            if (os().isTen)
            {
                HFONT previous = static_cast<HFONT>(SelectObject(dc, symbolFont_));
                const wchar_t symbol = pinned ? W10_SYMBOL_UNPIN : W10_SYMBOL_PIN;
                RECT textRect = pinRect;
                DrawTextW(dc, &symbol, 1, &textRect,
                          DT_SINGLELINE | DT_VCENTER | DT_CENTER);
                SelectObject(dc, previous);
            }
            else
            {
                RECT textRect = pinRect;
                DrawTextW(dc, pinned ? L"U" : L"P", 1, &textRect,
                          DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            }
        }
    }

    // Icon.
    if (item.icon >= 0)
    {
        ImageList_Draw(imageList_, item.icon, dc, itemRect.left + itemPadding_.cx,
                       itemRect.top + itemPadding_.cy, ILD_TRANSPARENT);
    }

    // Caption.
    const std::wstring text = item.style == VtStyle::Footer
        ? item.caption
        : list_.groups[static_cast<std::size_t>(item.group)]
              .items[static_cast<std::size_t>(item.item)].name;
    HFONT previous = static_cast<HFONT>(SelectObject(dc, font_));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? colors_.textSelected : colors_.text);
    RECT textRect = itemRect;
    textRect.left += textOffset_;
    textRect.right -= 1;
    DrawTextW(dc, text.c_str(), -1, &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_WORD_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
}

void JumpListWindow::paint(HDC dc)
{
    if (background_.handle() != nullptr)
    {
        BitBlt(dc, 0, 0, background_.width(), background_.height(),
               background_.dc(), 0, 0, SRCCOPY);
    }
}

int JumpListWindow::itemAt(int x, int y) const
{
    const POINT point{x, y};
    for (std::size_t i = 0; i < visual_.size(); ++i)
    {
        if (pointInRect(visual_[i].rect, point))
        {
            return static_cast<int>(i);
        }
    }
    return INDEX_NONE;
}

void JumpListWindow::setHot(int value)
{
    int index = value;
    if (index != INDEX_NONE)
    {
        const int base = index & MASK_PIN;
        if (base < 0 || static_cast<std::size_t>(base) >= visual_.size() ||
            !visual_[static_cast<std::size_t>(base)].selectable())
        {
            index = INDEX_NONE;
        }
    }
    if (index == hotIndex_)
    {
        return;
    }
    hotIndex_ = index;
    tooltip_->cancel();
    redraw();
    InvalidateRect(window_, nullptr, FALSE);
    if (hotItem() != INDEX_NONE)
    {
        SetTimer(window_, TIMER_TOOLTIP_SHOW, GetDoubleClickTime(), nullptr);
    }
}

void JumpListWindow::updateTooltip()
{
    const int index = hotItem();
    if (index == INDEX_NONE)
    {
        return;
    }
    const VtItem& item = visual_[static_cast<std::size_t>(index)];
    std::wstring text;
    if (item.pinnable && pinSelected())
    {
        const bool pinned = list_.groups[static_cast<std::size_t>(item.group)].type ==
                            JumpGroupType::Pinned;
        text = pinned ? l10n(L"Jumplist.UnpinTip", L"Unpin from this list")
                      : l10n(L"Jumplist.PinTip", L"Pin to this list");
    }
    else
    {
        text = description(index);
    }
    if (text.empty())
    {
        return;
    }
    POINT point = hotByMouse_ ? currentMessagePoint()
                              : POINT{item.rect.left + textOffset_, item.rect.bottom};
    if (!hotByMouse_)
    {
        ClientToScreen(window_, &point);
    }
    tooltip_->activate(point, text, HorzAlign::Left, VertAlign::Top);
    SetTimer(window_, TIMER_TOOLTIP_HIDE, GetDoubleClickTime() * 10, nullptr);
}

std::wstring JumpListWindow::description(int index) const
{
    const VtItem& visual = visual_[static_cast<std::size_t>(index)];
    if (visual.style == VtStyle::Footer)
    {
        return visual.caption;
    }
    if (visual.style != VtStyle::Item)
    {
        return {};
    }
    return list_.groups[static_cast<std::size_t>(visual.group)]
        .items[static_cast<std::size_t>(visual.item)].name;
}

void JumpListWindow::click()
{
    const int index = hotItem();
    if (index == INDEX_NONE)
    {
        return;
    }
    const VtItem& item = visual_[static_cast<std::size_t>(index)];
    if (item.style == VtStyle::Footer)
    {
        HWND owner = owner_;
        PIDLIST_ABSOLUTE appExe = ILClone(appExe_);
        closeJumplist();
        openByDefaultVerb(owner, appExe);
        CoTaskMemFree(appExe);
        return;
    }
    if (item.style != VtStyle::Item)
    {
        return;
    }
    const JumpGroupType groupType =
        list_.groups[static_cast<std::size_t>(item.group)].type;
    if (item.pinnable)
    {
        if (pinSelected())
        {
            pinSelectedItem(groupType != JumpGroupType::Pinned);
            return;
        }
    }
    executeSelected();
}

void JumpListWindow::executeSelected()
{
    const int index = hotItem();
    if (index == INDEX_NONE)
    {
        return;
    }
    const VtItem& visual = visual_[static_cast<std::size_t>(index)];
    if (visual.style != VtStyle::Item)
    {
        return;
    }
    const JumpItem& item = list_.groups[static_cast<std::size_t>(visual.group)]
                                .items[static_cast<std::size_t>(visual.item)];
    // The copy shares the shell object, so it stays alive after the window closes.
    const JumpItem copy = item;
    const HWND owner = owner_;
    closeJumplist();
    (void)executeJumpItem(copy, owner);
}

void JumpListWindow::pinSelectedItem(bool pin)
{
    const int index = hotItem();
    if (index == INDEX_NONE)
    {
        return;
    }
    const VtItem& visual = visual_[static_cast<std::size_t>(index)];
    const JumpItem& item = list_.groups[static_cast<std::size_t>(visual.group)]
                                .items[static_cast<std::size_t>(visual.item)];
    lastPinHash_ = static_cast<int>(item.hash);
    pinJumpItem(appId_, list_, visual.group, visual.item, pin, pin ? -1 : 0);
    if (window_ != nullptr)
    {
        visual_.clear();
        background_.setSize(0, 0);
        (void)build();
        layout();
    }
}

void JumpListWindow::removeSelected()
{
    const int index = hotItem();
    if (index == INDEX_NONE)
    {
        return;
    }
    const VtItem& visual = visual_[static_cast<std::size_t>(index)];
    removeJumpItem(appId_, list_, visual.group, visual.item);
    visual_.clear();
    background_.setSize(0, 0);
    (void)build();
    layout();
}

void JumpListWindow::showMenu(int x, int y)
{
    const int index = hotItem();
    if (index == INDEX_NONE)
    {
        return;
    }
    const VtItem& item = visual_[static_cast<std::size_t>(index)];
    menuVisible_ = true;
    tooltip_->cancel();
    HWND window = window_;
    if (item.style == VtStyle::Footer)
    {
        POINT point{x, y};
        ClientToScreen(window_, &point);
        // The bar's own submenu is attached to the shell menu, so the shortcut
        // can be configured from here as well.
        HMENU submenu = CreatePopupMenu();
        if (submenu != nullptr)
        {
            AppendMenuW(submenu, MF_STRING, CMD_ITEM_TEXT,
                        l10n(L"Menu.ShowText", L"Show text").c_str());
            AppendMenuW(submenu, MF_STRING, CMD_ITEM_TEXT_COLOR,
                        l10n(L"Menu.TextColor", L"Text color...").c_str());
            AppendMenuW(submenu, MF_STRING, CMD_ITEM_TEXT_COLOR_RESET,
                        l10n(L"Menu.TextColorDefault", L"Default text color").c_str());
        }
        explorerMenuPopup(window_, appExe_, point, GetKeyState(VK_SHIFT) < 0, submenu);
        menuVisible_ = false;
        if (IsWindow(window))
        {
            SetFocus(window);
        }
        return;
    }
    if (item.style != VtStyle::Item)
    {
        menuVisible_ = false;
        return;
    }

    const JumpGroupType groupType =
        list_.groups[static_cast<std::size_t>(item.group)].type;
    HMENU menu = CreatePopupMenu();
    if (menu != nullptr)
    {
        AppendMenuW(menu, MF_STRING, 1, l10n(L"Jumplist.Open", L"&Open").c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (groupType == JumpGroupType::Pinned)
        {
            AppendMenuW(menu, MF_STRING, 2,
                        l10n(L"Jumplist.Unpin", L"&Unpin from this list").c_str());
        }
        else if (groupType == JumpGroupType::Recent || groupType == JumpGroupType::Frequent ||
                 groupType == JumpGroupType::Custom)
        {
            AppendMenuW(menu, MF_STRING, 3,
                        l10n(L"Jumplist.Pin", L"P&in to this list").c_str());
            AppendMenuW(menu, MF_STRING, 4,
                        l10n(L"Jumplist.Remove", L"Remove &from this list").c_str());
        }
        POINT point{x, y};
        ClientToScreen(window_, &point);
        const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                              point.x, point.y, window_, nullptr);
        DestroyMenu(menu);
        menuVisible_ = false;
        if (command == 1) executeSelected();
        else if (command == 2) pinSelectedItem(false);
        else if (command == 3) pinSelectedItem(true);
        else if (command == 4) removeSelected();
        if (IsWindow(window))
        {
            SetFocus(window);
        }
    }
    else
    {
        menuVisible_ = false;
    }
}

void JumpListWindow::keyboard(UINT key)
{
    const int count = static_cast<int>(visual_.size());
    switch (key)
    {
    case VK_ESCAPE:
        closeJumplist();
        return;
    case VK_RETURN:
    case VK_SPACE:
        click();
        return;
    case VK_DELETE:
    {
        const int index = hotItem();
        if (index == INDEX_NONE) return;
        const JumpGroupType type =
            list_.groups[static_cast<std::size_t>(visual_[static_cast<std::size_t>(index)].group)].type;
        if (type == JumpGroupType::Pinned) pinSelectedItem(false);
        else if (type != JumpGroupType::Tasks) removeSelected();
        return;
    }
    case VK_UP:
    case VK_DOWN:
    {
        int index = hotItem();
        const int step = key == VK_UP ? -1 : 1;
        if (index == INDEX_NONE)
        {
            index = 1;
        }
        else
        {
            index += step;
            if (index < 1) index = count - 1;
            if (index >= count) index = 1;
        }
        while (index > 0 && index < count &&
               !visual_[static_cast<std::size_t>(index)].selectable())
        {
            index += step;
            if (index >= count) index = 1;
            if (index < 1) index = count - 1;
        }
        setHot(index);
        return;
    }
    case VK_LEFT:
        if (hotItem() != INDEX_NONE) setHot(hotItem());
        return;
    case VK_RIGHT:
    {
        const int index = hotItem();
        if (index != INDEX_NONE && visual_[static_cast<std::size_t>(index)].pinnable)
        {
            setHot(index | INDEX_PIN);
        }
        return;
    }
    case VK_TAB:
    {
        int index = hotItem();
        if (index == INDEX_NONE || index == count - 1)
        {
            index = 1;
        }
        else
        {
            while (++index < count)
            {
                if (visual_[static_cast<std::size_t>(index)].style == VtStyle::Footer ||
                    visual_[static_cast<std::size_t>(index - 1)].style == VtStyle::Group)
                {
                    break;
                }
            }
            if (index >= count) index = count - 1;
        }
        setHot(index);
        return;
    }
    default:
        return;
    }
}

LRESULT CALLBACK JumpListWindow::windowProc(HWND window, UINT message,
                                             WPARAM wparam, LPARAM lparam) noexcept
{
    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<JumpListWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
        return DefWindowProcW(window, message, wparam, lparam);
    }
    auto* self = reinterpret_cast<JumpListWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr)
    {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    return self->handleMessage(message, wparam, lparam);
}

LRESULT JumpListWindow::handleMessage(UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paintStruct{};
        HDC dc = BeginPaint(window_, &paintStruct);
        paint(dc);
        EndPaint(window_, &paintStruct);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        if (!tracking_)
        {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window_;
            TrackMouseEvent(&track);
            tracking_ = true;
        }
        if (x == tempX_ && y == tempY_)
        {
            return 0;
        }
        tempX_ = x;
        tempY_ = y;
        int index = itemAt(x, y);
        if (index != INDEX_NONE)
        {
            const VtItem& item = visual_[static_cast<std::size_t>(index)];
            if (item.pinnable && item.style == VtStyle::Item &&
                x >= item.rect.right - pinButtonWidth_)
            {
                index |= INDEX_PIN;
            }
            hotByMouse_ = true;
        }
        else
        {
            index = INDEX_NONE;
        }
        setHot(index);
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (!menuVisible_)
        {
            setHot(INDEX_NONE);
        }
        return 0;
    case WM_LBUTTONUP:
    {
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        int index = itemAt(x, y);
        if (index != INDEX_NONE)
        {
            const VtItem& item = visual_[static_cast<std::size_t>(index)];
            if (item.pinnable && x >= item.rect.right - pinButtonWidth_)
            {
                index |= INDEX_PIN;
            }
            hotByMouse_ = true;
            hotIndex_ = index;
            click();
        }
        return 0;
    }
    case WM_RBUTTONUP:
    {
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        const int index = itemAt(x, y);
        if (index != INDEX_NONE)
        {
            hotByMouse_ = true;
            hotIndex_ = index;
            redraw();
            showMenu(x, y);
        }
        return 0;
    }
    case WM_KEYDOWN:
        keyboard(static_cast<UINT>(wparam));
        return 0;
    case LM_CM_ITEMS:
        // Commands of the attached "Linkbar" submenu belong to the bar.
        if (owner_ != nullptr)
        {
            PostMessageW(owner_, LM_CM_ITEMS, 0, lparam);
        }
        return 0;
    case WM_TIMER:
        if (wparam == TIMER_TOOLTIP_SHOW)
        {
            KillTimer(window_, TIMER_TOOLTIP_SHOW);
            updateTooltip();
            return 0;
        }
        if (wparam == TIMER_TOOLTIP_HIDE)
        {
            KillTimer(window_, TIMER_TOOLTIP_HIDE);
            tooltip_->cancel();
            return 0;
        }
        return 0;
    case WM_KILLFOCUS:
        if (!menuVisible_)
        {
            closeJumplist();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        if (owner_ != nullptr)
        {
            PostMessageW(owner_, LM_JUMPLISTCLOSED, 0, 0);
        }
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        window_ = nullptr;
        if (instance == this)
        {
            instance = nullptr;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

} // namespace

void JumpList::clear() noexcept
{
    groups.clear(); // every JumpItem releases its shell object
}

// Adds Recent/Frequent groups from IApplicationDocumentLists. Used when the
// automatic destination lists do not provide them (common on Windows 10/11).
static void appendDocumentGroups(const std::wstring& appId, JumpList& list, int maxCount)
{
    if (maxCount <= 0)
    {
        return;
    }
    ComPtr<IApplicationDocumentLists> documents;
    if (FAILED(CoCreateInstance(CLSID_ApplicationDocumentLists, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(documents.address()))) || !documents)
    {
        return;
    }
    documents->SetAppID(appId.c_str());
    const std::array<std::pair<APPDOCLISTTYPE, JumpGroupType>, 2> types{{
        {ADLT_RECENT, JumpGroupType::Recent},
        {ADLT_FREQUENT, JumpGroupType::Frequent}
    }};
    for (const auto& [type, groupType] : types)
    {
        const bool present = std::any_of(list.groups.begin(), list.groups.end(),
            [groupType](const JumpGroup& group) {
                return group.type == groupType && !group.items.empty();
            });
        if (present)
        {
            continue;
        }
        ComPtr<IShellItemArray> array;
        if (FAILED(documents->GetList(type, static_cast<UINT>(std::max(maxCount, 1)),
                                      IID_PPV_ARGS(array.address()))) || !array)
        {
            continue;
        }
        DWORD count = 0;
        if (FAILED(array->GetCount(&count)) || count == 0)
        {
            continue;
        }
        JumpGroup group;
        group.type = groupType;
        group.name = groupType == JumpGroupType::Recent
            ? l10n(L"Jumplist.Recent", L"Recent")
            : l10n(L"Jumplist.Frequent", L"Frequent");
        ComPtr<IEnumShellItems> enumerator;
        if (FAILED(array->EnumItems(enumerator.address())) || !enumerator)
        {
            continue;
        }
        for (DWORD i = 0; i < count; ++i)
        {
            IShellItem* item = nullptr;
            if (enumerator->Next(1, &item, nullptr) != S_OK || item == nullptr)
            {
                break;
            }
            JumpItem entry;
            entry.type = JumpItemType::Item;
            entry.object = ComPtr<IUnknown>(item); // adopts the reference from Next()
            PWSTR display = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display)) && display != nullptr)
            {
                entry.name = display;
                CoTaskMemFree(display);
            }
            PWSTR parsing = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsing)) &&
                parsing != nullptr)
            {
                CharUpperW(parsing);
                entry.hash = fnvHash(std::wstring_view(parsing), FNV_HASH0);
                CoTaskMemFree(parsing);
            }
            if (!entry.name.empty())
            {
                group.items.push_back(std::move(entry));
            }
        }
        if (!group.items.empty())
        {
            for (std::size_t i = 0; i < group.items.size(); ++i)
            {
                group.items[i].hidden = static_cast<int>(i) >= maxCount;
            }
            list.groups.push_back(std::move(group));
        }
    }
}

bool getAppInfoForLink(PCIDLIST_ABSOLUTE pidl, std::wstring& appId)
{
    appId.clear();
    if (pidl == nullptr)
    {
        return false;
    }

    PCITEMID_CHILD child = nullptr;
    ComPtr<IShellFolder> folder;
    if (FAILED(SHBindToParent(pidl, IID_PPV_ARGS(folder.address()), &child)))
    {
        return false;
    }
    ComPtr<IShellLinkW> link;
    if (FAILED(folder->GetUIObjectOf(nullptr, 1, &child, IID_IShellLinkW, nullptr,
                                     link.voidAddress())))
    {
        return false;
    }

    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(store.address()))))
    {
        PROPVARIANT value{};
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_AppUserModel_PreventPinning, &value)) &&
            value.vt == VT_BOOL && value.boolVal != VARIANT_FALSE)
        {
            PropVariantClear(&value);
            return false;
        }
        PropVariantClear(&value);

        const std::wstring id = propertyStoreString(store.get(), PKEY_AppUserModel_ID);
        if (!id.empty())
        {
            appId = id;
            return true;
        }
    }

    ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(item.address()))))
    {
        return false;
    }
    IApplicationResolver* resolver = applicationResolver();
    if (resolver == nullptr)
    {
        return false;
    }
    LPWSTR id = nullptr;
    if (FAILED(resolver->GetAppIDForShortcut(item.get(), &id)) || id == nullptr)
    {
        return false;
    }
    appId = id;
    CoTaskMemFree(id);
    return !appId.empty();
}

static bool queryJumplist(const std::wstring& appId)
{
    ComPtr<IDestinationList> custom = getCustomList(appId);
    UINT customCount = 0;
    const bool customOk = custom && SUCCEEDED(custom->GetCategoryCount(&customCount)) &&
                          customCount > 0;
    AutomaticList automatic(appId);
    const bool automaticValid = automatic.valid();
    const bool automaticHasList = automaticValid && automatic.hasList();
    UINT documentsCount = 0;
    ComPtr<IApplicationDocumentLists> documents;
    if (SUCCEEDED(CoCreateInstance(CLSID_ApplicationDocumentLists, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(documents.address()))) && documents)
    {
        documents->SetAppID(appId.c_str());
        for (const APPDOCLISTTYPE type : {ADLT_RECENT, ADLT_FREQUENT})
        {
            ComPtr<IShellItemArray> array;
            if (SUCCEEDED(documents->GetList(type, 1, IID_PPV_ARGS(array.address()))) && array)
            {
                DWORD count = 0;
                if (SUCCEEDED(array->GetCount(&count)))
                {
                    documentsCount += count;
                }
            }
        }
    }
    if (customOk || automaticHasList || documentsCount > 0)
    {
        return true;
    }
    return false;
}

bool hasJumplist(const std::wstring& appId)
{
    // Destination lists are slow to read; the right-click handler asks several
    // times in a row, so the answer is reused for a few seconds per application.
    constexpr ULONGLONG CACHE_LIFETIME_MS = 5000;
    struct CacheEntry
    {
        ULONGLONG timestamp = 0;
        bool value = false;
    };
    static std::unordered_map<std::wstring, CacheEntry> cache;

    if (appId.empty())
    {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    if (const auto it = cache.find(appId); it != cache.end() &&
        now - it->second.timestamp < CACHE_LIFETIME_MS)
    {
        return it->second.value;
    }
    const bool value = queryJumplist(appId);
    if (cache.size() > 64)
    {
        cache.clear();
    }
    cache[appId] = CacheEntry{now, value};
    return value;
}

bool getJumplist(const std::wstring& appId, JumpList& list, int maxCount)
{
    list.clear();
    if (appId.empty())
    {
        return false;
    }

    ComPtr<IDestinationList> custom = getCustomList(appId);
    UINT categoryCount = 0;
    if (!custom || FAILED(custom->GetCategoryCount(&categoryCount)))
    {
        categoryCount = 0;
    }

    std::vector<ComPtr<IShellItem>> ignoreItems;
    std::vector<std::uint32_t> ignoreLinks;
    AutomaticList automatic(appId);

    ComPtr<IObjectCollection> collection = automatic.getList(JL_LT_PINNED, 30);
    if (collection)
    {
        JumpGroup group;
        group.type = JumpGroupType::Pinned;
        group.name = l10n(L"Jumplist.Pinned", L"Pinned");
        addCollection(group, collection.get(), ignoreItems, ignoreLinks);
        for (const JumpItem& item : group.items)
        {
            if (!item.object)
            {
                continue;
            }
            ComPtr<IShellItem> shellItem;
            if (SUCCEEDED(item.object->QueryInterface(IID_PPV_ARGS(shellItem.address()))) &&
                shellItem)
            {
                ignoreItems.push_back(std::move(shellItem));
            }
            else
            {
                ComPtr<IShellLinkW> link;
                if (SUCCEEDED(item.object->QueryInterface(IID_PPV_ARGS(link.address()))) && link)
                {
                    const std::uint32_t hash = calculateLinkHash(link.get());
                    if (hash != 0)
                    {
                        ignoreLinks.push_back(hash);
                    }
                }
            }
        }
        list.groups.push_back(std::move(group));
    }

    const int maxWithIgnored = maxCount + static_cast<int>(ignoreItems.size() + ignoreLinks.size());
    int taskIndex = -1;
    for (UINT categoryIndex = 0; categoryIndex < categoryCount; ++categoryIndex)
    {
        AppDestCategory category{};
        if (FAILED(custom->GetCategory(categoryIndex, 1, &category)))
        {
            continue;
        }
        if (maxCount > 0 && category.type == JL_CT_CUSTOM && category.data.name != nullptr &&
            category.data.name[0] != L'\0')
        {
            wchar_t expanded[512]{};
            SHLoadIndirectString(category.data.name, expanded,
                                 static_cast<UINT>(std::size(expanded)), nullptr);
            CoTaskMemFree(const_cast<wchar_t*>(category.data.name));
            ComPtr<IObjectCollection> customCollection;
            if (SUCCEEDED(custom->EnumerateCategoryDestinations(
                    categoryIndex, IID_IObjectCollection, customCollection.voidAddress())) &&
                customCollection)
            {
                JumpGroup group;
                group.type = JumpGroupType::Custom;
                group.name = expanded;
                addCollection(group, customCollection.get(), ignoreItems, ignoreLinks);
                list.groups.push_back(std::move(group));
            }
        }
        else if (category.type == JL_CT_NORMAL)
        {
            if (maxCount > 0 && (category.data.subType == JL_LT_RECENT ||
                                 category.data.subType == JL_LT_FREQUENT))
            {
                const int listType = 3 - static_cast<int>(category.data.subType);
                ComPtr<IObjectCollection> normal = automatic.getList(
                    listType, static_cast<UINT>(std::max(maxWithIgnored, 1)));
                if (normal)
                {
                    JumpGroup group;
                    if (category.data.subType == JL_LT_FREQUENT)
                    {
                        group.type = JumpGroupType::Frequent;
                        group.name = l10n(L"Jumplist.Frequent", L"Frequent");
                    }
                    else
                    {
                        group.type = JumpGroupType::Recent;
                        group.name = l10n(L"Jumplist.Recent", L"Recent");
                    }
                    addCollection(group, normal.get(), ignoreItems, ignoreLinks);
                    list.groups.push_back(std::move(group));
                }
            }
        }
        else if (category.type == JL_CT_TASKS && taskIndex == -1)
        {
            taskIndex = static_cast<int>(categoryIndex);
        }
    }

    if (taskIndex != -1)
    {
        ComPtr<IObjectCollection> tasks;
        if (SUCCEEDED(custom->EnumerateCategoryDestinations(
                static_cast<UINT>(taskIndex), IID_IObjectCollection, tasks.voidAddress())) && tasks)
        {
            JumpGroup group;
            group.type = JumpGroupType::Tasks;
            group.name = l10n(L"Jumplist.Tasks", L"Tasks");
            addCollection(group, tasks.get(), ignoreItems, ignoreLinks);
            list.groups.push_back(std::move(group));
        }
    }

    if (categoryCount == 0 && maxCount > 0)
    {
        ComPtr<IObjectCollection> recent = automatic.getList(
            JL_LT_FREQUENT, static_cast<UINT>(std::max(maxWithIgnored, 1)));
        if (recent)
        {
            JumpGroup group;
            group.type = JumpGroupType::Recent;
            group.name = l10n(L"Jumplist.Recent", L"Recent");
            addCollection(group, recent.get(), ignoreItems, ignoreLinks);
            list.groups.push_back(std::move(group));
        }
    }

    // Recent/Frequent missing from the automatic lists are filled from the
    // document lists, so both sources complement each other.
    appendDocumentGroups(appId, list, maxCount);

    for (JumpGroup& group : list.groups)
    {
        if (group.type != JumpGroupType::Tasks && group.type != JumpGroupType::Pinned)
        {
            for (std::size_t i = 0; i < group.items.size(); ++i)
            {
                group.items[i].hidden = static_cast<int>(i) >= maxCount;
            }
        }
        group.hidden = true;
        for (const JumpItem& item : group.items)
        {
            if (!item.hidden)
            {
                group.hidden = false;
                break;
            }
        }
    }
    return true;
}

bool executeJumpItem(const JumpItem& item, HWND owner)
{
    if (!item.object)
    {
        return false;
    }
    if (item.type == JumpItemType::Item)
    {
        ComPtr<IShellItem> shellItem;
        if (FAILED(item.object->QueryInterface(IID_PPV_ARGS(shellItem.address()))) || !shellItem)
        {
            return false;
        }
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHGetIDListFromObject(shellItem.get(), &pidl)) || pidl == nullptr)
        {
            return false;
        }
        SHELLEXECUTEINFOW execute{};
        execute.cbSize = sizeof(execute);
        execute.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_LOG_USAGE;
        execute.nShow = SW_NORMAL;
        execute.lpIDList = pidl;
        ShellExecuteExW(&execute);
        CoTaskMemFree(pidl);
        return true;
    }
    if (item.type == JumpItemType::Link)
    {
        ComPtr<IContextMenu> menu;
        if (FAILED(item.object->QueryInterface(IID_PPV_ARGS(menu.address()))) || !menu)
        {
            return false;
        }
        HMENU popup = CreatePopupMenu();
        if (popup == nullptr)
        {
            return false;
        }
        if (SUCCEEDED(menu->QueryContextMenu(popup, 0, 0, 0x7B00, CMF_DEFAULTONLY)))
        {
            const UINT id = static_cast<UINT>(GetMenuDefaultItem(popup, 0, 0));
            if (id != static_cast<UINT>(-1))
            {
                CMINVOKECOMMANDINFO info{};
                info.cbSize = sizeof(info);
                info.fMask = CMIC_MASK_FLAG_LOG_USAGE;
                info.hwnd = owner;
                info.nShow = SW_NORMAL;
                info.lpVerb = MAKEINTRESOURCEA(id);
                menu->InvokeCommand(&info);
            }
        }
        DestroyMenu(popup);
        return true;
    }
    return false;
}

void removeJumpItem(const std::wstring& appId, JumpList& list, int groupIndex, int itemIndex)
{
    if (groupIndex < 0 || static_cast<std::size_t>(groupIndex) >= list.groups.size())
    {
        return;
    }
    JumpGroup& group = list.groups[static_cast<std::size_t>(groupIndex)];
    if (itemIndex < 0 || static_cast<std::size_t>(itemIndex) >= group.items.size())
    {
        return;
    }
    JumpItem& item = group.items[static_cast<std::size_t>(itemIndex)];
    bool removed = false;
    if (group.type == JumpGroupType::Frequent || group.type == JumpGroupType::Recent)
    {
        AutomaticList automatic(appId);
        removed = automatic.removeDestination(item.object.get());
    }
    else
    {
        ComPtr<IDestinationList> custom = getCustomList(appId);
        if (custom)
        {
            removed = SUCCEEDED(custom->RemoveDestination(item.object.get()));
        }
    }
    if (removed)
    {
        group.items.erase(group.items.begin() + itemIndex); // releases the object
    }
}

void pinJumpItem(const std::wstring& appId, JumpList& list, int groupIndex, int itemIndex,
                 bool pin, int pinIndex)
{
    if (groupIndex < 0 || static_cast<std::size_t>(groupIndex) >= list.groups.size())
    {
        return;
    }
    JumpGroup& group = list.groups[static_cast<std::size_t>(groupIndex)];
    if (itemIndex < 0 || static_cast<std::size_t>(itemIndex) >= group.items.size())
    {
        return;
    }
    AutomaticList automatic(appId);
    automatic.pinItem(group.items[static_cast<std::size_t>(itemIndex)].object.get(),
                      pin ? pinIndex : -2);
}

void createLinkbarTasksJumplist()
{
    if (!os().jumplistAvailable)
    {
        return;
    }
    ComGuard com;
    ComPtr<ICustomDestinationList> list;
    if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(list.address()))))
    {
        return;
    }
    UINT maximum = 0;
    IObjectArray* removed = nullptr;
    if (FAILED(list->BeginList(&maximum, IID_PPV_ARGS(&removed))))
    {
        return;
    }
    if (removed != nullptr)
    {
        removed->Release();
    }
    ComPtr<IObjectCollection> tasks;
    if (SUCCEEDED(CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(tasks.address()))))
    {
        const auto addTask = [&](const std::wstring& title, const std::wstring& arguments) {
            ComPtr<IShellLinkW> link;
            if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(link.address()))))
            {
                return;
            }
            const std::wstring executable = exeFileName();
            link->SetPath(executable.c_str());
            link->SetIconLocation(executable.c_str(), 0);
            link->SetArguments(arguments.c_str());
            ComPtr<IPropertyStore> store;
            if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(store.address()))))
            {
                PROPVARIANT value{};
                PropVariantInit(&value);
                value.vt = VT_LPWSTR;
                value.pwszVal = const_cast<wchar_t*>(title.c_str());
                store->SetValue(PKEY_Title, value);
                store->Commit();
            }
            tasks->AddObject(link.get());
        };
        addTask(l10n(L"Jumplist.NewLinkbar", L"New linkbar"), commandParam(CLK_NEW));
        addTask(l10n(L"Menu.CloseAll", L"Close all"), commandParam(CLK_CLOSEALL));
        ComPtr<IObjectArray> array;
        if (SUCCEEDED(tasks->QueryInterface(IID_PPV_ARGS(array.address()))))
        {
            list->AddUserTasks(array.get());
        }
    }
    list->CommitList();
}

void closeJumplist()
{
    if (activeJumplist)
    {
        JumpListWindow* window = activeJumplist.release();
        JumpListWindow::instance = nullptr;
        window->close();
        retiredJumplists.emplace_back(window);
    }
}

bool showJumplist(HWND owner, PCIDLIST_ABSOLUTE pidl, int maxRecent,
                  const RECT& itemRect, PanelAlign align)
{
    closeJumplist();
    retiredJumplists.clear();
    activeJumplist = std::make_unique<JumpListWindow>();
    if (!activeJumplist->create(owner, pidl, maxRecent, itemRect, align))
    {
        activeJumplist.reset();
        return false;
    }
    JumpListWindow::instance = activeJumplist.get();
    return true;
}

} // namespace lb
