// Linkbar - Windows desktop toolbar
// Faithful C++20 port of the original Delphi application.
//
// Central declaration of enums, constants, defaults and INI key names.
// Values of enums deliberately match the ordinal values of the Delphi
// enumerations so that existing .lbr profiles remain byte-compatible.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace lb {

// ---------------------------------------------------------------------------
// Enums (ordinal values must match the Delphi originals)
// ---------------------------------------------------------------------------

enum class ItemOrder : std::uint8_t
{
    LeftToRight = 0,
    UpToDown = 1,
};

enum class PanelAlign : std::uint8_t
{
    Left = 0,
    Top = 1,
    Right = 2,
    Bottom = 3,
};

enum class PanelLayout : std::uint8_t
{
    Left = 0,
    Center = 1,
    Right = 2,
};

// TTextLayout = (None, Left, Top, Right, Bottom)
enum class TextLayout : std::uint8_t
{
    None = 0,
    Left = 1,
    Top = 2,
    Right = 3,
    Bottom = 4,
};

enum class AutoShowMode : std::uint8_t
{
    MouseHover = 0,
    MouseClickLeft = 1,
    MouseClickRight = 2,
};

enum class JumplistShowMode : std::uint8_t
{
    Disabled = 0,
    MouseClickRight = 1,
};

// TLook = (Light, Dark, Accent, Custom)
enum class Look : std::uint8_t
{
    Light = 0,
    Dark = 1,
    Accent = 2,
    Custom = 3,
};

enum class TransparencyMode : std::uint8_t
{
    Opaque = 0,
    Transparent = 1,
    Glass = 2,
    Disabled = 3,
};

enum class SeparatorStyle : std::uint8_t
{
    Bar = 0,
    Space = 1,
};

enum class WatchAction : std::uint8_t
{
    Added,
    Removed,
    Modified,
    RenamedOld,
    RenamedNew,
};

// ---------------------------------------------------------------------------
// Command-line switches
// ---------------------------------------------------------------------------

inline constexpr std::wstring_view CLK_LANG = L"l";
inline constexpr std::wstring_view CLK_FILE = L"f";
inline constexpr std::wstring_view CLK_NEW = L"n";
inline constexpr std::wstring_view CLK_DELAY = L"d";
inline constexpr std::wstring_view CLK_CLOSEALL = L"ca";

// ---------------------------------------------------------------------------
// Application identity / folders
// ---------------------------------------------------------------------------

inline constexpr std::wstring_view APP_ID_LINKBAR = L"991E87F9787C468E8594072D75A476C7";
inline constexpr std::wstring_view APP_NAME_LINKBAR = L"Linkbar";
inline constexpr std::wstring_view URL_WEB = L"https://sourceforge.net/projects/linkbar/";
inline constexpr std::wstring_view URL_EMAIL = L"linkbar@yandex.ru";
inline constexpr std::wstring_view URL_GITHUB = L"https://github.com/ATGH15102AFMLD/Linkbar";
inline constexpr std::wstring_view URL_WINDOWS_HOTKEY =
    L"https://support.microsoft.com/en-ie/help/12445/windows-keyboard-shortcuts";

inline constexpr std::wstring_view DN_SHARED_BARS = L"Shared bars\\";
inline constexpr std::wstring_view DN_USER_BARS = L"User bars\\";
inline constexpr std::wstring_view DN_LOCALES = L"Locales\\";
inline constexpr std::wstring_view EXT_LBR = L".lbr";
inline constexpr std::wstring_view MASK_LBR = L"*.lbr";

inline constexpr std::wstring_view LINKSLIST_FILE_NAME = L"list";

// ---------------------------------------------------------------------------
// Window messages (private)
// ---------------------------------------------------------------------------

inline constexpr std::uint32_t LM_AB_CALLBACK = WM_USER + 1;
inline constexpr std::uint32_t LM_AB_FULLSCREENAPP = WM_USER + 2;
inline constexpr std::uint32_t LM_AB_TASKBARSTARTED = WM_USER + 3;
inline constexpr std::uint32_t LM_CM_ITEMS = WM_USER + 11;
inline constexpr std::uint32_t LM_CM_RENAME = WM_USER + 12;
inline constexpr std::uint32_t LM_CM_INVOKE = WM_USER + 13;
inline constexpr std::uint32_t LM_CM_DELETE = WM_USER + 14;
inline constexpr std::uint32_t LM_STOPDIRWATCH = WM_USER + 33;
inline constexpr std::uint32_t LM_DOAUTOHIDE = WM_USER + 66;
inline constexpr std::uint32_t LM_SHELLNOTIFY = WM_USER + 88;
inline constexpr std::uint32_t LM_JUMPLISTCLOSED = WM_USER + 89;

inline constexpr std::uint32_t ABM_GETAUTOHIDEBAREX = 0x0000000bU;
inline constexpr std::uint32_t ABM_SETAUTOHIDEBAREX = 0x0000000cU;
inline constexpr std::uint32_t ABN_STATECHANGE = 1;
inline constexpr std::uint32_t ABN_FULLSCREENAPP = 2;
inline constexpr std::uint32_t ABN_POSCHANGED = 4;

// Context-menu commands shared by BarWindow and the Jump List window. They are
// kept below the shell's scratch range so they are routed back as LM_CM_ITEMS.
inline constexpr UINT CMD_ITEM_TEXT = 11;
inline constexpr UINT CMD_ITEM_TEXT_COLOR = 12;
inline constexpr UINT CMD_ITEM_TEXT_COLOR_RESET = 13;

inline constexpr std::uintptr_t LB_HOTKEY_ID = 1;
inline constexpr std::uint32_t TIMER_AUTO_SHOW = 15;
inline constexpr std::uint32_t TIMER_AUTO_HIDE = 16;
inline constexpr std::uint32_t TIMER_UPDATE = 17;
inline constexpr std::uint32_t TIMER_AUTO_HIDE_DELAY = 300;

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

inline constexpr int AUTOHIDE_SIZE = 2;
// Any icon size is accepted; the upper bound only guards against absurd
// values that would allocate gigabytes of DIB memory.
inline constexpr int ICON_SIZE_MIN = 1;
inline constexpr int ICON_SIZE_MAX = 1024;
inline constexpr int MARGIN_MIN = 0;
inline constexpr int MARGIN_MAX = 64;
inline constexpr int TEXT_OFFSET_MIN = 0;
inline constexpr int TEXT_OFFSET_MAX = 64;
inline constexpr int TEXT_WIDTH_MIN = 16;
inline constexpr int TEXT_WIDTH_MAX = 512;
inline constexpr int GLOW_SIZE_MIN = 0;
inline constexpr int GLOW_SIZE_MAX = 16;
inline constexpr int JUMPLIST_RECENTMAX_MIN = 0;
inline constexpr int JUMPLIST_RECENTMAX_MAX = 60;
inline constexpr int CORNER_GAP_WIDTH_MIN = 0;
inline constexpr int CORNER_GAP_WIDTH_MAX = 512;
inline constexpr int GRIP_SIZE = 12;
inline constexpr int TOOLTIP_OFFSET = 8;
inline constexpr int DROP_INDICATOR_SIZE = 4;
inline constexpr int DROP_INDICATOR_PADDING_DIV = 8;
inline constexpr int SEPARATOR_WIDTH_MIN = 2;
inline constexpr int SEPARATOR_WIDTH_MAX = 256;
inline constexpr double PANEL_DRAG_THRESHOLD = 5.0;
inline constexpr int ITEM_NONE = -1;
inline constexpr int INDEX_NONE = -1;

// DT_* flags for every text position (maps TTextLayout to Win32 DrawText).
inline constexpr std::array<std::uint32_t, 5> TEXTALIGN{
    0U, DT_RIGHT, DT_CENTER, DT_LEFT, DT_CENTER
};

// ---------------------------------------------------------------------------
// Default settings
// ---------------------------------------------------------------------------

inline constexpr bool DEF_AUTOHIDE = false;
inline constexpr bool DEF_AUTOHIDE_TRANSPARENCY = false;
inline constexpr AutoShowMode DEF_AUTOHIDE_SHOWMODE = AutoShowMode::MouseHover;
// Shift+Ctrl+Alt+L : ((MOD_SHIFT|MOD_CONTROL|MOD_ALT) << 16) | 'L'
inline constexpr std::uint32_t DEF_AUTOHIDE_HOTKEY = 0x0007004CU;
inline constexpr std::wstring_view DEF_DIR_LINKS = L".\\links";
inline constexpr PanelLayout DEF_ITEMS_ALIGN = PanelLayout::Left;
inline constexpr PanelAlign DEF_EDGE = PanelAlign::Top;
inline constexpr bool DEF_TOOLTIP_SHOW = true;
inline constexpr int DEF_ICON_SIZE = 32;
inline constexpr bool DEF_ISLIGHT = false;
inline constexpr ItemOrder DEF_ITEM_ORDER = ItemOrder::LeftToRight;
inline constexpr bool DEF_LOCK_BAR = false;
inline constexpr int DEF_MARGINX = 4;
inline constexpr int DEF_MARGINY = 4;
inline constexpr TextLayout DEF_TEXT_LAYOUT = TextLayout::None;
inline constexpr int DEF_TEXT_OFFSET = 4;
inline constexpr int DEF_TEXT_WIDTH = 64;
inline constexpr int DEF_AUTOSHOW_DELAY = 0;
inline constexpr bool DEF_SORT_AB = false;
inline constexpr std::uint32_t DEF_BKGCOLOR = 0x00000000U;
// White reads on the dark taskbar look that is the default on Windows 10/11.
inline constexpr std::uint32_t DEF_TXTCOLOR = 0x00FFFFFFU;
inline constexpr bool DEF_USEBKGCOLOR = false;
inline constexpr bool DEF_USETXTCOLOR = true;
inline constexpr int DEF_GLOWSIZE = 12;
inline constexpr bool DEF_ENABLE_AG = false;
inline constexpr JumplistShowMode DEF_JUMPLIST_SHOWMODE = JumplistShowMode::MouseClickRight;
inline constexpr int DEF_JUMPLIST_RECENTMAX = 16;
inline constexpr bool DEF_STAYONTOP = true;
inline constexpr Look DEF_COLORMODE = Look::Accent;
inline constexpr TransparencyMode DEF_TRANSPARENCYMODE = TransparencyMode::Glass;
inline constexpr int DEF_CORNERGAP_WIDTH = 0;
inline constexpr int DEF_SEPARATOR_WIDTH = 16;
inline constexpr SeparatorStyle DEF_SEPARATOR_STYLE = SeparatorStyle::Bar;

// ---------------------------------------------------------------------------
// INI sections / fields
// ---------------------------------------------------------------------------

inline constexpr std::wstring_view INI_SECTION_MAIN = L"Main";
inline constexpr std::wstring_view INI_SECTION_DEV = L"Dev";
// Per-shortcut overrides, keyed by the file name in the working directory.
inline constexpr std::wstring_view INI_SECTION_ITEMS = L"Items";

inline constexpr std::wstring_view INI_AUTOHIDE = L"autohide";
inline constexpr std::wstring_view INI_AUTOHIDE_TRANSPARENCY = L"autohidetransparency";
inline constexpr std::wstring_view INI_AUTOHIDE_SHOWMODE = L"autohideshowmode";
inline constexpr std::wstring_view INI_AUTOHIDE_HOTKEY = L"autohidehotkey";
inline constexpr std::wstring_view INI_DIR_LINKS = L"dirlinks";
inline constexpr std::wstring_view INI_ITEMS_ALIGN = L"itemsalign";
inline constexpr std::wstring_view INI_EDGE = L"Edge";
inline constexpr std::wstring_view INI_TOOLTIP_SHOW = L"tooltipshow";
inline constexpr std::wstring_view INI_ICON_SIZE = L"iconsize";
inline constexpr std::wstring_view INI_ISLIGHT = L"usestylecombined";
inline constexpr std::wstring_view INI_ITEM_ORDER = L"itemorder";
inline constexpr std::wstring_view INI_LOCK_BAR = L"lockbar";
inline constexpr std::wstring_view INI_MARGINX = L"marginx";
inline constexpr std::wstring_view INI_MARGINY = L"marginy";
inline constexpr std::wstring_view INI_MONITORNUM = L"monitornum";
inline constexpr std::wstring_view INI_TEXT_LAYOUT = L"textlayout";
inline constexpr std::wstring_view INI_TEXT_OFFSET = L"textoffset";
inline constexpr std::wstring_view INI_TEXT_WIDTH = L"textwidth";
inline constexpr std::wstring_view INI_AUTOSHOW_DELAY = L"autoshowdelay";
inline constexpr std::wstring_view INI_SORT_AB = L"sortab";
inline constexpr std::wstring_view INI_USEBKGCOLOR = L"usebgcolor";
inline constexpr std::wstring_view INI_BKGCOLOR = L"bgcolor";
inline constexpr std::wstring_view INI_USETXTCOLOR = L"usetxtcolor";
inline constexpr std::wstring_view INI_TXTCOLOR = L"txtcolor";
inline constexpr std::wstring_view INI_GLOWSIZE = L"glowsize";
inline constexpr std::wstring_view INI_JUMPLIST_SHOWMODE = L"jumplistshowmode";
inline constexpr std::wstring_view INI_JUMPLIST_RECENTMAX = L"jumplistrecentmaxitems";
inline constexpr std::wstring_view INI_STAYONTOP = L"stayontop";
inline constexpr std::wstring_view INI_COLORMODE = L"colormode";
inline constexpr std::wstring_view INI_TRANSPARENCYMODE = L"look";
inline constexpr std::wstring_view INI_ENABLE_AG = L"enableaeroglass";
inline constexpr std::wstring_view INI_CORNER1GAP_WIDTH = L"corner1gapwidth";
inline constexpr std::wstring_view INI_CORNER2GAP_WIDTH = L"corner2gapwidth";
inline constexpr std::wstring_view INI_SEPARATOR_WIDTH = L"separatorwidth";
inline constexpr std::wstring_view INI_SEPARATOR_STYLE = L"separatorstyle";

// ---------------------------------------------------------------------------
// Globals shared by the different panels of one process
// ---------------------------------------------------------------------------

struct Globals
{
    PanelLayout layout = DEF_ITEMS_ALIGN;
    Look look = DEF_COLORMODE;
    bool aeroGlassEnabled = DEF_ENABLE_AG;
};

Globals& globals() noexcept;

} // namespace lb
