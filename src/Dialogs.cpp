// Native WinAPI dialogs. The Settings window exposes every option of the
// original project with the same ranges and INI mapping.
//
// Coordinates in this file are in 96-DPI units. The Layout class scales them to
// the DPI of the window and re-applies that scaling on WM_DPICHANGED.

#include "Dialogs.h"
#include "BarWindow.h"

#include "ColorPicker.h"
#include "Common.h"
#include "DarkTheme.h"
#include "IniFile.h"
#include "Localization.h"
#include "Os.h"
#include "Shell.h"
#include "StringUtil.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <memory>
#include <optional>
#include <vector>
#include <commctrl.h>
#include <lmcons.h>
#include <shlobj.h>
#include <shobjidl_core.h>

namespace lb {
namespace {

enum ControlId : int
{
    ID_TAB = 200,
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

// Opening a modal dialog from inside the button's SendMessage chain nests a
// modal loop in someone else's message dispatch. Post it instead, so the
// picker starts from a normal top-level message.
constexpr UINT LM_PICK_COLOR = WM_APP + 20;

// New linkbar window
constexpr int NEW_ID_ALL_USERS = 101;
constexpr int NEW_ID_ME = 102;
constexpr int NEW_ID_BROWSE = 103;
constexpr int NEW_ID_CREATE = 104;

constexpr std::wstring_view CLASS_PAGE = L"LINKBAR_SETTINGSPAGE";
constexpr std::wstring_view CLASS_HOTKEY = L"LINKBAR_HOTKEYEDIT";
constexpr std::wstring_view CLASS_SETTINGS = L"LINKBAR_SETTINGS";
constexpr std::wstring_view CLASS_NEW = L"LINKBAR_NEW";
constexpr std::wstring_view CLASS_RENAME = L"LINKBAR_RENAME";

constexpr std::array<wchar_t, 9> INVALID_NAME_CHARS{L'\\', L'/', L':', L'*', L'?', L'"', L'<', L'>', L'|'};

// Windows 10 1903 and later: the drop-down list shows at least this many rows
// while the combo box itself stays one line high.
constexpr int COMBO_MIN_VISIBLE = 16;
constexpr unsigned COMBO_NEW_BUILD = 18362;

// Open dialogs, so IsDialogMessage can route Tab/Enter/Esc into them.
std::vector<HWND> g_dialogs;

bool isDialogMessage(MSG& message)
{
    // IsDialogMessage must only see keyboard messages: it can swallow mouse
    // clicks and leave a button focused without ever sending BN_CLICKED.
    if (message.message < WM_KEYFIRST || message.message > WM_KEYLAST)
    {
        return false;
    }
    for (HWND dialog : g_dialogs)
    {
        if (IsDialogMessageW(dialog, &message))
        {
            return true;
        }
    }
    return false;
}

void registerClass(std::wstring_view name, WNDPROC procedure, HBRUSH background, LPCWSTR cursor)
{
    WNDCLASSW existing{};
    if (GetClassInfoW(GetModuleHandleW(nullptr), name.data(), &existing) != 0)
    {
        return;
    }
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, cursor);
    windowClass.hbrBackground = background;
    windowClass.lpszClassName = name.data();
    RegisterClassW(&windowClass);
}

HBRUSH buttonFaceBrush() noexcept
{
    return reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
}

// Window size (outer) for a client size, using the DPI-aware API when present.
RECT outerRect(int clientWidth, int clientHeight, DWORD style, DWORD exStyle, unsigned dpi)
{
    using AdjustFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    RECT rect{0, 0, clientWidth, clientHeight};
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
    {
        if (auto adjust = reinterpret_cast<AdjustFn>(GetProcAddress(user32, "AdjustWindowRectExForDpi")))
        {
            adjust(&rect, style, FALSE, exStyle, dpi);
            return rect;
        }
    }
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    return rect;
}

HFONT createUiFont(unsigned dpi)
{
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    LOGFONTW font = metrics.lfMessageFont;
    font.lfHeight = MulDiv(font.lfHeight, static_cast<int>(dpi), systemDpi());
    return CreateFontIndirectW(&font);
}

// Child controls that follow the DPI of their dialog.
class Layout final
{
public:
    explicit Layout(unsigned dpi) : dpi_(dpi), font_(createUiFont(dpi)) {}
    ~Layout()
    {
        DeleteObject(font_);
    }
    Layout(const Layout&) = delete;
    Layout& operator=(const Layout&) = delete;

    [[nodiscard]] int px(int value) const noexcept
    {
        return MulDiv(value, static_cast<int>(dpi_), 96);
    }

    [[nodiscard]] unsigned dpi() const noexcept { return dpi_; }

    HWND add(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style,
             const RECT& base, int id = 0, DWORD exStyle = 0, bool tracked = true)
    {
        HWND window = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style,
                                      px(base.left), px(base.top), px(rectWidth(base)),
                                      px(rectHeight(base)), parent,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      GetModuleHandleW(nullptr), nullptr);
        if (window != nullptr)
        {
            SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
            if (tracked)
            {
                placed_.push_back(Placed{window, base});
            }
        }
        return window;
    }

    void setDpi(unsigned dpi)
    {
        dpi_ = dpi;
        HFONT previous = font_;
        font_ = createUiFont(dpi_);
        for (const Placed& item : placed_)
        {
            SendMessageW(item.window, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            MoveWindow(item.window, px(item.base.left), px(item.base.top),
                       px(rectWidth(item.base)), px(rectHeight(item.base)), TRUE);
        }
        DeleteObject(previous);
    }

    [[nodiscard]] HFONT font() const noexcept { return font_; }

private:
    struct Placed
    {
        HWND window = nullptr;
        RECT base{};
    };

    unsigned dpi_ = 96;
    HFONT font_ = nullptr;
    std::vector<Placed> placed_;
};

RECT box(int x, int y, int width, int height) noexcept
{
    return makeRect(x, y, width, height);
}

HWND addLabel(Layout& layout, HWND parent, const std::wstring& text, const RECT& base)
{
    return layout.add(parent, L"Static", text.c_str(), SS_LEFT, base);
}

HWND addCheckBox(Layout& layout, HWND parent, const std::wstring& text, const RECT& base, int id)
{
    return layout.add(parent, L"Button", text.c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, base, id);
}

HWND addCombo(Layout& layout, HWND parent, int x, int y, int width,
              const std::vector<std::wstring>& items, int id)
{
    const bool shortClosedBox = os().build >= COMBO_NEW_BUILD;
    const int height = shortClosedBox ? 22 : 220;
    HWND combo = layout.add(parent, L"ComboBox", nullptr,
                            CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL,
                            box(x, y, width, height), id);
    if (shortClosedBox)
    {
        SendMessageW(combo, CB_SETMINVISIBLE, COMBO_MIN_VISIBLE, 0);
    }
    for (const std::wstring& item : items)
    {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
    return combo;
}

HWND addSpin(Layout& layout, HWND parent, const RECT& base, int min, int max, int id)
{
    HWND edit = layout.add(parent, L"Edit", L"0", WS_BORDER | WS_TABSTOP | ES_CENTER | ES_NUMBER,
                           base, id);
    // The up-down control follows its buddy, so it is not tracked for DPI moves.
    // No UDS_SETBUDDYINT: the edit is the single source of truth and accepts
    // any typed number; the arrows are handled through UDN_DELTAPOS.
    HWND spin = layout.add(parent, UPDOWN_CLASSW, nullptr,
                           UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS | WS_TABSTOP,
                           box(0, 0, 20, 22), 0, 0, false);
    SendMessageW(spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(edit), 0);
    SendMessageW(spin, UDM_SETRANGE32, static_cast<WPARAM>(min), static_cast<LPARAM>(max));
    return edit;
}

int spinValue(HWND edit, int fallback, int min, int max)
{
    wchar_t buffer[32]{};
    GetWindowTextW(edit, buffer, static_cast<int>(std::size(buffer)));
    const auto value = parseInteger(buffer, 10);
    if (!value || *value < min || *value > max)
    {
        return std::clamp(fallback, min, max);
    }
    return static_cast<int>(*value);
}

void setSpin(HWND edit, int value)
{
    SetWindowTextW(edit, format(L"%d", value).c_str());
}

void setCombo(HWND combo, int index)
{
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(std::clamp(index, 0, 32)), 0);
}

int comboIndex(HWND combo, int min, int max)
{
    const LRESULT result = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return result == CB_ERR ? min : std::clamp(static_cast<int>(result), min, max);
}

bool isChecked(HWND control)
{
    return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void setChecked(HWND control, bool value)
{
    SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}

std::optional<Color> editHex(HWND edit, Color fallback)
{
    wchar_t text[32]{};
    GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
    const auto value = parseHexColor(text);
    return value ? value : std::optional<Color>(fallback);
}

// Only hex digits may be typed into a color field; pasted text is sanitized
// in WM_COMMAND/EN_CHANGE below.
LRESULT CALLBACK hexEditProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto* original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CHAR)
    {
        const wchar_t c = static_cast<wchar_t>(wparam);
        if (!isHexDigit(c) && c != L'\b' && c != 0x7F)
        {
            return 0;
        }
    }
    return CallWindowProcW(original, window, message, wparam, lparam);
}

void makeHexEdit(HWND edit, int maxDigits)
{
    SendMessageW(edit, EM_LIMITTEXT, maxDigits, 0);
    auto* original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hexEditProc)));
    SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(original));
}

void sanitizeHexEdit(HWND edit, int maxDigits)
{
    wchar_t text[32]{};
    GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
    std::wstring digits;
    for (const wchar_t* p = text; *p != L'\0' && digits.size() < static_cast<std::size_t>(maxDigits); ++p)
    {
        if (isHexDigit(*p))
        {
            digits.push_back(static_cast<wchar_t>(std::towupper(*p)));
        }
    }
    if (digits != text)
    {
        SetWindowTextW(edit, digits.c_str());
        SendMessageW(edit, EM_SETSEL, digits.size(), digits.size());
    }
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
    return hotkeyName(state.modifiers, state.keyCode);
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
// Settings window
// ---------------------------------------------------------------------------

struct SettingsState
{
    BarWindow* bar = nullptr;
    HWND window = nullptr;
    HWND tab = nullptr;
    Layout layout;
    std::array<HWND, CONTROL_SLOTS> controls{};
    std::array<HWND, 4> pages{};
    HotkeyState hotkey;
    bool canChange = false;

    SettingsState(BarWindow* owner, unsigned dpi) : bar(owner), layout(dpi) {}

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
    case LM_PICK_COLOR:
        if (state != nullptr)
        {
            const bool background = wparam == ID_BG_BUTTON;
            const int editId = background ? ID_BG_EDIT : ID_TEXT_COLOR_EDIT;
            const HWND edit = state->control(editId);
            const HWND apply = state->control(ID_APPLY);
            const Color fallback = background ? state->bar->backgroundColor_ : state->bar->textColor_;
            const Color current = background
                ? editHex(edit, fallback).value_or(fallback)
                : 0xFF000000U | editHex(edit, fallback).value_or(fallback);
            Color chosen = current;
            if (pickColor(window, current, chosen) && IsWindow(edit))
            {
                SetWindowTextW(edit, toHexColor(background ? chosen : chosen & 0x00FFFFFFU,
                                                background ? 8 : 6).c_str());
                if (IsWindow(apply))
                {
                    EnableWindow(apply, TRUE);
                }
            }
        }
        return 0;
    case WM_DPICHANGED:
        if (state != nullptr)
        {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            state->layout.setDpi(HIWORD(wparam));
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         rectWidth(*suggested), rectHeight(*suggested),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_NOTIFY:
    {
        auto* header = state != nullptr ? reinterpret_cast<NMHDR*>(lparam) : nullptr;
        if (header == nullptr)
        {
            return 0;
        }
        if (header->code == TCN_SELCHANGE)
        {
            const int selected = TabCtrl_GetCurSel(state->tab);
            for (int i = 0; i < 4; ++i)
            {
                ShowWindow(state->pages[static_cast<std::size_t>(i)],
                           i == selected ? SW_SHOW : SW_HIDE);
            }
            return 0;
        }
        if (header->code == UDN_DELTAPOS)
        {
            // Arrows change the edit text ourselves, so typing stays untouched.
            auto* change = reinterpret_cast<NMUPDOWN*>(lparam);
            HWND buddy = reinterpret_cast<HWND>(SendMessageW(header->hwndFrom, UDM_GETBUDDY, 0, 0));
            if (buddy != nullptr)
            {
                wchar_t buffer[32]{};
                GetWindowTextW(buddy, buffer, static_cast<int>(std::size(buffer)));
                const long long current = parseInteger(buffer, 10).value_or(0);
                int minimum = 0;
                int maximum = 0;
                SendMessageW(header->hwndFrom, UDM_GETRANGE32,
                             reinterpret_cast<WPARAM>(&minimum),
                             reinterpret_cast<LPARAM>(&maximum));
                const long long next = std::clamp<long long>(current + change->iDelta,
                                                             minimum, maximum);
                SetWindowTextW(buddy, format(L"%I64d", next).c_str());
            }
            return TRUE; // the up-down itself must not change anything
        }
        return 0;
    }
    case WM_COMMAND:
        if (state != nullptr)
        {
            const int id = LOWORD(wparam);
            if (id == IDOK || id == ID_APPLY)
            {
                state->apply();
                if (id == IDOK)
                {
                    DestroyWindow(window);
                }
                else
                {
                    EnableWindow(state->control(ID_APPLY), FALSE);
                }
                return 0;
            }
            if (id == IDCANCEL)
            {
                DestroyWindow(window);
                return 0;
            }
            if (id == ID_BG_BUTTON || id == ID_TEXT_COLOR_BUTTON)
            {
                PostMessageW(window, LM_PICK_COLOR, static_cast<WPARAM>(id), 0);
                return 0;
            }
            if (state->canChange)
            {
                if (id == ID_BG_EDIT)
                {
                    sanitizeHexEdit(state->control(ID_BG_EDIT), 8);
                }
                else if (id == ID_TEXT_COLOR_EDIT)
                {
                    sanitizeHexEdit(state->control(ID_TEXT_COLOR_EDIT), 6);
                }
                EnableWindow(state->control(ID_APPLY), TRUE);
            }
        }
        return 0;
    case WM_DESTROY:
        if (state != nullptr)
        {
            std::unique_ptr<SettingsState> owned(state);
            if (owned->bar != nullptr && owned->bar->settingsWindow_ == window)
            {
                owned->bar->settingsWindow_ = nullptr;
                owned->bar->lockAutoHide_ = false;
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        unregisterDialogWindow(window);
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
    SetWindowTextW(control(ID_BG_EDIT), toHexColor(b.backgroundColor_, 8).c_str());

    setSpin(control(ID_ICON_SIZE), b.iconSize_);
    setSpin(control(ID_MARGIN_H), b.itemMargin_.cx);
    setSpin(control(ID_MARGIN_V), b.itemMargin_.cy);
    setCombo(control(ID_TEXT_LAYOUT), static_cast<int>(b.textLayout_));
    setSpin(control(ID_TEXT_WIDTH), b.textWidth_);
    setSpin(control(ID_TEXT_OFFSET), b.textOffset_);
    setChecked(control(ID_USE_TEXT_COLOR), b.useTextColor_);
    SetWindowTextW(control(ID_TEXT_COLOR_EDIT), toHexColor(b.textColor_, 6).c_str());
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
    b.align_ = static_cast<PanelAlign>(comboIndex(control(ID_SCREEN_POSITION), 0, 3));
    b.itemOrder_ = static_cast<ItemOrder>(comboIndex(control(ID_ITEM_ORDER), 0, 1));
    b.setLayout(static_cast<PanelLayout>(comboIndex(control(ID_ITEMS_ALIGN), 0, 2)));
    b.stayOnTop_ = isChecked(control(ID_STAY_ON_TOP));
    b.isLightStyle_ = isChecked(control(ID_LIGHT_STYLE));
    b.enableAeroGlass_ = isChecked(control(ID_AERO_GLASS));
    b.look_ = static_cast<Look>(comboIndex(control(ID_COLOR_MODE), 0, 3));
    b.transparencyMode_ = static_cast<TransparencyMode>(comboIndex(control(ID_TRANSPARENCY), 0, 3));
    // Values with a guarded setter are not assigned here directly: the setter
    // compares against the current member and would skip the real update.
    b.backgroundColor_ = editHex(control(ID_BG_EDIT), b.backgroundColor_)
                             .value_or(b.backgroundColor_);
    const int iconSize = spinValue(control(ID_ICON_SIZE), DEF_ICON_SIZE,
                                   ICON_SIZE_MIN, ICON_SIZE_MAX);
    b.itemMargin_.cx = spinValue(control(ID_MARGIN_H), DEF_MARGINX, MARGIN_MIN, MARGIN_MAX);
    b.itemMargin_.cy = spinValue(control(ID_MARGIN_V), DEF_MARGINY, MARGIN_MIN, MARGIN_MAX);
    b.textLayout_ = static_cast<TextLayout>(comboIndex(control(ID_TEXT_LAYOUT), 0, 4));
    b.textWidth_ = spinValue(control(ID_TEXT_WIDTH), DEF_TEXT_WIDTH, TEXT_WIDTH_MIN, TEXT_WIDTH_MAX);
    b.textOffset_ = spinValue(control(ID_TEXT_OFFSET), DEF_TEXT_OFFSET, TEXT_OFFSET_MIN, TEXT_OFFSET_MAX);
    b.useTextColor_ = isChecked(control(ID_USE_TEXT_COLOR));
    b.textColor_ = editHex(control(ID_TEXT_COLOR_EDIT), b.textColor_)
                       .value_or(b.textColor_) & 0x00FFFFFFU;
    b.glowSize_ = spinValue(control(ID_GLOW_SIZE), DEF_GLOWSIZE, GLOW_SIZE_MIN, GLOW_SIZE_MAX);
    b.tooltipShow_ = isChecked(control(ID_TOOLTIP_SHOW));
    b.separatorWidth_ = spinValue(control(ID_SEPARATOR_WIDTH), DEF_SEPARATOR_WIDTH,
                                  SEPARATOR_WIDTH_MIN, SEPARATOR_WIDTH_MAX);
    b.separatorStyle_ = static_cast<SeparatorStyle>(comboIndex(control(ID_SEPARATOR_STYLE), 0, 1));

    b.autoShowMode_ = static_cast<AutoShowMode>(comboIndex(control(ID_SHOW_MODE), 0, 2));
    b.autoShowDelay_ = spinValue(control(ID_AUTO_SHOW_DELAY), DEF_AUTOSHOW_DELAY, 0, 60000);
    b.corner1GapWidth_ = spinValue(control(ID_CORNER1), 0, 0, CORNER_GAP_WIDTH_MAX);
    b.corner2GapWidth_ = spinValue(control(ID_CORNER2), 0, 0, CORNER_GAP_WIDTH_MAX);
    b.hotkey_.modifiers = hotkey.modifiers;
    b.hotkey_.keyCode = hotkey.keyCode;
    b.autoHideTransparency_ = isChecked(control(ID_AUTO_HIDE_TRANSPARENCY));

    b.jumplistShowMode_ = static_cast<JumplistShowMode>(comboIndex(control(ID_JUMPLIST_MODE), 0, 1));
    b.jumplistRecentMax_ = spinValue(control(ID_JUMPLIST_RECENT), DEF_JUMPLIST_RECENTMAX,
                                     JUMPLIST_RECENTMAX_MIN, JUMPLIST_RECENTMAX_MAX);

    b.setAlign(b.align_);
    b.setAutoHide(isChecked(control(ID_AUTO_HIDE)));
    b.setStayOnTop(b.stayOnTop_);
    b.setIconSize(iconSize);
    b.setUseBackgroundColor(isChecked(control(ID_USE_BG)));
    b.setLook(b.look_);
    b.setTransparencyMode(b.transparencyMode_);
    b.updateBackgroundColor();
    b.updateItemSizes();
    // The button size changed: the AppBar has to reserve the new height/width
    // and rebuild the layered bitmap for it.
    if (b.appBar_)
    {
        b.appBar_->appBarPosChanged();
    }
    else
    {
        b.recreateMainBitmap(b.bitmapPanel_.width(), b.bitmapPanel_.height());
        b.updateWindow();
    }
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

HWND addPage(SettingsState& state, int index, const std::wstring& caption)
{
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(caption.c_str());
    TabCtrl_InsertItem(state.tab, index, &item);

    HWND page = state.layout.add(state.tab, CLASS_PAGE.data(), nullptr,
                                 WS_CLIPCHILDREN, box(2, 28, 428, 508), 0, WS_EX_CONTROLPARENT);
    if (index != 0)
    {
        ShowWindow(page, SW_HIDE);
    }
    return page;
}

void buildPanelPage(SettingsState& state, HWND page)
{
    Layout& layout = state.layout;
    const int x = 12;
    const int labelWidth = 200;
    const int controlX = 214;
    const int controlWidth = 180;
    const int rowHeight = 26;

    addLabel(layout, page, l10n(L"Properties.Position", L"Position on screen:"), box(x, 13, labelWidth, 20));
    state.store(ID_SCREEN_POSITION, addCombo(state.layout, page, controlX, 10, controlWidth,
        {l10n(L"Properties.Left", L"Left"), l10n(L"Properties.Top", L"Top"),
         l10n(L"Properties.Right", L"Right"), l10n(L"Properties.Bottom", L"Bottom")},
        ID_SCREEN_POSITION));
    state.store(ID_ITEM_ORDER, addCombo(state.layout, page, controlX, 10 + rowHeight, controlWidth,
        {l10n(L"Properties.LtR", L"Left to right"), l10n(L"Properties.UtD", L"Up to down")},
        ID_ITEM_ORDER));
    addLabel(layout, page, l10n(L"Properties.Order", L"Order of shortcuts:"),
             box(x, 13 + rowHeight, labelWidth, 20));
    state.store(ID_ITEMS_ALIGN, addCombo(state.layout, page, controlX, 10 + 2 * rowHeight, controlWidth,
        {l10n(L"Properties.Left", L"Left"), l10n(L"Properties.Center", L"Center")},
        ID_ITEMS_ALIGN));
    addLabel(layout, page, l10n(L"Properties.ItemsAlign", L"Align (single row):"),
             box(x, 13 + 2 * rowHeight, labelWidth, 20));
    state.store(ID_STAY_ON_TOP, addCheckBox(layout, page,
        l10n(L"Properties.AlwaysOnTop", L"Keep the Linkbar on top of other windows"),
        box(x, 10 + 3 * rowHeight, 370, 20), ID_STAY_ON_TOP));
    addLabel(layout, page, l10n(L"Properties.ForWindows", L"Style"), box(x, 10 + 4 * rowHeight, 370, 20));
    state.store(ID_LIGHT_STYLE, addCheckBox(layout, page,
        l10n(L"Properties.Style1", L"Use style like taskbar with combined buttons"),
        box(x, 10 + 5 * rowHeight, 370, 20), ID_LIGHT_STYLE));
    state.store(ID_AERO_GLASS, addCheckBox(layout, page,
        l10n(L"Properties.AeroGlass", L"Enable AeroGlass support (installed separately)"),
        box(x, 10 + 5 * rowHeight, 370, 20), ID_AERO_GLASS));
    state.store(ID_COLOR_MODE, addCombo(state.layout, page, controlX, 10 + 5 * rowHeight, controlWidth,
        {l10n(L"Properties.Light", L"Light"), l10n(L"Properties.Dark", L"Dark"),
         l10n(L"Properties.Accent", L"Accent")}, ID_COLOR_MODE));
    const std::wstring styleLabel = os().isTen
        ? l10n(L"Properties.Theme", L"Choose your color:") : std::wstring{};
    state.store(ID_THEME_LABEL, addLabel(layout, page, styleLabel,
                                         box(x, 13 + 5 * rowHeight, labelWidth, 20)));
    state.store(ID_TRANSPARENCY, addCombo(state.layout, page, controlX, 10 + 6 * rowHeight, controlWidth,
        {l10n(L"Properties.Opaque", L"Opaque"), l10n(L"Properties.Transparent", L"Transparent"),
         l10n(L"Properties.Glass", L"Glass")}, ID_TRANSPARENCY));
    state.store(ID_TRANSPARENCY_LABEL, addLabel(layout, page,
        l10n(L"Properties.Transparency", L"Transparency:"), box(x, 13 + 6 * rowHeight, labelWidth, 20)));
    state.store(ID_USE_BG, addCheckBox(layout, page, l10n(L"Properties.BgColor", L"Background color:"),
                                       box(x, 10 + 7 * rowHeight, 190, 22), ID_USE_BG));
    HWND backgroundEdit = layout.add(page, L"Edit", nullptr,
        WS_BORDER | WS_TABSTOP | ES_CENTER | ES_UPPERCASE,
        box(controlX, 10 + 7 * rowHeight, 140, 22), ID_BG_EDIT);
    state.store(ID_BG_EDIT, backgroundEdit);
    makeHexEdit(backgroundEdit, 8);
    state.store(ID_BG_BUTTON, layout.add(page, L"Button", L"...", WS_TABSTOP,
        box(controlX + 145, 10 + 7 * rowHeight, 32, 22), ID_BG_BUTTON));
}

void buildItemsPage(SettingsState& state, HWND page)
{
    Layout& layout = state.layout;
    const int x = 12;
    const int labelWidth = 200;
    const int controlX = 214;
    const int controlWidth = 180;
    const int rowHeight = 26;

    state.store(ID_ICON_SIZE, addSpin(layout, page, box(controlX, 10, controlWidth, 22),
        ICON_SIZE_MIN, ICON_SIZE_MAX, ID_ICON_SIZE));
    addLabel(layout, page, l10n(L"Properties.IconSize", L"Icon size:"), box(x, 13, labelWidth, 20));
    state.store(ID_MARGIN_H, addSpin(layout, page, box(controlX, 10 + rowHeight, 85, 22),
        MARGIN_MIN, MARGIN_MAX, ID_MARGIN_H));
    state.store(ID_MARGIN_V, addSpin(layout, page, box(controlX + 95, 10 + rowHeight, 85, 22),
        MARGIN_MIN, MARGIN_MAX, ID_MARGIN_V));
    addLabel(layout, page, l10n(L"Properties.Margins", L"Margins (horizontal/vertical):"),
             box(x, 13 + rowHeight, labelWidth, 20));
    state.store(ID_TEXT_LAYOUT, addCombo(state.layout, page, controlX, 10 + 2 * rowHeight, controlWidth,
        {l10n(L"Properties.Without", L"Without text"), l10n(L"Properties.Left", L"Left"),
         l10n(L"Properties.Top", L"Top"), l10n(L"Properties.Right", L"Right"),
         l10n(L"Properties.Bottom", L"Bottom")}, ID_TEXT_LAYOUT));
    addLabel(layout, page, l10n(L"Properties.TextPos", L"Text position:"),
             box(x, 13 + 2 * rowHeight, labelWidth, 20));
    state.store(ID_TEXT_WIDTH, addSpin(layout, page, box(controlX, 10 + 3 * rowHeight, 85, 22),
        TEXT_WIDTH_MIN, TEXT_WIDTH_MAX, ID_TEXT_WIDTH));
    state.store(ID_TEXT_OFFSET, addSpin(layout, page, box(controlX + 95, 10 + 3 * rowHeight, 85, 22),
        TEXT_OFFSET_MIN, TEXT_OFFSET_MAX, ID_TEXT_OFFSET));
    addLabel(layout, page, l10n(L"Properties.TextWidth", L"Text width/indent:"),
             box(x, 13 + 3 * rowHeight, labelWidth, 20));
    state.store(ID_USE_TEXT_COLOR, addCheckBox(layout, page,
        l10n(L"Properties.TextColor", L"Use custom text color"),
        box(x, 10 + 4 * rowHeight, 190, 22), ID_USE_TEXT_COLOR));
    HWND textEdit = layout.add(page, L"Edit", nullptr,
        WS_BORDER | WS_TABSTOP | ES_CENTER | ES_UPPERCASE,
        box(controlX, 10 + 4 * rowHeight, 140, 22), ID_TEXT_COLOR_EDIT);
    state.store(ID_TEXT_COLOR_EDIT, textEdit);
    makeHexEdit(textEdit, 6);
    state.store(ID_TEXT_COLOR_BUTTON, layout.add(page, L"Button", L"...", WS_TABSTOP,
        box(controlX + 145, 10 + 4 * rowHeight, 32, 22), ID_TEXT_COLOR_BUTTON));
    state.store(ID_GLOW_SIZE, addSpin(layout, page, box(controlX, 10 + 5 * rowHeight, controlWidth, 22),
        GLOW_SIZE_MIN, GLOW_SIZE_MAX, ID_GLOW_SIZE));
    addLabel(layout, page, l10n(L"Properties.GlowSize", L"Glow size:"),
             box(x, 13 + 5 * rowHeight, labelWidth, 20));
    state.store(ID_TOOLTIP_SHOW, addCheckBox(layout, page,
        l10n(L"Properties.ShowTooltips", L"Show tooltips"), box(x, 10 + 6 * rowHeight, 370, 20),
        ID_TOOLTIP_SHOW));
    addLabel(layout, page, l10n(L"Properties.Separators", L"Separators"), box(x, 10 + 7 * rowHeight, 370, 20));
    state.store(ID_SEPARATOR_WIDTH, addSpin(layout, page,
        box(controlX, 10 + 8 * rowHeight, controlWidth, 22),
        SEPARATOR_WIDTH_MIN, SEPARATOR_WIDTH_MAX, ID_SEPARATOR_WIDTH));
    addLabel(layout, page, l10n(L"Properties.Width", L"Width:"), box(x, 13 + 8 * rowHeight, labelWidth, 20));
    state.store(ID_SEPARATOR_STYLE, addCombo(state.layout, page, controlX, 10 + 9 * rowHeight,
        controlWidth, {l10n(L"Properties.Line", L"Line"), l10n(L"Properties.Spacer", L"Spacer")},
        ID_SEPARATOR_STYLE));
    addLabel(layout, page, l10n(L"Properties.Style", L"Style:"), box(x, 13 + 9 * rowHeight, labelWidth, 20));
}

void buildAutoHidePage(SettingsState& state, HWND page)
{
    Layout& layout = state.layout;
    const int x = 12;
    const int labelWidth = 200;
    const int controlX = 214;
    const int controlWidth = 180;
    const int rowHeight = 26;

    state.store(ID_AUTO_HIDE, addCheckBox(layout, page,
        l10n(L"Properties.Automatically", L"Automatically"), box(x, 10, 200, 20), ID_AUTO_HIDE));
    addLabel(layout, page, l10n(L"Properties.Hide", L"Hide:"), box(x, 40, 200, 20));
    state.store(ID_SHOW_MODE, addCombo(state.layout, page, controlX, 10 + rowHeight, controlWidth,
        {l10n(L"Properties.MouseHover", L"Mouse hover"),
         l10n(L"Properties.MouseLC", L"Mouse left-click"),
         l10n(L"Properties.MouseRC", L"Mouse right-click")}, ID_SHOW_MODE));
    addLabel(layout, page, l10n(L"Properties.Show", L"Show:"), box(x, 13 + rowHeight, labelWidth, 20));
    state.store(ID_AUTO_SHOW_DELAY, addSpin(layout, page,
        box(controlX, 10 + 2 * rowHeight, controlWidth, 22), 0, 60000, ID_AUTO_SHOW_DELAY));
    addLabel(layout, page, l10n(L"Properties.Delay", L"Delay, ms:"), box(x, 13 + 2 * rowHeight, labelWidth, 20));
    state.store(ID_CORNER1, addSpin(layout, page, box(controlX, 10 + 3 * rowHeight, 85, 22),
        0, CORNER_GAP_WIDTH_MAX, ID_CORNER1));
    state.store(ID_CORNER2, addSpin(layout, page, box(controlX + 95, 10 + 3 * rowHeight, 85, 22),
        0, CORNER_GAP_WIDTH_MAX, ID_CORNER2));
    addLabel(layout, page, l10n(L"Properties.CornerTransWidth", L"Corners transparency width:"),
             box(x, 13 + 3 * rowHeight, labelWidth, 20));
    state.store(ID_HOTKEY, layout.add(page, CLASS_HOTKEY.data(), nullptr, WS_BORDER | WS_TABSTOP,
        box(controlX, 10 + 4 * rowHeight, controlWidth, 22), ID_HOTKEY));
    SetWindowLongPtrW(state.control(ID_HOTKEY), GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(&state.hotkey));
    addLabel(layout, page, l10n(L"Properties.HotKey", L"Keyboard shortcut:"),
             box(x, 13 + 4 * rowHeight, labelWidth, 20));
    state.store(ID_AUTO_HIDE_TRANSPARENCY, addCheckBox(layout, page,
        l10n(L"Properties.AutoHideTransparency", L"Transparent when hidden"),
        box(x, 10 + 5 * rowHeight, 370, 20), ID_AUTO_HIDE_TRANSPARENCY));
}

void buildAdditionalPage(SettingsState& state, HWND page)
{
    Layout& layout = state.layout;
    const int x = 12;
    const int labelWidth = 200;
    const int controlX = 214;
    const int controlWidth = 180;
    const int rowHeight = 26;

    state.store(ID_JUMPLIST_MODE, addCombo(state.layout, page, controlX, 10, controlWidth,
        {l10n(L"Properties.No", L"No"), l10n(L"Properties.MouseRC", L"Mouse right-click")},
        ID_JUMPLIST_MODE));
    addLabel(layout, page, l10n(L"Properties.Jumplists", L"Jump Lists"), box(x, 13, labelWidth, 20));
    addLabel(layout, page, l10n(L"Properties.Show", L"Show:"), box(x, 39, labelWidth, 20));
    state.store(ID_JUMPLIST_RECENT, addSpin(layout, page, box(controlX, 10 + rowHeight, controlWidth, 22),
        JUMPLIST_RECENTMAX_MIN, JUMPLIST_RECENTMAX_MAX, ID_JUMPLIST_RECENT));
    // The localized caption wraps to three lines; the label needs room for all of them.
    addLabel(layout, page, l10n(L"Properties.JumplistRecentMaxItems",
                                L"Number of recent items to display in Jump Lists:"),
             box(x, 13 + rowHeight, labelWidth, 64));
}

void buildSettings(SettingsState& state)
{
    Layout& layout = state.layout;
    HWND window = state.window;

    state.tab = layout.add(window, WC_TABCONTROLW, nullptr, WS_CLIPSIBLINGS | WS_TABSTOP,
                           box(4, 4, 432, 540), ID_TAB);
    state.store(ID_TAB, state.tab);
    state.pages[0] = addPage(state, 0, l10n(L"Properties.PagePanel", L"Panel"));
    state.pages[1] = addPage(state, 1, l10n(L"Properties.PageItems", L"Items"));
    state.pages[2] = addPage(state, 2, l10n(L"Properties.PageAutoHide", L"Autohide"));
    state.pages[3] = addPage(state, 3, l10n(L"Properties.PageAdditional", L"Additional"));

    buildPanelPage(state, state.pages[0]);
    buildItemsPage(state, state.pages[1]);
    buildAutoHidePage(state, state.pages[2]);
    buildAdditionalPage(state, state.pages[3]);

    // Buttons are right-aligned in the 440-wide client area; Apply is the rightmost.
    layout.add(window, L"Button", l10n(L"Button.Ok", L"OK").c_str(),
               WS_TABSTOP | BS_DEFPUSHBUTTON, box(152, 554, 88, 26), IDOK);
    layout.add(window, L"Button", l10n(L"Button.Cancel", L"Cancel").c_str(), WS_TABSTOP,
               box(248, 554, 88, 26), IDCANCEL);
    HWND apply = layout.add(window, L"Button", l10n(L"Button.Apply", L"Apply").c_str(),
                            WS_TABSTOP | WS_DISABLED, box(344, 554, 88, 26), ID_APPLY);
    state.store(ID_APPLY, apply);
}

} // namespace

void registerDialogWindow(HWND window)
{
    if (window != nullptr)
    {
        g_dialogs.push_back(window);
    }
}

void unregisterDialogWindow(HWND window)
{
    std::erase(g_dialogs, window);
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

int runMessageLoop()
{
    MSG message{};
    BOOL result = FALSE;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        if (!isDialogMessage(message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return result < 0 ? -1 : static_cast<int>(message.wParam);
}

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

    registerClass(CLASS_PAGE, &pageProc, buttonFaceBrush(), IDC_ARROW);
    registerClass(CLASS_HOTKEY, &hotkeyProc, nullptr, IDC_IBEAM);
    registerClass(CLASS_SETTINGS, &settingsProc, buttonFaceBrush(), IDC_ARROW);

    const unsigned dpi = windowDpi(bar->window_);
    auto state = std::make_unique<SettingsState>(bar, dpi);
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    const DWORD exStyle = WS_EX_APPWINDOW | WS_EX_DLGMODALFRAME;
    const RECT outer = outerRect(state->layout.px(440), state->layout.px(588), style, exStyle, dpi);
    // Created hidden and registered with the bar before it is shown, so the
    // bar cannot auto-hide while the window is being set up.
    HWND window = CreateWindowExW(exStyle, CLASS_SETTINGS.data(),
        l10n(L"Menu.Properties", L"Settings...").c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, rectWidth(outer), rectHeight(outer),
        bar->window_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr)
    {
        return;
    }
    state->window = window;
    bar->settingsWindow_ = window;
    bar->lockAutoHide_ = true;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state.get()));
    registerDialogWindow(window);
    allowDarkModeForWindow(window, false);

    buildSettings(*state);
    state->load();
    state->canChange = true;
    reduceSysMenu(window);
    centerWindow(window, bar->window_);
    SetFocus(state->control(ID_SCREEN_POSITION));
    ShowWindow(window, SW_SHOW);
    // The first click must reach the control, so the dialog has to be active
    // from the moment it is shown (no synthetic Alt-key workaround).
    bringToFront(window);
    state.release(); // owned by the window; freed in WM_DESTROY
}

namespace {

struct NewBarState
{
    Layout layout;
    HWND window = nullptr;
    HWND edit = nullptr;
    HWND allUsers = nullptr;
    HWND me = nullptr;
    HWND create = nullptr;
    bool quitOnClose = false;
    std::wstring directory;
    std::wstring appDir;
    std::wstring userDir;
    std::wstring fileName;

    explicit NewBarState(unsigned dpi) : layout(dpi) {}
};

std::wstring currentUserName()
{
    // Display name first ("Name Surname"), account name as the fallback. The
    // export lives in secur32, so it is resolved lazily to keep the import
    // table small and the binary loadable without it.
    constexpr int NAME_DISPLAY = 3; // EXTENDED_NAME_FORMAT::NameDisplay
    using GetUserNameExWFn = BOOLEAN(WINAPI*)(int, LPWSTR, PULONG);
    wchar_t name[512]{};
    ULONG size = static_cast<ULONG>(std::size(name));
    if (HMODULE secur32 = LoadLibraryW(L"secur32.dll"))
    {
        auto getDisplayName = reinterpret_cast<GetUserNameExWFn>(
            GetProcAddress(secur32, "GetUserNameExW"));
        const BOOLEAN resolved = getDisplayName != nullptr &&
                                 getDisplayName(NAME_DISPLAY, name, &size) != FALSE;
        FreeLibrary(secur32);
        if (resolved && name[0] != L'\0')
        {
            return name;
        }
    }
    size = static_cast<ULONG>(std::size(name));
    if (GetUserNameW(name, &size) && name[0] != L'\0')
    {
        return name;
    }
    return {};
}

void browseForFolder(NewBarState& state)
{
    ComPtr<IFileDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(dialog.address()))))
    {
        return;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
    if (FAILED(dialog->Show(state.window)))
    {
        return;
    }
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.address())))
    {
        return;
    }
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
    {
        state.directory = path;
        SetWindowTextW(state.edit, path);
        EnableWindow(state.create, TRUE);
        CoTaskMemFree(path);
    }
}

void createLinkbar(NewBarState& state)
{
    if (state.directory.empty())
    {
        return;
    }
    const bool forAll = SendMessageW(state.allUsers, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const std::wstring file = (forAll ? state.appDir : state.userDir) + state.fileName;
    IniFile ini(file);
    ini.writeString(INI_SECTION_MAIN, INI_DIR_LINKS, state.directory);
    ini.flush();
    std::wstring command = commandParam(CLK_FILE, file);
    if (!Translations::instance().locale().empty())
    {
        command = commandParam(CLK_LANG, Translations::instance().locale()) + command;
    }
    createProcess(exeFileName(), command);
    DestroyWindow(state.window);
}

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
    case WM_DPICHANGED:
        if (state != nullptr)
        {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            state->layout.setDpi(HIWORD(wparam));
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         rectWidth(*suggested), rectHeight(*suggested),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_COMMAND:
        if (state != nullptr)
        {
            switch (LOWORD(wparam))
            {
            case NEW_ID_BROWSE:
                browseForFolder(*state);
                return 0;
            case NEW_ID_CREATE:
                createLinkbar(*state);
                return 0;
            case IDCANCEL:
                DestroyWindow(window);
                return 0;
            default:
                break;
            }
        }
        return 0;
    case WM_DESTROY:
    {
        unregisterDialogWindow(window);
        std::unique_ptr<NewBarState> owned(state);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (owned && owned->quitOnClose)
        {
            PostQuitMessage(0);
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

HWND openNewBarDialog(bool quitOnClose)
{
    registerClass(CLASS_NEW, &newBarProc, buttonFaceBrush(), IDC_ARROW);

    const unsigned dpi = systemDpi();
    auto state = std::make_unique<NewBarState>(dpi);
    state->quitOnClose = quitOnClose;
    state->appDir = exeDirectory() + std::wstring(DN_SHARED_BARS);
    state->userDir = linkbarRoamingFolder() + std::wstring(DN_USER_BARS);
    state->fileName = format(L"%08X.lbr", static_cast<unsigned>(GetTickCount64()));

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const RECT outer = outerRect(state->layout.px(440), state->layout.px(190), style,
                                 WS_EX_DLGMODALFRAME, dpi);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, CLASS_NEW.data(),
        l10n(L"New.Caption", L"New linkbar").c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, rectWidth(outer), rectHeight(outer),
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr)
    {
        return nullptr;
    }
    state->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state.get()));
    registerDialogWindow(window);
    allowDarkModeForWindow(window, false);
    Layout& layout = state->layout;

    addLabel(layout, window, l10n(L"New.ToWhom", L"Create linkbar for:"), box(10, 10, 400, 20));
    state->allUsers = layout.add(window, L"Button",
        l10n(L"New.ForAll", L"Anyone who uses this computer (all users)").c_str(),
        BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP, box(10, 32, 410, 20), NEW_ID_ALL_USERS);
    SendMessageW(state->allUsers, BM_SETCHECK, BST_CHECKED, 0);
    state->me = layout.add(window, L"Button",
        format(l10n(L"New.ForMe", L"Only for me (%s)"), currentUserName().c_str()).c_str(),
        BS_AUTORADIOBUTTON | WS_TABSTOP, box(10, 56, 410, 20), NEW_ID_ME);
    addLabel(layout, window, l10n(L"New.Folder", L"Choose a folder:"), box(10, 82, 200, 20));
    state->edit = layout.add(window, L"Edit", nullptr, WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
                             box(10, 104, 370, 22), 0);
    layout.add(window, L"Button", L"...", WS_TABSTOP, box(386, 104, 34, 22), NEW_ID_BROWSE);
    state->create = layout.add(window, L"Button", l10n(L"New.Create", L"Create").c_str(),
                               WS_TABSTOP | BS_DEFPUSHBUTTON | WS_DISABLED,
                               box(260, 142, 80, 26), NEW_ID_CREATE);
    layout.add(window, L"Button", l10n(L"Button.Cancel", L"Cancel").c_str(), WS_TABSTOP,
               box(348, 142, 80, 26), IDCANCEL);

    centerWindow(window, nullptr);
    SetFocus(state->allUsers);
    ShowWindow(window, SW_SHOW);
    // The first click must reach the control, so the dialog has to be active
    // from the moment it is shown (no synthetic Alt-key workaround).
    bringToFront(window);
    state.release(); // owned by the window; freed in WM_DESTROY
    return window;
}

namespace {

struct RenameState
{
    Layout layout;
    BarWindow* owner = nullptr;
    HWND window = nullptr;
    HWND edit = nullptr;
    PIDLIST_ABSOLUTE pidl = nullptr;

    explicit RenameState(unsigned dpi) : layout(dpi) {}
};

void showRenameError(HWND edit, const std::wstring& text)
{
    EDITBALLOONTIP tip{};
    tip.cbStruct = sizeof(tip);
    tip.pszTitle = APP_NAME_LINKBAR.data();
    tip.pszText = text.c_str();
    tip.ttiIcon = TTI_ERROR;
    SendMessageW(edit, EM_SHOWBALLOONTIP, 0, reinterpret_cast<LPARAM>(&tip));
    SetFocus(edit);
}

void submitRename(RenameState& state)
{
    wchar_t buffer[MAX_PATH]{};
    GetWindowTextW(state.edit, buffer, MAX_PATH);
    const std::wstring name = trim(buffer);
    if (name.empty())
    {
        showRenameError(state.edit, l10n(L"Rename.Empty", L"The name cannot be empty."));
        return;
    }
    if (name.find_first_of(std::wstring_view(INVALID_NAME_CHARS.data(), INVALID_NAME_CHARS.size())) !=
        std::wstring::npos)
    {
        showRenameError(state.edit, l10n(L"Rename.InvalidChars",
            L"The name cannot contain these characters: \\ / : * ? \" < > |"));
        return;
    }
    if (FAILED(shRename(state.window, state.pidl, name)))
    {
        showRenameError(state.edit, l10n(L"Rename.Failed",
            L"This name is already used or is not valid."));
        return;
    }
    DestroyWindow(state.window);
}

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
    case WM_DPICHANGED:
        if (state != nullptr)
        {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            state->layout.setDpi(HIWORD(wparam));
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         rectWidth(*suggested), rectHeight(*suggested),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_COMMAND:
        if (state != nullptr)
        {
            if (LOWORD(wparam) == IDOK)
            {
                submitRename(*state);
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
    {
        unregisterDialogWindow(window);
        std::unique_ptr<RenameState> owned(state);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (owned)
        {
            CoTaskMemFree(owned->pidl);
            if (owned->owner != nullptr && owned->owner->window_ != nullptr)
            {
                owned->owner->lockAutoHide_ = false;
                EnableWindow(owned->owner->window_, TRUE);
                SetForegroundWindow(owned->owner->window_);
            }
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

void openRenameDialog(BarWindow* owner, PIDLIST_ABSOLUTE pidl)
{
    if (owner == nullptr || pidl == nullptr)
    {
        return;
    }
    registerClass(CLASS_RENAME, &renameProc, buttonFaceBrush(), IDC_ARROW);

    const unsigned dpi = windowDpi(owner->window_);
    auto state = std::make_unique<RenameState>(dpi);
    state->owner = owner;
    state->pidl = ILClone(pidl);

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const RECT outer = outerRect(state->layout.px(420), state->layout.px(105), style,
                                 WS_EX_DLGMODALFRAME, dpi);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, CLASS_RENAME.data(),
        l10n(L"Rename.Caption", L"Renaming").c_str(), style,
        CW_USEDEFAULT, CW_USEDEFAULT, rectWidth(outer), rectHeight(outer),
        owner->window_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr)
    {
        CoTaskMemFree(state->pidl);
        return;
    }
    state->window = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state.get()));
    registerDialogWindow(window);
    allowDarkModeForWindow(window, false);
    Layout& layout = state->layout;

    state->edit = layout.add(window, L"Edit", displayName(pidl).c_str(),
                             WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, box(10, 10, 390, 24), 0);
    SendMessageW(state->edit, EM_LIMITTEXT, MAX_PATH, 0);
    SendMessageW(state->edit, EM_SETSEL, 0, -1);
    layout.add(window, L"Button", l10n(L"Button.Ok", L"OK").c_str(),
               WS_TABSTOP | BS_DEFPUSHBUTTON, box(240, 44, 79, 26), IDOK);
    layout.add(window, L"Button", l10n(L"Button.Cancel", L"Cancel").c_str(), WS_TABSTOP,
               box(328, 44, 79, 26), IDCANCEL);

    // Modal behaviour for the bar: it stays disabled while the rename is open.
    owner->lockAutoHide_ = true;
    EnableWindow(owner->window_, FALSE);
    centerWindow(window, owner->window_);
    SetFocus(state->edit);
    ShowWindow(window, SW_SHOW);
    // The first click must reach the control, so the dialog has to be active
    // from the moment it is shown (no synthetic Alt-key workaround).
    bringToFront(window);
    state.release(); // owned by the window; freed in WM_DESTROY
}

} // namespace lb


