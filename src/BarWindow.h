// The Linkbar main window: a layered, borderless tool window that owns all
// rendering, input handling, autohide, drag&drop and shell integration.

#pragma once

#include "AppBar.h"
#include "ComPtr.h"
#include "Constants.h"
#include "DibBitmap.h"
#include "DirectoryWatch.h"
#include "Hint.h"
#include "Items.h"
#include "PanelImage.h"

#include <oleidl.h>
#include <memory>
#include <string>
#include <vector>

namespace lb {

class BarWindow final : public IDropTarget
{
public:
    static void registerWindowClass();
    static void closeAll();

    explicit BarWindow(const std::wstring& settingsFile);
    ~BarWindow();

    BarWindow(const BarWindow&) = delete;
    BarWindow& operator=(const BarWindow&) = delete;

    [[nodiscard]] HWND handle() const noexcept { return window_; }
    int run();

    friend void openSettingsDialog(BarWindow* bar);
    friend void openRenameDialog(BarWindow* owner, PIDLIST_ABSOLUTE pidl);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IDropTarget
    STDMETHODIMP DragEnter(IDataObject* dataObject, DWORD keyState, POINTL point,
                           DWORD* effect) override;
    STDMETHODIMP DragOver(DWORD keyState, POINTL point, DWORD* effect) override;
    STDMETHODIMP DragLeave() override;
    STDMETHODIMP Drop(IDataObject* dataObject, DWORD keyState, POINTL point,
                      DWORD* effect) override;

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam) noexcept;
    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam) noexcept;

    void loadSettings();
    void saveSettings();
    void saveLinks();
    void loadItems();
    void updateBackgroundColor();
    void updateBlur();

    void updateItemSizes();
    void updateWindowSize();
    void querySizing(bool vertical, int& width, int& height);
    void querySized(int x, int y, int width, int height);
    void createCaptionFont();

    void drawBackground(DibBitmap& bitmap, const RECT& clip);
    void drawCaption(DibBitmap& bitmap, int index, bool forDrag = false);
    void drawItem(DibBitmap& bitmap, int index, bool selected, bool pressed,
                  bool drawBackground = true, bool forDrag = false);
    void drawItems(int width, int height);
    void recreateMainBitmap(int width, int height);
    void recreateButtonBitmap(int width, int height);
    void updateWindow();
    void updateWindow(const RECT& bounds);

    [[nodiscard]] int itemIndexAt(const POINT& point, int lastIndex = ITEM_NONE) const;
    [[nodiscard]] bool isItemIndex(int index) const noexcept;
    void deleteItem(int index);

    void executeItem(int index);
    void clickItem(int x, int y);
    void renameItem(int index);
    void newShortcut();
    void newSeparator();
    void openWorkDirectory();
    void removeBar();
    void toggleLock();
    void toggleSort();
    void openSettings();
    void createNewBar() const;

    void showContextMenu(const POINT& point, bool extended);
    void showSeparatorMenu(const POINT& point);
    void popupMenuCommand(int command);
    HMENU buildMenu(bool extended);

    void mouseDown(int x, int y);
    void mouseMove(int x, int y);
    void mouseUp(UINT button, int x, int y);
    void keyDown(UINT key);
    void dragLinkbar(int x, int y);
    void startItemDrag();

    void doAutoHide();
    void doAutoShow();
    void delayedAutoHide(std::uint32_t delay);
    void delayedAutoShow();
    void setHotIndex(int value);
    void setPressedIndex(int value);
    void setAlign(PanelAlign value);
    void setStayOnTop(bool value);
    void setAutoHide(bool value);
    void setIconSize(int value);
    void setLook(Look value);
    void setTransparencyMode(TransparencyMode value);
    void setUseBackgroundColor(bool value);
    void setSortAlphabetically(bool value);
    void setMonitor(int value);
    void setPreviewColor(bool background, std::uint32_t color);
    void setPreviewImage(const std::wstring& path, ImageLayout layout, int opacity);
    bool updateDistanceScales(int x, int y);
    void startAnimation(bool toHidden);

    void setDropPosition(const POINT& point);
    [[nodiscard]] DWORD proposedDropEffect() const noexcept;

    void setLayout(PanelLayout value);
    void toggleItemText(int index);
    void chooseItemTextColor(int index);
    void resetItemTextColor(int index);
    void loadItemSettings();
    void saveItemSettings();
    void refreshItems();
    RECT itemDropRect(int index, int part) const;
    void dirWatchChange(WatchAction action, const std::wstring& fileName);
    void dirWatchError(DWORD code, const std::wstring& message);

    [[nodiscard]] HBITMAP createDragImage(POINT& offset);

    void registerHotkey();
    void unregisterHotkey();
    void updateBitBucketIcons();

    [[nodiscard]] std::wstring panelName() const;

    // State ---------------------------------------------------------------
    HWND window_ = nullptr;
    HWND settingsWindow_ = nullptr;
    bool mouseTracking_ = false;
    std::wstring settingsFile_;
    std::wstring workDir_;
    PIDLIST_ABSOLUTE workDirPidl_ = nullptr;

    DibBitmap bitmapSelected_;
    DibBitmap bitmapDropPosition_;
    DibBitmap bitmapButton_;
    DibBitmap bitmapPanel_;
    ItemList items_;
    std::unique_ptr<AppBar> appBar_;
    std::unique_ptr<Tooltip> tooltip_;
    DirectoryWatch directoryWatch_;
    HFONT captionFont_ = nullptr;

    ULONG bitBucketNotify_ = 0;
    HWND previousForeground_ = nullptr;
    HMENU contextMenuTemplate_ = nullptr;

    bool created_ = false;
    bool removed_ = false;
    int hotIndex_ = ITEM_NONE;
    int pressedIndex_ = ITEM_NONE;
    POINT mouseDown_{};
    POINT mouseUp_{};
    bool mouseLeftDown_ = false;
    bool mouseDragLinkbar_ = false;
    bool mouseDragItem_ = false;
    bool draggingItem_ = false;
    RECT beforeDragBounds_{};
    bool lockHotIndex_ = false;

    bool autoHide_ = false;
    bool autoHideTransparency_ = false;
    bool autoHiden_ = false;
    bool canAutoHide_ = true;
    bool lockAutoHide_ = false;
    bool autoShowDelaySet_ = false;
    int autoShowDelay_ = 0;
    AutoShowMode autoShowMode_ = DEF_AUTOHIDE_SHOWMODE;
    RECT beforeAutoHideBound_{};
    RECT afterAutoHideBound_{};

    SIZE buttonSize_{};
    bool enableAeroGlass_ = false;
    int gripSize_ = GRIP_SIZE;
    bool tooltipShow_ = true;
    struct HotkeyInfo { std::uint16_t modifiers = 0; std::uint16_t keyCode = 0; } hotkey_;
    SIZE itemMargin_{};
    int iconSize_ = DEF_ICON_SIZE;
    bool isLightStyle_ = false;
    ItemOrder itemOrder_ = DEF_ITEM_ORDER;
    JumplistShowMode jumplistShowMode_ = DEF_JUMPLIST_SHOWMODE;
    int jumplistRecentMax_ = DEF_JUMPLIST_RECENTMAX;
    TransparencyMode transparencyMode_ = DEF_TRANSPARENCYMODE;
    Look look_ = DEF_COLORMODE;
    bool lockBar_ = false;
    bool sortAlphabetically_ = false;
    bool stayOnTop_ = DEF_STAYONTOP;
    std::uint32_t backgroundColor_ = DEF_BKGCOLOR;
    std::uint32_t systemBackgroundColor_ = 0;
    std::uint32_t textColor_ = DEF_TXTCOLOR;
    bool useBackgroundColor_ = DEF_USEBKGCOLOR;
    bool useTextColor_ = DEF_USETXTCOLOR;
    int glowSize_ = DEF_GLOWSIZE;
    PanelLayout layout_ = DEF_ITEMS_ALIGN;
    PanelAlign align_ = DEF_EDGE;
    int textWidth_ = DEF_TEXT_WIDTH;
    int textOffset_ = DEF_TEXT_OFFSET;
    TextLayout textLayout_ = DEF_TEXT_LAYOUT;
    POINT iconOffset_{};
    RECT textRect_{};
    int separatorWidth_ = DEF_SEPARATOR_WIDTH;
    SeparatorStyle separatorStyle_ = DEF_SEPARATOR_STYLE;
    int corner1GapWidth_ = 0;
    int corner2GapWidth_ = 0;
    bool animationsEnabled_ = DEF_ANIMATIONS;
    bool distanceScaling_ = DEF_DISTANCE_SCALING;
    int distanceRadius_ = DEF_DISTANCE_RADIUS;
    BackgroundMode backgroundMode_ = DEF_BACKGROUND_MODE;
    int backgroundIntensity_ = DEF_BACKGROUND_INTENSITY;
    std::wstring backgroundImage_;
    ImageLayout imageLayout_ = ImageLayout::None;
    int imageOpacity_ = DEF_IMAGE_OPACITY;
    PanelImage panelImage_;
    int imageFrame_ = 0;
    bool animating_ = false;
    bool animationToHidden_ = false;
    DWORD animationStart_ = 0;
    RECT animationFrom_{};
    RECT animationTo_{};
    int monitorNum_ = 0;
    int dragMonitorNum_ = 0;
    PanelAlign dragAlign_ = DEF_EDGE;
    int itemPopup_ = ITEM_NONE;
    int dragIndex_ = ITEM_NONE;
    std::vector<RECT> monitorsWorkArea_;

    int itemDropPosition_ = ITEM_NONE;
    int lastDropPart_ = 0;
    RECT lastDropRect_{};
    PIDLIST_ABSOLUTE dropPidl_ = nullptr;

    std::uint32_t lastModifiedHash_ = 0;
    std::uint32_t renamedOldHash_ = 0;
    bool updateTimerEnabled_ = false;
    bool dirWatchActive_ = false;

    // Drag&drop COM state
    LONG referenceCount_ = 1;
    ComPtr<IDropTargetHelper> dropHelper_;
    ComPtr<IDataObject> dragObject_;
    DWORD dragAllowedEffects_ = DROPEFFECT_NONE;
    DWORD dragKeyState_ = 0;
    bool isDragDrop_ = false;
};

} // namespace lb
