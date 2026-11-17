// Native WinAPI dialogs. The Settings dialog exposes every option of the
// original project with the same ranges and INI mapping.

#include "Dialogs.h"
#include "BarWindow.h"

#include "ColorPicker.h"
#include "Common.h"
#include "IniFile.h"
#include "Localization.h"
#include "Os.h"
#include "Shell.h"
#include "StringUtil.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <commctrl.h>
#include <shobjidl_core.h>

namespace lb {
namespace {

enum ControlId : int
{
    ID_TAB = 200,
    ID_OK = 201,
    ID_CANCEL = 202,
    ID_APPLY = 203,

    ID_SCREEN_POSITION = 210,
    ID_ITEM_ORDER = 211,
    ID_ITEMS_ALIGN = 212,
    ID_STAY_ON_TOP = 213,
    ID_LIGHT_STYLE = 214,
    ID_AERO_GLASS = 215,
    ID_COLOR_MODE = 216,
    ID_TRANSPARENCY = 217,
    ID_USE_BG = 218,
    ID_BG_EDIT = 219,
    ID_BG_BUTTON = 220,

    ID_ICON_SIZE = 230,
    ID_MARGIN_H = 231,
    ID_MARGIN_V = 232,
    ID_TEXT_LAYOUT = 233,
    ID_TEXT_WIDTH = 234,
    ID_TEXT_OFFSET = 235,
    ID_USE_TEXT_COLOR = 236,
    ID_TEXT_COLOR_EDIT = 237,
    ID_GLOW_SIZE = 238,
    ID_TOOLTIP_SHOW = 239,
    ID_SEPARATOR_WIDTH = 240,
    ID_SEPARATOR_STYLE = 241,

    ID_AUTO_HIDE = 250,
    ID_SHOW_MODE = 251,
    ID_AUTO_SHOW_DELAY = 252,
    ID_CORNER1 = 253,
    ID_CORNER2 = 254,
    ID_HOTKEY = 255,
    ID_AUTO_HIDE_TRANSPARENCY = 256,

    ID_JUMPLIST_MODE = 260,
    ID_JUMPLIST_RECENT = 261,
    ID_THEME_LABEL = 270,
    ID_TRANSPARENCY_LABEL = 271,
    ID_TEXT_COLOR_BUTTON = 272,
};

constexpr std::size_t CONTROL_SLOTS = 96;

HWND addControl(HWND parent, const wchar_t* className, const wchar_t* text,
                DWORD style, int x, int y, int width, int height, int id,
                DWORD exStyle = 0)
{
    return CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
                           x, y, width, height, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void setDefaultFont(HWND control)
{
    SendMessageW(control, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}

HWND addLabel(HWND parent, const std::wstring& text, int x, int y, int width, int height)
{
    HWND label = addControl(parent, L"Static", text.c_str(), SS_LEFT, x, y, width, height, 0);
    setDefaultFont(label);
    return label;
}

HWND addCheckBox(HWND parent, const std::wstring& text, int x, int y, int width, int height, int id)
{
    HWND control = addControl(parent, L"Button", text.c_str(),
                              BS_AUTOCHECKBOX | WS_TABSTOP, x, y, width, height, id);
    setDefaultFont(control);
    return control;
}

HWND addCombo(HWND parent, int x, int y, int width,
              const std::vector<std::wstring>& items, int id)
{
    HWND combo = addControl(parent, L"ComboBox", nullptr,
                            CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL,
                            x, y, width, 220, id);
    for (const std::wstring& item : items)
    {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    setDefaultFont(combo);
    return combo;
}

HWND addSpin(HWND parent, int x, int y, int width, int min, int max, int id)
{
    HWND edit = addControl(parent, L"Edit", L"0",
                           WS_BORDER | WS_TABSTOP | ES_CENTER | ES_NUMBER,
                           x, y, width, 22, id);
    setDefaultFont(edit);
    HWND spin = addControl(parent, UPDOWN_CLASSW, nullptr,
                           UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_SETBUDDYINT |
                           UDS_NOTHOUSANDS | WS_TABSTOP,
                           0, 0, 20, 22, 0);
    SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(edit), 0);
    SendMessageW(spin, UDM_SETRANGE32, static_cast<WPARAM>(min), static_cast<LPARAM>(max));
    setDefaultFont(spin);
    return edit;
}

int spinValue(HWND edit, int fallback = 0)
{
    wchar_t buffer[32]{};
    GetWindowTextW(edit, buffer, 32);
    try
    {
        std::size_t used = 0;
        const int value = std::stoi(buffer, &used);
        if (used == wcslen(buffer))
        {
            return value;
        }
    }
    catch (...)
    {
    }
    return fallback;
}

void setSpin(HWND edit, int value)
{
    const std::wstring text = format(L"%d", value);
    SetWindowTextW(edit, text.c_str());
}

void setCombo(HWND combo, int index)
{
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(std::clamp(index, 0, 32)), 0);
}

int comboIndex(HWND combo)
{
    const LRESULT result = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return result == CB_ERR ? 0 : static_cast<int>(result);
}

bool checked(HWND control)
{
    return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void setChecked(HWND control, bool value)
{
    SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}

std::uint32_t parseHexColor(const std::wstring& text) noexcept
{
    std::uint32_t value = 0;
    for (const wchar_t c : text)
    {
        int digit = -1;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
        else if (c == L'$' || c == L'x' || c == L'X') continue;
        else return 0;
        value = (value << 4) | static_cast<std::uint32_t>(digit);
    }
    return value;
}

// ---------------------------------------------------------------------------
// Hotkey edit control
// ---------------------------------------------------------------------------

struct HotkeyState
{
    std::uint16_t modifiers = 0;
    std::uint16_t keyCode = 0;
};

std::wstring hotkeyText(const HotkeyState& state)
{
    if (state.keyCode == 0)
    {
        return L"None";
    }
    wchar_t name[64]{};
    const UINT scan = MapVirtualKeyW(state.keyCode, MAPVK_VK_TO_VSC);
    if (GetKeyNameTextW(static_cast<LONG>(scan) << 16, name, 64) == 0)
    {
        swprintf_s(name, L"VK%02X", state.keyCode);
    }
    std::wstring result = name;
    if ((state.modifiers & MOD_WIN) != 0) result = L"Win+" + result;
    if ((state.modifiers & MOD_ALT) != 0) result = L"Alt+" + result;
    if ((state.modifiers & MOD_CONTROL) != 0) result = L"Ctrl+" + result;
    if ((state.modifiers & MOD_SHIFT) != 0) result = L"Shift+" + result;
    return result;
}

LRESULT CALLBACK hotkeyProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto* state = reinterpret_cast<HotkeyState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
        if (state != nullptr)
        {
            const UINT key = static_cast<UINT>(wparam);
            if (key == VK_TAB)
            {
                return DefWindowProcW(window, message, wparam, lparam);
            }
            if (key == VK_BACK || key == VK_DELETE)
            {
                state->keyCode = 0;
                state->modifiers = 0;
            }
            else if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU ||
                     key == VK_LWIN || key == VK_RWIN)
            {
                return 0;
            }
            else
            {
                std::uint16_t modifiers = 0;
                if (GetKeyState(VK_SHIFT) < 0) modifiers |= MOD_SHIFT;
                if (GetKeyState(VK_CONTROL) < 0) modifiers |= MOD_CONTROL;
                if (GetKeyState(VK_MENU) < 0) modifiers |= MOD_ALT;
                if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) modifiers |= MOD_WIN;
                state->modifiers = modifiers;
                state->keyCode = static_cast<std::uint16_t>(key);
            }
            InvalidateRect(window, nullptr, TRUE);
            const int id = GetDlgCtrlID(window);
            SendMessageW(GetParent(window), WM_COMMAND,
                         MAKEWPARAM(id, 0), reinterpret_cast<LPARAM>(window));
        }
        return 0;
    case WM_CHAR:
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        FrameRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOWFRAME + 1));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        const std::wstring text = state != nullptr ? hotkeyText(*state) : L"None";
        RECT textRect = rect;
        InflateRect(&textRect, -3, 0);
        DrawTextW(dc, text.c_str(), -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
        EndPaint(window, &paint);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// ---------------------------------------------------------------------------
// Settings dialog
// ---------------------------------------------------------------------------

struct SettingsState
{
    BarWindow* bar = nullptr;
    HWND window = nullptr;
    HWND tab = nullptr;
    std::array<HWND, CONTROL_SLOTS> controls{};
    std::array<HWND, 5> pages{};
    HotkeyState hotkey;
    bool canChange = false;

    [[nodiscard]] HWND control(int id) const
    {
        const std::size_t index = static_cast<std::size_t>(id - ID_TAB);
        return index < controls.size() ? controls[index] : nullptr;
    }
    void store(int id, HWND control)
    {
        controls[static_cast<std::size_t>(id - ID_TAB)] = control;
    }

    void load();
    void apply();
};

LRESULT CALLBACK pageProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    if (message == WM_COMMAND || message == WM_NOTIFY)
    {
        HWND parent = GetParent(window);
        HWND grandParent = parent != nullptr ? GetParent(parent) : nullptr;
        if (grandParent != nullptr)
        {
            return SendMessageW(grandParent, message, wparam, lparam);
        }
    }
    if (message == WM_CTLCOLORSTATIC)
    {
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK settingsProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_NCHITTEST:
    {
        LPARAM result = DefWindowProcW(window, message, wparam, lparam);
        preventSizing(result);
        return result;
    }
    case WM_NOTIFY:
        if (state != nullptr && reinterpret_cast<NMHDR*>(lparam)->code == TCN_SELCHANGE)
        {
            const int selected = TabCtrl_GetCurSel(state->tab);
            for (int i = 0; i < 5; ++i)
            {
                ShowWindow(state->pages[static_cast<std::size_t>(i)],
                           i == selected ? SW_SHOW : SW_HIDE);
            }
        }
        return 0;
    case WM_COMMAND:
        if (state != nullptr)
        {
            const int id = LOWORD(wparam);
            if (id == ID_OK || id == ID_APPLY)
            {
                state->apply();
                if (id == ID_OK)
                {
                    DestroyWindow(window);
                }
                else
                {
                    EnableWindow(state->control(ID_APPLY), FALSE);
                }
                return 0;
            }
            if (id == ID_CANCEL)
            {
                DestroyWindow(window);
                return 0;
            }
            if (id == ID_BG_BUTTON)
            {
                std::uint32_t current = 0;
                wchar_t text[32]{};
                GetWindowTextW(state->control(ID_BG_EDIT), text, 32);
                current = parseHexColor(text);
                if (const auto chosen = showColorDialog(window, current))
                {
                    SetWindowTextW(state->control(ID_BG_EDIT),
                                   format(L"%08X", *chosen).c_str());
                }
            }
            if (id == ID_TEXT_COLOR_BUTTON)
            {
                wchar_t text[32]{};
                GetWindowTextW(state->control(ID_TEXT_COLOR_EDIT), text, 32);
                const std::uint32_t current = 0xFF000000U | parseHexColor(text);
                if (const auto chosen = showColorDialog(window, current))
                {
                    SetWindowTextW(state->control(ID_TEXT_COLOR_EDIT),
                                   format(L"%06X", *chosen & 0x00FFFFFFU).c_str());
                }
            }
            if (state->canChange)
            {
                EnableWindow(state->control(ID_APPLY), TRUE);
            }
        }
        return 0;
    case WM_DESTROY:
        if (state != nullptr)
        {
            if (state->bar != nullptr && state->bar->settingsWindow_ == window)
            {
                state->bar->settingsWindow_ = nullptr;
            }
            delete state;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void SettingsState::load()
{
    BarWindow& b = *bar;
    setCombo(control(ID_SCREEN_POSITION), static_cast<int>(b.align_));
    setCombo(control(ID_ITEM_ORDER), static_cast<int>(b.itemOrder_));
    setCombo(control(ID_ITEMS_ALIGN), static_cast<int>(b.layout_));
    setChecked(control(ID_STAY_ON_TOP), b.stayOnTop_);
    setChecked(control(ID_LIGHT_STYLE), b.isLightStyle_);
    setChecked(control(ID_AERO_GLASS), b.enableAeroGlass_);
    setCombo(control(ID_COLOR_MODE), static_cast<int>(b.look_));
    setCombo(control(ID_TRANSPARENCY), static_cast<int>(b.transparencyMode_));
    setChecked(control(ID_USE_BG), b.useBackgroundColor_);
    SetWindowTextW(control(ID_BG_EDIT), format(L"%08X", b.backgroundColor_).c_str());

    setSpin(control(ID_ICON_SIZE), b.iconSize_);
    setSpin(control(ID_MARGIN_H), b.itemMargin_.cx);
    setSpin(control(ID_MARGIN_V), b.itemMargin_.cy);
    setCombo(control(ID_TEXT_LAYOUT), static_cast<int>(b.textLayout_));
    setSpin(control(ID_TEXT_WIDTH), b.textWidth_);
    setSpin(control(ID_TEXT_OFFSET), b.textOffset_);
    setChecked(control(ID_USE_TEXT_COLOR), b.useTextColor_);
    SetWindowTextW(control(ID_TEXT_COLOR_EDIT), format(L"%06X", b.textColor_).c_str());
    setSpin(control(ID_GLOW_SIZE), b.glowSize_);
    setChecked(control(ID_TOOLTIP_SHOW), b.tooltipShow_);
    setSpin(control(ID_SEPARATOR_WIDTH), b.separatorWidth_);
    setCombo(control(ID_SEPARATOR_STYLE), static_cast<int>(b.separatorStyle_));

    setChecked(control(ID_AUTO_HIDE), b.autoHide_);
    setCombo(control(ID_SHOW_MODE), static_cast<int>(b.autoShowMode_));
    setSpin(control(ID_AUTO_SHOW_DELAY), b.autoShowDelay_);
    setSpin(control(ID_CORNER1), b.corner1GapWidth_);
    setSpin(control(ID_CORNER2), b.corner2GapWidth_);
    hotkey.modifiers = b.hotkey_.modifiers;
    hotkey.keyCode = b.hotkey_.keyCode;
    setChecked(control(ID_AUTO_HIDE_TRANSPARENCY), b.autoHideTransparency_);

    setCombo(control(ID_JUMPLIST_MODE), static_cast<int>(b.jumplistShowMode_));
    setSpin(control(ID_JUMPLIST_RECENT), b.jumplistRecentMax_);

    ShowWindow(control(ID_LIGHT_STYLE), os().isSeven ? SW_SHOW : SW_HIDE);
    ShowWindow(control(ID_AERO_GLASS), os().isEightAndEightPointOne ? SW_SHOW : SW_HIDE);
    ShowWindow(control(ID_COLOR_MODE), os().isTen ? SW_SHOW : SW_HIDE);
    ShowWindow(control(ID_THEME_LABEL), os().isTen ? SW_SHOW : SW_HIDE);
    ShowWindow(control(ID_TRANSPARENCY), os().isTen ? SW_SHOW : SW_HIDE);
    ShowWindow(control(ID_TRANSPARENCY_LABEL), os().isTen ? SW_SHOW : SW_HIDE);
    InvalidateRect(control(ID_HOTKEY), nullptr, TRUE);
}

void SettingsState::apply()
{
    BarWindow& b = *bar;
    b.align_ = static_cast<PanelAlign>(std::clamp(comboIndex(control(ID_SCREEN_POSITION)), 0, 3));
    b.itemOrder_ = static_cast<ItemOrder>(std::clamp(comboIndex(control(ID_ITEM_ORDER)), 0, 1));
    b.layout_ = static_cast<PanelLayout>(std::clamp(comboIndex(control(ID_ITEMS_ALIGN)), 0, 2));
    b.stayOnTop_ = checked(control(ID_STAY_ON_TOP));
    b.isLightStyle_ = checked(control(ID_LIGHT_STYLE));
    b.enableAeroGlass_ = checked(control(ID_AERO_GLASS));
    b.look_ = static_cast<Look>(std::clamp(comboIndex(control(ID_COLOR_MODE)), 0, 3));
    b.transparencyMode_ = static_cast<TransparencyMode>(
        std::clamp(comboIndex(control(ID_TRANSPARENCY)), 0, 3));
    b.useBackgroundColor_ = checked(control(ID_USE_BG));
    wchar_t text[32]{};
    GetWindowTextW(control(ID_BG_EDIT), text, 32);
    b.backgroundColor_ = parseHexColor(text);

    b.iconSize_ = std::clamp(spinValue(control(ID_ICON_SIZE), DEF_ICON_SIZE),
                             ICON_SIZE_MIN, ICON_SIZE_MAX);
    b.itemMargin_.cx = std::clamp(spinValue(control(ID_MARGIN_H), DEF_MARGINX),
                                  MARGIN_MIN, MARGIN_MAX);
    b.itemMargin_.cy = std::clamp(spinValue(control(ID_MARGIN_V), DEF_MARGINY),
                                  MARGIN_MIN, MARGIN_MAX);
    b.textLayout_ = static_cast<TextLayout>(std::clamp(comboIndex(control(ID_TEXT_LAYOUT)), 0, 4));
    b.textWidth_ = std::clamp(spinValue(control(ID_TEXT_WIDTH), DEF_TEXT_WIDTH),
                              TEXT_WIDTH_MIN, TEXT_WIDTH_MAX);
    b.textOffset_ = std::clamp(spinValue(control(ID_TEXT_OFFSET), DEF_TEXT_OFFSET),
                               TEXT_OFFSET_MIN, TEXT_OFFSET_MAX);
    b.useTextColor_ = checked(control(ID_USE_TEXT_COLOR));
    GetWindowTextW(control(ID_TEXT_COLOR_EDIT), text, 32);
    b.textColor_ = parseHexColor(text) & 0x00FFFFFFU;
    b.glowSize_ = std::clamp(spinValue(control(ID_GLOW_SIZE), DEF_GLOWSIZE),
                             GLOW_SIZE_MIN, GLOW_SIZE_MAX);
    b.tooltipShow_ = checked(control(ID_TOOLTIP_SHOW));
    b.separatorWidth_ = std::clamp(spinValue(control(ID_SEPARATOR_WIDTH), DEF_SEPARATOR_WIDTH),
                                   SEPARATOR_WIDTH_MIN, SEPARATOR_WIDTH_MAX);
    b.separatorStyle_ = static_cast<SeparatorStyle>(
        std::clamp(comboIndex(control(ID_SEPARATOR_STYLE)), 0, 1));

    b.autoShowMode_ = static_cast<AutoShowMode>(
        std::clamp(comboIndex(control(ID_SHOW_MODE)), 0, 2));
    b.autoShowDelay_ = std::clamp(spinValue(control(ID_AUTO_SHOW_DELAY)), 0, 60000);
    b.corner1GapWidth_ = std::clamp(spinValue(control(ID_CORNER1)), 0, CORNER_GAP_WIDTH_MAX);
    b.corner2GapWidth_ = std::clamp(spinValue(control(ID_CORNER2)), 0, CORNER_GAP_WIDTH_MAX);
    b.hotkey_.modifiers = hotkey.modifiers;
    b.hotkey_.keyCode = hotkey.keyCode;
    b.autoHideTransparency_ = checked(control(ID_AUTO_HIDE_TRANSPARENCY));

    b.jumplistShowMode_ = static_cast<JumplistShowMode>(
        std::clamp(comboIndex(control(ID_JUMPLIST_MODE)), 0, 1));
    b.jumplistRecentMax_ = std::clamp(spinValue(control(ID_JUMPLIST_RECENT), DEF_JUMPLIST_RECENTMAX),
                                      JUMPLIST_RECENTMAX_MIN, JUMPLIST_RECENTMAX_MAX);

    const bool wasAutoHide = b.autoHide_;
    b.setAlign(b.align_);
    if (wasAutoHide == b.autoHide_)
    {
        b.setAutoHide(checked(control(ID_AUTO_HIDE)));
    }
    b.setStayOnTop(b.stayOnTop_);
    b.setIconSize(b.iconSize_);
    b.setUseBackgroundColor(b.useBackgroundColor_);
    b.setLook(b.look_);
    b.setTransparencyMode(b.transparencyMode_);
    b.updateBackgroundColor();
    b.updateItemSizes();
    b.recreateMainBitmap(b.bitmapPanel_.width(), b.bitmapPanel_.height());
    b.updateWindow();
    b.registerHotkey();
    b.saveSettings();
}

void centerWindow(HWND window, HWND owner)
{
    RECT rect{};
    GetWindowRect(window, &rect);
    RECT reference{};
    HMONITOR monitor = owner != nullptr && IsWindow(owner)
        ? MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST)
        : MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info))
    {
        reference = info.rcWork;
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &reference, 0);
    }
    const int x = reference.left + (rectWidth(reference) - rectWidth(rect)) / 2;
    const int y = reference.top + (rectHeight(reference) - rectHeight(rect)) / 2;
    SetWindowPos(window, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

HWND createPage(HWND tab, int index, const std::wstring& caption)
{
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(caption.c_str());
    TabCtrl_InsertItem(tab, index, &item);

    HWND page = CreateWindowExW(WS_EX_CONTROLPARENT, L"LINKBAR_SETTINGSPAGE", nullptr,
                                WS_CHILD | WS_CLIPCHILDREN | (index == 0 ? WS_VISIBLE : 0),
                                2, 28, 404, 492, tab, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    return page;
}

} // namespace

void openSettingsDialog(BarWindow* bar)
{
    if (bar == nullptr || bar->window_ == nullptr)
    {
        return;
    }
    if (bar->settingsWindow_ != nullptr && IsWindow(bar->settingsWindow_))
    {
        ShowWindow(bar->settingsWindow_, SW_SHOW);
        SetForegroundWindow(bar->settingsWindow_);
        return;
    }

    WNDCLASSW pageClass{};
    pageClass.lpfnWndProc = &pageProc;
    pageClass.hInstance = GetModuleHandleW(nullptr);
    pageClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    pageClass.lpszClassName = L"LINKBAR_SETTINGSPAGE";
    RegisterClassW(&pageClass);

    WNDCLASSW hotkeyClass{};
    hotkeyClass.lpfnWndProc = &hotkeyProc;
    hotkeyClass.hInstance = GetModuleHandleW(nullptr);
    hotkeyClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    hotkeyClass.lpszClassName = L"LINKBAR_HOTKEYEDIT";
    RegisterClassW(&hotkeyClass);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &settingsProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = L"LINKBAR_SETTINGS";
    RegisterClassW(&windowClass);

    auto* state = new SettingsState;
    state->bar = bar;

    RECT desired{0, 0, 424, 600};
    AdjustWindowRectEx(&desired, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE,
                       WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME);
    state->window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME,
        windowClass.lpszClassName, l10n(L"Menu.Properties", L"Settings...").c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, rectWidth(desired), rectHeight(desired),
        bar->window_, nullptr, windowClass.hInstance, nullptr);
    SetWindowLongPtrW(state->window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    bar->settingsWindow_ = state->window;
    centerWindow(state->window, bar->window_);

    state->tab = addControl(state->window, WC_TABCONTROLW, nullptr,
                            WS_CLIPSIBLINGS | WS_TABSTOP, 4, 4, 416, 538, ID_TAB);
    setDefaultFont(state->tab);
    state->store(ID_TAB, state->tab);
    state->pages[0] = createPage(state->tab, 0, l10n(L"Properties.PagePanel", L"Panel"));
    state->pages[1] = createPage(state->tab, 1, l10n(L"Properties.PageItems", L"Items"));
    state->pages[2] = createPage(state->tab, 2, l10n(L"Properties.PageAutoHide", L"Autohide"));
    state->pages[3] = createPage(state->tab, 3, l10n(L"Properties.PageAdditional", L"Additional"));
    state->pages[4] = createPage(state->tab, 4, l10n(L"Properties.PageAbout", L"About"));

    const int x = 12;
    const int labelWidth = 200;
    const int controlX = 214;
    const int controlWidth = 180;
    const int rowHeight = 26;

    HWND page = state->pages[0];
    state->store(ID_SCREEN_POSITION, addCombo(page, controlX, 10, controlWidth,
        {l10n(L"Properties.Left", L"Left"), l10n(L"Properties.Top", L"Top"),
         l10n(L"Properties.Right", L"Right"), l10n(L"Properties.Bottom", L"Bottom")},
        ID_SCREEN_POSITION));
    addLabel(page, l10n(L"Properties.Position", L"Position on screen:"), x, 13, labelWidth, 20);
    state->store(ID_ITEM_ORDER, addCombo(page, controlX, 10 + rowHeight, controlWidth,
        {l10n(L"Properties.LtR", L"Left to right"), l10n(L"Properties.UtD", L"Up to down")},
        ID_ITEM_ORDER));
    addLabel(page, l10n(L"Properties.Order", L"Order of shortcuts:"),
             x, 13 + rowHeight, labelWidth, 20);
    state->store(ID_ITEMS_ALIGN, addCombo(page, controlX, 10 + 2 * rowHeight, controlWidth,
        {l10n(L"Properties.Left", L"Left"), l10n(L"Properties.Center", L"Center")},
        ID_ITEMS_ALIGN));
    addLabel(page, l10n(L"Properties.ItemsAlign", L"Align (single row):"),
             x, 13 + 2 * rowHeight, labelWidth, 20);
    state->store(ID_STAY_ON_TOP, addCheckBox(page,
        l10n(L"Properties.AlwaysOnTop", L"Keep the Linkbar on top of other windows"),
        x, 10 + 3 * rowHeight, 370, 20, ID_STAY_ON_TOP));
    addLabel(page, l10n(L"Properties.ForWindows", L"Style"), x, 10 + 4 * rowHeight, 370, 20);
    state->store(ID_LIGHT_STYLE, addCheckBox(page,
        l10n(L"Properties.Style1", L"Use style like taskbar with combined buttons"),
        x, 10 + 5 * rowHeight, 370, 20, ID_LIGHT_STYLE));
    state->store(ID_AERO_GLASS, addCheckBox(page,
        l10n(L"Properties.AeroGlass", L"Enable AeroGlass support (installed separately)"),
        x, 10 + 5 * rowHeight, 370, 20, ID_AERO_GLASS));
    state->store(ID_COLOR_MODE, addCombo(page, controlX, 10 + 5 * rowHeight, controlWidth,
        {l10n(L"Properties.Light", L"Light"), l10n(L"Properties.Dark", L"Dark"),
         l10n(L"Properties.Accent", L"Accent")}, ID_COLOR_MODE));
    const std::wstring styleLabel = os().isTen
        ? l10n(L"Properties.Theme", L"Choose your color:") : std::wstring{};
    state->store(ID_THEME_LABEL, addLabel(page, styleLabel, x, 13 + 5 * rowHeight,
                                          labelWidth, 20));
    state->store(ID_TRANSPARENCY, addCombo(page, controlX, 10 + 6 * rowHeight, controlWidth,
        {l10n(L"Properties.Opaque", L"Opaque"), l10n(L"Properties.Transparent", L"Transparent"),
         l10n(L"Properties.Glass", L"Glass")}, ID_TRANSPARENCY));
    state->store(ID_TRANSPARENCY_LABEL, addLabel(page,
        l10n(L"Properties.Transparency", L"Transparency:"),
        x, 13 + 6 * rowHeight, labelWidth, 20));
    state->store(ID_USE_BG, addCheckBox(page, l10n(L"Properties.BgColor", L"Background color:"),
                                        x, 10 + 7 * rowHeight, 190, 22, ID_USE_BG));
    state->store(ID_BG_EDIT, addControl(page, L"Edit", nullptr,
        WS_BORDER | WS_TABSTOP | ES_CENTER | ES_UPPERCASE, controlX, 10 + 7 * rowHeight,
        140, 22, ID_BG_EDIT));
    setDefaultFont(state->control(ID_BG_EDIT));
    state->store(ID_BG_BUTTON, addControl(page, L"Button", L"...", WS_TABSTOP,
        controlX + 145, 10 + 7 * rowHeight, 32, 22, ID_BG_BUTTON));
    setDefaultFont(state->control(ID_BG_BUTTON));

    page = state->pages[1];
    state->store(ID_ICON_SIZE, addSpin(page, controlX, 10, controlWidth,
        ICON_SIZE_MIN, ICON_SIZE_MAX, ID_ICON_SIZE));
    addLabel(page, l10n(L"Properties.IconSize", L"Icon size:"), x, 13, labelWidth, 20);
    state->store(ID_MARGIN_H, addSpin(page, controlX, 10 + rowHeight, 85,
        MARGIN_MIN, MARGIN_MAX, ID_MARGIN_H));
    state->store(ID_MARGIN_V, addSpin(page, controlX + 95, 10 + rowHeight, 85,
        MARGIN_MIN, MARGIN_MAX, ID_MARGIN_V));
    addLabel(page, l10n(L"Properties.Margins", L"Margins (horizontal/vertical):"),
             x, 13 + rowHeight, labelWidth, 20);
    state->store(ID_TEXT_LAYOUT, addCombo(page, controlX, 10 + 2 * rowHeight, controlWidth,
        {l10n(L"Properties.Without", L"Without text"), l10n(L"Properties.Left", L"Left"),
         l10n(L"Properties.Top", L"Top"), l10n(L"Properties.Right", L"Right"),
         l10n(L"Properties.Bottom", L"Bottom")}, ID_TEXT_LAYOUT));
    addLabel(page, l10n(L"Properties.TextPos", L"Text position:"),
             x, 13 + 2 * rowHeight, labelWidth, 20);
    state->store(ID_TEXT_WIDTH, addSpin(page, controlX, 10 + 3 * rowHeight, 85,
        TEXT_WIDTH_MIN, TEXT_WIDTH_MAX, ID_TEXT_WIDTH));
    state->store(ID_TEXT_OFFSET, addSpin(page, controlX + 95, 10 + 3 * rowHeight, 85,
        TEXT_OFFSET_MIN, TEXT_OFFSET_MAX, ID_TEXT_OFFSET));
    addLabel(page, l10n(L"Properties.TextWidth", L"Text width/indent:"),
             x, 13 + 3 * rowHeight, labelWidth, 20);
    state->store(ID_USE_TEXT_COLOR, addCheckBox(page,
        l10n(L"Properties.TextColor", L"Text color:"), x, 10 + 4 * rowHeight, 190, 22,
        ID_USE_TEXT_COLOR));
    state->store(ID_TEXT_COLOR_EDIT, addControl(page, L"Edit", nullptr,
        WS_BORDER | WS_TABSTOP | ES_CENTER | ES_UPPERCASE, controlX, 10 + 4 * rowHeight,
        140, 22, ID_TEXT_COLOR_EDIT));
    setDefaultFont(state->control(ID_TEXT_COLOR_EDIT));
    state->store(ID_TEXT_COLOR_BUTTON, addControl(page, L"Button", L"...", WS_TABSTOP,
        controlX + 145, 10 + 4 * rowHeight, 32, 22, ID_TEXT_COLOR_BUTTON));
    setDefaultFont(state->control(ID_TEXT_COLOR_BUTTON));
    state->store(ID_GLOW_SIZE, addSpin(page, controlX, 10 + 5 * rowHeight, controlWidth,
        GLOW_SIZE_MIN, GLOW_SIZE_MAX, ID_GLOW_SIZE));
    addLabel(page, l10n(L"Properties.GlowSize", L"Glow size:"),
             x, 13 + 5 * rowHeight, labelWidth, 20);
    state->store(ID_TOOLTIP_SHOW, addCheckBox(page,
        l10n(L"Properties.ShowTooltips", L"Show tooltips"), x, 10 + 6 * rowHeight,
        370, 20, ID_TOOLTIP_SHOW));
    addLabel(page, l10n(L"Properties.Separators", L"Separators"), x, 10 + 7 * rowHeight,
             370, 20);
    state->store(ID_SEPARATOR_WIDTH, addSpin(page, controlX, 10 + 8 * rowHeight,
        controlWidth, SEPARATOR_WIDTH_MIN, SEPARATOR_WIDTH_MAX, ID_SEPARATOR_WIDTH));
    addLabel(page, l10n(L"Properties.Width", L"Width:"),
             x, 13 + 8 * rowHeight, labelWidth, 20);
    state->store(ID_SEPARATOR_STYLE, addCombo(page, controlX, 10 + 9 * rowHeight,
        controlWidth, {l10n(L"Properties.Line", L"Line"),
                       l10n(L"Properties.Spacer", L"Spacer")}, ID_SEPARATOR_STYLE));
    addLabel(page, l10n(L"Properties.Style", L"Style:"),
             x, 13 + 9 * rowHeight, labelWidth, 20);

    page = state->pages[2];
    state->store(ID_AUTO_HIDE, addCheckBox(page,
        l10n(L"Properties.Automatically", L"Automatically"), x, 10, 200, 20, ID_AUTO_HIDE));
    addLabel(page, l10n(L"Properties.Hide", L"Hide:"), x, 40, 200, 20);
    state->store(ID_SHOW_MODE, addCombo(page, controlX, 10 + rowHeight, controlWidth,
        {l10n(L"Properties.MouseHover", L"Mouse hover"),
         l10n(L"Properties.MouseLC", L"Mouse left-click"),
         l10n(L"Properties.MouseRC", L"Mouse right-click")}, ID_SHOW_MODE));
    addLabel(page, l10n(L"Properties.Show", L"Show:"), x, 13 + rowHeight, labelWidth, 20);
    state->store(ID_AUTO_SHOW_DELAY, addSpin(page, controlX, 10 + 2 * rowHeight,
        controlWidth, 0, 60000, ID_AUTO_SHOW_DELAY));
    addLabel(page, l10n(L"Properties.Delay", L"Delay, ms:"),
             x, 13 + 2 * rowHeight, labelWidth, 20);
    state->store(ID_CORNER1, addSpin(page, controlX, 10 + 3 * rowHeight, 85,
        0, CORNER_GAP_WIDTH_MAX, ID_CORNER1));
    state->store(ID_CORNER2, addSpin(page, controlX + 95, 10 + 3 * rowHeight, 85,
        0, CORNER_GAP_WIDTH_MAX, ID_CORNER2));
    addLabel(page, l10n(L"Properties.CornerTransWidth", L"Corners transparency width:"),
             x, 13 + 3 * rowHeight, labelWidth, 20);
    state->store(ID_HOTKEY, addControl(page, L"LINKBAR_HOTKEYEDIT", nullptr,
        WS_BORDER | WS_TABSTOP, controlX, 10 + 4 * rowHeight, controlWidth, 22, ID_HOTKEY));
    SetWindowLongPtrW(state->control(ID_HOTKEY), GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(&state->hotkey));
    addLabel(page, l10n(L"Properties.HotKey", L"Keyboard shortcut:"),
             x, 13 + 4 * rowHeight, labelWidth, 20);
    state->store(ID_AUTO_HIDE_TRANSPARENCY, addCheckBox(page,
        l10n(L"Properties.AutoHideTransparency", L"Transparent when hidden"),
        x, 10 + 5 * rowHeight, 370, 20, ID_AUTO_HIDE_TRANSPARENCY));

    page = state->pages[3];
    state->store(ID_JUMPLIST_MODE, addCombo(page, controlX, 10, controlWidth,
        {l10n(L"Properties.No", L"No"), l10n(L"Properties.MouseRC", L"Mouse right-click")},
        ID_JUMPLIST_MODE));
    addLabel(page, l10n(L"Properties.Jumplists", L"Jump Lists"), x, 13, labelWidth, 20);
    addLabel(page, l10n(L"Properties.Show", L"Show:"), x, 39, labelWidth, 20);
    state->store(ID_JUMPLIST_RECENT, addSpin(page, controlX, 10 + rowHeight,
        controlWidth, JUMPLIST_RECENTMAX_MIN, JUMPLIST_RECENTMAX_MAX, ID_JUMPLIST_RECENT));
    addLabel(page, l10n(L"Properties.JumplistRecentMaxItems",
                        L"Number of recent items to display in Jump Lists:"),
             x, 13 + rowHeight, labelWidth, 40);

    page = state->pages[4];
    addLabel(page, l10n(L"Properties.Version", L"Version:"), x, 10, 370, 20);
    addLabel(page, lb::format(l10n(L"Properties.Version", L"Version: %s"),
                              os().versionString.c_str()), x, 34, 370, 20);
    addLabel(page, l10n(L"Properties.Localizer", L"localizer: Asaq"), x, 62, 370, 20);
    addLabel(page, l10n(L"Properties.SystemInfo", L"System info:"), x, 96, 370, 20);
    addLabel(page, systemInfo(), x, 120, 370, 100);

    state->store(ID_OK, addControl(state->window, L"Button", l10n(L"Button.Ok", L"OK").c_str(),
                                   WS_TABSTOP | BS_DEFPUSHBUTTON, 200, 556, 80, 26, ID_OK));
    state->store(ID_CANCEL, addControl(state->window, L"Button",
        l10n(L"Button.Cancel", L"Cancel").c_str(), WS_TABSTOP, 288, 556, 80, 26, ID_CANCEL));
    state->store(ID_APPLY, addControl(state->window, L"Button",
        l10n(L"Button.Apply", L"Apply").c_str(), WS_TABSTOP | WS_DISABLED,
        376, 556, 80, 26, ID_APPLY));
    for (const int id : {ID_OK, ID_CANCEL, ID_APPLY})
    {
        setDefaultFont(state->control(id));
    }

    state->load();
    state->canChange = true;
    UpdateWindow(state->window);
}

namespace {

struct NewBarState
{
    HWND window = nullptr;
    HWND edit = nullptr;
    HWND allUsers = nullptr;
    HWND me = nullptr;
    HWND create = nullptr;
    std::wstring directory;
    std::wstring appDir;
    std::wstring userDir;
    std::wstring fileName;
};

LRESULT CALLBACK newBarProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto* state = reinterpret_cast<NewBarState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_NCHITTEST:
    {
        LPARAM result = DefWindowProcW(window, message, wparam, lparam);
        preventSizing(result);
        return result;
    }
    case WM_COMMAND:
        if (state != nullptr)
        {
            switch (LOWORD(wparam))
            {
            case 5: // Browse
            {
                IFileDialog* dialog = nullptr;
                if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                               IID_PPV_ARGS(&dialog))))
                {
                    DWORD options = 0;
                    dialog->GetOptions(&options);
                    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
                    if (SUCCEEDED(dialog->Show(window)))
                    {
                        IShellItem* item = nullptr;
                        if (SUCCEEDED(dialog->GetResult(&item)))
                        {
                            PWSTR path = nullptr;
                            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                            {
                                state->directory = path;
                                SetWindowTextW(state->edit, path);
                                EnableWindow(state->create, TRUE);
                                CoTaskMemFree(path);
                            }
                            item->Release();
                        }
                    }
                    dialog->Release();
                }
                return 0;
            }
            case 6: // Create
            {
                if (state->directory.empty())
                {
                    return 0;
                }
                const bool forAll = SendMessageW(state->allUsers, BM_GETCHECK, 0, 0) == BST_CHECKED;
                const std::wstring base = forAll ? state->appDir : state->userDir;
                std::error_code error;
                std::filesystem::create_directories(std::filesystem::path(base), error);
                const std::wstring file = base + state->fileName;
                IniFile ini(file);
                ini.writeString(INI_SECTION_MAIN, INI_DIR_LINKS, state->directory);
                ini.flush();
                std::wstring command = commandParam(CLK_FILE, file);
                if (!Translations::instance().locale().empty())
                {
                    command = commandParam(CLK_LANG, Translations::instance().locale()) + command;
                }
                createProcess(exeFileName(), command);
                DestroyWindow(window);
                return 0;
            }
            case IDCANCEL:
                DestroyWindow(window);
                return 0;
            default:
                break;
            }
        }
        return 0;
    case WM_DESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

struct RenameState
{
    HWND window = nullptr;
    HWND edit = nullptr;
    PIDLIST_ABSOLUTE pidl = nullptr;
};

LRESULT CALLBACK renameProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto* state = reinterpret_cast<RenameState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_NCHITTEST:
    {
        LPARAM result = DefWindowProcW(window, message, wparam, lparam);
        preventSizing(result);
        return result;
    }
    case WM_COMMAND:
        if (state != nullptr)
        {
            if (LOWORD(wparam) == 1)
            {
                wchar_t text[MAX_PATH]{};
                GetWindowTextW(state->edit, text, MAX_PATH);
                if (text[0] != L'\0' && SUCCEEDED(shRename(window, state->pidl, text)))
                {
                    DestroyWindow(window);
                }
                else
                {
                    SendMessageW(state->edit, EM_SETSEL, 0, -1);
                }
                return 0;
            }
            if (LOWORD(wparam) == IDCANCEL)
            {
                DestroyWindow(window);
                return 0;
            }
        }
        return 0;
    case WM_DESTROY:
        if (state != nullptr)
        {
            CoTaskMemFree(state->pidl);
        }
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

HWND openNewBarDialog(HWND owner)
{
    WNDCLASSW pageClass{};
    pageClass.lpfnWndProc = &pageProc;
    pageClass.hInstance = GetModuleHandleW(nullptr);
    pageClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    pageClass.lpszClassName = L"LINKBAR_SETTINGSPAGE";
    RegisterClassW(&pageClass);

    WNDCLASSW hotkeyClass{};
    hotkeyClass.lpfnWndProc = &hotkeyProc;
    hotkeyClass.hInstance = GetModuleHandleW(nullptr);
    hotkeyClass.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    hotkeyClass.lpszClassName = L"LINKBAR_HOTKEYEDIT";
    RegisterClassW(&hotkeyClass);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &newBarProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = L"LINKBAR_NEW";
    RegisterClassW(&windowClass);

    auto* state = new NewBarState;
    state->appDir = exeDirectory() + std::wstring(DN_SHARED_BARS);
    state->userDir = linkbarRoamingFolder() + std::wstring(DN_USER_BARS);
    state->fileName = format(L"%08X.lbr", static_cast<unsigned>(GetTickCount64()));
    RECT desired{0, 0, 440, 190};
    AdjustWindowRectEx(&desired, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, windowClass.lpszClassName,
        l10n(L"New.Caption", L"New linkbar").c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, rectWidth(desired), rectHeight(desired),
        owner, nullptr, windowClass.hInstance, state);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    state->window = window;
    centerWindow(window, owner);

    addLabel(window, l10n(L"New.ToWhom", L"Create linkbar for:"), 10, 10, 400, 20);
    state->allUsers = addControl(window, L"Button",
        l10n(L"New.ForAll", L"Anyone who uses this computer (all users)").c_str(),
        BS_AUTORADIOBUTTON | WS_TABSTOP, 10, 32, 410, 20, 1);
    SendMessageW(state->allUsers, BM_SETCHECK, BST_CHECKED, 0);
    state->me = addControl(window, L"Button", l10n(L"New.ForMe", L"Only for me").c_str(),
        BS_AUTORADIOBUTTON | WS_TABSTOP, 10, 56, 410, 20, 4);
    addLabel(window, l10n(L"New.Folder", L"Choose a folder:"), 10, 82, 200, 20);
    state->edit = addControl(window, L"Edit", nullptr, WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
                             10, 104, 370, 22, 0);
    setDefaultFont(state->edit);
    addControl(window, L"Button", L"...", WS_TABSTOP, 386, 104, 34, 22, 5);
    state->create = addControl(window, L"Button", l10n(L"New.Create", L"Create").c_str(),
                               WS_TABSTOP | BS_DEFPUSHBUTTON | WS_DISABLED,
                               260, 142, 80, 26, 6);
    addControl(window, L"Button", l10n(L"Button.Cancel", L"Cancel").c_str(),
               WS_TABSTOP, 348, 142, 80, 26, IDCANCEL);
    for (HWND child = GetWindow(window, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT))
    {
        setDefaultFont(child);
    }
    return window;
}

void openRenameDialog(HWND owner, PIDLIST_ABSOLUTE pidl)
{
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &renameProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = L"LINKBAR_RENAME";
    RegisterClassW(&windowClass);

    auto* state = new RenameState;
    state->pidl = ILClone(pidl);
    RECT desired{0, 0, 420, 105};
    AdjustWindowRectEx(&desired, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, windowClass.lpszClassName,
        l10n(L"Rename.Caption", L"Renaming").c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, rectWidth(desired), rectHeight(desired),
        owner, nullptr, windowClass.hInstance, state);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    state->window = window;
    centerWindow(window, owner);
    state->edit = addControl(window, L"Edit", displayName(pidl).c_str(),
                             WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 10, 390, 24, 0);
    setDefaultFont(state->edit);
    SendMessageW(state->edit, EM_LIMITTEXT, MAX_PATH, 0);
    SendMessageW(state->edit, EM_SETSEL, 0, -1);
    addControl(window, L"Button", l10n(L"Button.Ok", L"OK").c_str(),
               WS_TABSTOP | BS_DEFPUSHBUTTON, 240, 44, 79, 26, 1);
    addControl(window, L"Button", l10n(L"Button.Cancel", L"Cancel").c_str(),
               WS_TABSTOP, 328, 44, 79, 26, IDCANCEL);
    for (HWND child = GetWindow(window, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT))
    {
        setDefaultFont(child);
    }
    SetFocus(state->edit);
}

} // namespace lb
