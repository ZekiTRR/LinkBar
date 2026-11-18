// Modal color picker. Every pixel rectangle is derived from the real client
// area, so DPI changes cannot corrupt the drawing; the modal loop, focus and
// activation belong to Windows (DialogBoxParam), not to this module.

#include "ColorPicker.h"

#include "Common.h"
#include "DibBitmap.h"
#include "Localization.h"
#include "StringUtil.h"

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <string>
#include <dwmapi.h>
#include <windowsx.h>

namespace lb {
namespace {

constexpr UINT PICKER_DIALOG = 100;
constexpr int EDIT_ID = 1000;

constexpr Color BACKGROUND = 0x001E1E1E;
constexpr Color BORDER = 0x00454545;
constexpr Color CHECKER_A = 0x00484848;
constexpr Color CHECKER_B = 0x00646464;
constexpr Color BUTTON_FILL = 0x00383838;
constexpr Color BUTTON_HOT = 0x004D4D4D;
constexpr Color ACCENT_FILL = 0x002F6FD0;
constexpr Color ACCENT_HOT = 0x004388E8;

struct Input
{
    Color initial = 0xFFFFFFFF;
    Color result = 0xFFFFFFFF;
    bool accepted = false;
};

struct PickerState
{
    Input* input = nullptr;
    HWND edit = nullptr;
    HBRUSH editBrush = nullptr;
    HFONT font = nullptr;
    Color color = 0xFFFFFFFF;
    Hsv hsv{};
    bool updatingEdit = false;
    bool dragSv = false;
    bool dragHue = false;
    bool dragAlpha = false;
    RECT sv{};
    RECT hue{};
    RECT alpha{};
    RECT preview{};
    RECT hex{};
    RECT cancel{};
    RECT ok{};
    DibBitmap canvas;
};

[[nodiscard]] COLORREF asColorRef(Color color) noexcept
{
    return RGB(colorRed(color), colorGreen(color), colorBlue(color));
}

[[nodiscard]] Color blend(Color foreground, Color background, int alpha) noexcept
{
    const int a = std::clamp(alpha, 0, 255);
    const auto mix = [a](int from, int to) {
        return (from * a + to * (255 - a) + 127) / 255;
    };
    return makeArgb(255, mix(colorRed(foreground), colorRed(background)),
                    mix(colorGreen(foreground), colorGreen(background)),
                    mix(colorBlue(foreground), colorBlue(background)));
}

void computeLayout(PickerState& state, HWND window)
{
    RECT client{};
    GetClientRect(window, &client);
    const int w = rectWidth(client);
    const int h = rectHeight(client);
    const int margin = std::max(8, w / 22);
    const int bar = std::max(10, w / 22);
    const int gap = std::max(4, w / 80);
    const int bottom = std::max(26, h / 8);
    const int previewWidth = 2 * bar + gap;
    const int barsX = w - margin - (2 * bar + 2 * gap + previewWidth);
    const int fieldHeight = h - 2 * margin - bottom - gap;

    state.sv = makeRect(margin, margin, barsX - gap - margin, fieldHeight);
    state.hue = makeRect(barsX, margin, bar, fieldHeight);
    state.alpha = makeRect(barsX + bar + gap, margin, bar, fieldHeight);
    state.preview = makeRect(state.alpha.right + gap, margin, previewWidth, fieldHeight);

    const int buttonWidth = std::max(56, w / 5);
    const int buttonHeight = std::max(24, bottom);
    const int rowY = h - margin - buttonHeight;
    const int hexWidth = w - 2 * margin - 2 * buttonWidth - 2 * gap;
    state.hex = makeRect(margin, rowY, hexWidth, buttonHeight);
    state.cancel = makeRect(w - margin - 2 * buttonWidth - gap, rowY, buttonWidth, buttonHeight);
    state.ok = makeRect(w - margin - buttonWidth, rowY, buttonWidth, buttonHeight);
}

[[nodiscard]] RECT clipped(const DibBitmap& canvas, RECT rect) noexcept
{
    RECT result{std::max(static_cast<int>(rect.left), 0),
                std::max(static_cast<int>(rect.top), 0),
                std::min(static_cast<int>(rect.right), canvas.width()),
                std::min(static_cast<int>(rect.bottom), canvas.height())};
    if (result.right <= result.left || result.bottom <= result.top)
    {
        return RECT{0, 0, 0, 0};
    }
    return result;
}

void drawGradientAreas(PickerState& state)
{
    if (state.canvas.bits() == nullptr)
    {
        return;
    }
    auto* pixels = static_cast<Color*>(state.canvas.bits());
    const int stride = state.canvas.width();
    const auto put = [&](int x, int y, Color color) {
        pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
               static_cast<std::size_t>(x)] = color;
    };

    // Saturation / value field.
    const RECT svClip = clipped(state.canvas, state.sv);
    if (!IsRectEmpty(&svClip))
    {
        for (int y = svClip.top; y < svClip.bottom; ++y)
        {
            const double v = 1.0 - static_cast<double>(y - state.sv.top) /
                                  std::max(rectHeight(state.sv) - 1, 1);
            for (int x = svClip.left; x < svClip.right; ++x)
            {
                const double s = static_cast<double>(x - state.sv.left) /
                                 std::max(rectWidth(state.sv) - 1, 1);
                put(x, y, fromHsv(state.hsv.h, s, v, 255));
            }
        }
    }

    // Hue bar.
    const RECT hueClip = clipped(state.canvas, state.hue);
    if (!IsRectEmpty(&hueClip))
    {
        for (int y = hueClip.top; y < hueClip.bottom; ++y)
        {
            const double hue = 360.0 * static_cast<double>(y - state.hue.top) /
                               std::max(rectHeight(state.hue) - 1, 1);
            const Color row = fromHsv(hue, 1.0, 1.0, 255);
            for (int x = hueClip.left; x < hueClip.right; ++x)
            {
                put(x, y, row);
            }
        }
    }

    const auto checker = [](int x, int y) {
        return ((x / 8) + (y / 8)) % 2 == 0 ? CHECKER_A : CHECKER_B;
    };

    // Alpha bar over a checkerboard.
    const RECT alphaClip = clipped(state.canvas, state.alpha);
    if (!IsRectEmpty(&alphaClip))
    {
        for (int y = alphaClip.top; y < alphaClip.bottom; ++y)
        {
            const int a = static_cast<int>(255.0 * (1.0 - static_cast<double>(y - state.alpha.top) /
                                                    std::max(rectHeight(state.alpha) - 1, 1)) + 0.5);
            const Color solid = withAlpha(state.color, 255);
            for (int x = alphaClip.left; x < alphaClip.right; ++x)
            {
                put(x, y, blend(solid, checker(x, y), a));
            }
        }
    }

    // Preview: new color on top, original color below.
    const RECT previewClip = clipped(state.canvas, state.preview);
    if (!IsRectEmpty(&previewClip))
    {
        const int split = rectHeight(state.preview) / 2;
        for (int y = previewClip.top; y < previewClip.bottom; ++y)
        {
            const bool isNew = (y - state.preview.top) < split;
            const Color color = isNew ? state.color : state.input->initial;
            for (int x = previewClip.left; x < previewClip.right; ++x)
            {
                put(x, y, blend(color, checker(x, y), colorAlpha(color)));
            }
        }
    }
}

void drawFrame(HDC dc, const RECT& rect)
{
    HPEN pen = CreatePen(PS_SOLID, 1, asColorRef(BORDER));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawMarker(HDC dc, int x, int y, int radius, Hsv hsv)
{
    HPEN outer = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
    HPEN inner = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(dc, outer);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, x - radius - 1, y - radius - 1, x + radius + 2, y + radius + 2);
    SelectObject(dc, inner);
    Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(outer);
    DeleteObject(inner);
    (void)hsv;
}

void drawLineMarker(HDC dc, const RECT& rect, int y)
{
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, rect.left, y, nullptr);
    LineTo(dc, rect.right, y);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void render(PickerState& state, HWND window)
{
    RECT client{};
    GetClientRect(window, &client);
    const int width = rectWidth(client);
    const int height = rectHeight(client);
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
    {
        return;
    }
    state.canvas.setSize(width, height);
    state.canvas.fillRect(client, BACKGROUND);
    drawGradientAreas(state);

    HDC dc = state.canvas.dc();
    drawFrame(dc, state.sv);
    drawFrame(dc, state.hue);
    drawFrame(dc, state.alpha);
    drawFrame(dc, state.preview);
    drawFrame(dc, state.hex);

    const int svX = state.sv.left +
                    static_cast<int>(state.hsv.s * (rectWidth(state.sv) - 1));
    const int svY = state.sv.top +
                    static_cast<int>((1.0 - state.hsv.v) * (rectHeight(state.sv) - 1));
    drawMarker(dc, svX, svY, 6, state.hsv);
    drawLineMarker(dc, state.hue,
                   state.hue.top + static_cast<int>(state.hsv.h / 360.0 *
                                                    (rectHeight(state.hue) - 1)));
    drawLineMarker(dc, state.alpha,
                   state.alpha.top + static_cast<int>((1.0 - colorAlpha(state.color) / 255.0) *
                                                      (rectHeight(state.alpha) - 1)));
}

// Repaint through WM_PAINT only: painting the parent with GetDC would cover
// the child buttons and the HEX edit.
void repaint(HWND window)
{
    InvalidateRect(window, nullptr, FALSE);
    UpdateWindow(window);
}

void updateEdit(PickerState& state)
{
    if (state.edit == nullptr)
    {
        return;
    }
    state.updatingEdit = true;
    SetWindowTextW(state.edit, toHexColor(state.color, 8).c_str());
    state.updatingEdit = false;
}

void applyHsv(PickerState& state, HWND window, int alpha = -1)
{
    state.color = fromHsv(state.hsv.h, state.hsv.s, state.hsv.v,
                          alpha < 0 ? colorAlpha(state.color) : alpha);
    updateEdit(state);
    render(state, window);
    repaint(window);
}

void updateFromSv(PickerState& state, HWND window, int x, int y)
{
    state.hsv.s = std::clamp(static_cast<double>(x - state.sv.left) /
                             std::max(rectWidth(state.sv) - 1, 1), 0.0, 1.0);
    state.hsv.v = std::clamp(1.0 - static_cast<double>(y - state.sv.top) /
                             std::max(rectHeight(state.sv) - 1, 1), 0.0, 1.0);
    applyHsv(state, window);
}

void updateFromHue(PickerState& state, HWND window, int y)
{
    state.hsv.h = std::clamp(static_cast<double>(y - state.hue.top) /
                             std::max(rectHeight(state.hue) - 1, 1), 0.0, 1.0) * 360.0;
    // Black and white have no usable hue: make the selection visible.
    if (state.hsv.v < 0.01) state.hsv.v = 1.0;
    if (state.hsv.s < 0.01) state.hsv.s = 1.0;
    applyHsv(state, window);
}

void updateFromAlpha(PickerState& state, HWND window, int y)
{
    const int alpha = static_cast<int>(255.0 * (1.0 - static_cast<double>(y - state.alpha.top) /
                                                std::max(rectHeight(state.alpha) - 1, 1)) + 0.5);
    applyHsv(state, window, std::clamp(alpha, 0, 255));
}

[[nodiscard]] bool inside(const RECT& rect, int x, int y) noexcept
{
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

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

void drawOwnButton(const DRAWITEMSTRUCT& item, const std::wstring& text, Color fill)
{
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const Color color = pressed ? BUTTON_HOT : fill;
    HBRUSH brush = CreateSolidBrush(asColorRef(color));
    HPEN pen = CreatePen(PS_SOLID, 1, asColorRef(BORDER));
    HGDIOBJ oldBrush = SelectObject(item.hDC, brush);
    HGDIOBJ oldPen = SelectObject(item.hDC, pen);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom,
              6, 6);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, RGB(255, 255, 255));
    DrawTextW(item.hDC, text.c_str(), -1, const_cast<RECT*>(&item.rcItem),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

INT_PTR CALLBACK pickerProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto* state = reinterpret_cast<PickerState*>(GetWindowLongPtrW(window, DWLP_USER));
    switch (message)
    {
    case WM_INITDIALOG:
    {
        auto* input = reinterpret_cast<Input*>(lparam);
        auto owned = std::make_unique<PickerState>();
        owned->input = input;
        owned->color = colorAlpha(input->initial) == 0
                           ? withAlpha(input->initial, 255) : input->initial;
        owned->hsv = toHsv(owned->color);
        if (owned->hsv.v < 0.01) owned->hsv.v = 1.0;
        if (owned->hsv.s < 0.01) owned->hsv.s = 1.0;
        owned->color = fromHsv(owned->hsv.h, owned->hsv.s, owned->hsv.v,
                               colorAlpha(owned->color));
        SetWindowLongPtrW(window, DWLP_USER, reinterpret_cast<LONG_PTR>(owned.get()));

        SetWindowTextW(window, l10n(L"Color.Caption", L"Color").c_str());
        BOOL dark = TRUE;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        computeLayout(*owned, window);
        owned->editBrush = CreateSolidBrush(asColorRef(0x00242424));
        LOGFONTW logFont{};
        logFont.lfHeight = -14;
        logFont.lfWeight = FW_NORMAL;
        logFont.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(logFont.lfFaceName, L"Segoe UI");
        owned->font = CreateFontIndirectW(&logFont);

        const RECT box = owned->hex;
        owned->edit = CreateWindowExW(0, L"Edit", nullptr,
                                      WS_CHILD | WS_VISIBLE | ES_CENTER | ES_UPPERCASE |
                                          ES_AUTOHSCROLL,
                                      box.left + 2, box.top + 2, rectWidth(box) - 4,
                                      rectHeight(box) - 4, window,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(EDIT_ID)),
                                      GetModuleHandleW(nullptr), nullptr);
        SendMessageW(owned->edit, WM_SETFONT, reinterpret_cast<WPARAM>(owned->font), TRUE);
        SendMessageW(owned->edit, EM_LIMITTEXT, 8, 0);
        auto* original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            owned->edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&hexEditProc)));
        SetWindowLongPtrW(owned->edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(original));
        updateEdit(*owned);

        CreateWindowExW(0, L"Button", l10n(L"Button.Cancel", L"Cancel").c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                        owned->cancel.left, owned->cancel.top, rectWidth(owned->cancel),
                        rectHeight(owned->cancel), window, reinterpret_cast<HMENU>(IDCANCEL),
                        GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, L"Button", l10n(L"Button.Ok", L"OK").c_str(),
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON | BS_OWNERDRAW,
                        owned->ok.left, owned->ok.top, rectWidth(owned->ok),
                        rectHeight(owned->ok), window, reinterpret_cast<HMENU>(IDOK),
                        GetModuleHandleW(nullptr), nullptr);

        render(*owned, window);
        bringToFront(window);
        SetFocus(owned->edit);
        owned.release();
        return FALSE;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (state != nullptr && state->canvas.handle() != nullptr)
        {
            BitBlt(dc, 0, 0, state->canvas.width(), state->canvas.height(),
                   state->canvas.dc(), 0, 0, SRCCOPY);
        }
        EndPaint(window, &paint);
        return TRUE;
    }
    case WM_ERASEBKGND:
        return TRUE;
    case WM_DRAWITEM:
    {
        if (state == nullptr)
        {
            break;
        }
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (item->CtlID == IDOK)
        {
            drawOwnButton(*item, l10n(L"Button.Ok", L"OK"), ACCENT_FILL);
            return TRUE;
        }
        if (item->CtlID == IDCANCEL)
        {
            drawOwnButton(*item, l10n(L"Button.Cancel", L"Cancel"), BUTTON_FILL);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    {
        if (state == nullptr)
        {
            break;
        }
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkColor(dc, asColorRef(0x00242424));
        SetTextColor(dc, RGB(242, 242, 242));
        return reinterpret_cast<INT_PTR>(state->editBrush);
    }
    case WM_COMMAND:
    {
        const int id = LOWORD(wparam);
        if (id == IDOK)
        {
            if (state != nullptr) state->input->result = state->color;
            state->input->accepted = true;
            EndDialog(window, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL)
        {
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        if (id == EDIT_ID && HIWORD(wparam) == EN_CHANGE && state != nullptr &&
            !state->updatingEdit)
        {
            wchar_t text[32]{};
            GetWindowTextW(state->edit, text, static_cast<int>(std::size(text)));
            std::wstring digits;
            for (const wchar_t* p = text; *p != L'\0' && digits.size() < 8; ++p)
            {
                if (isHexDigit(*p)) digits.push_back(static_cast<wchar_t>(std::towupper(*p)));
            }
            if (digits != text)
            {
                state->updatingEdit = true;
                SetWindowTextW(state->edit, digits.c_str());
                SendMessageW(state->edit, EM_SETSEL, digits.size(), digits.size());
                state->updatingEdit = false;
            }
            if (digits.size() == 6 || digits.size() == 8)
            {
                if (const auto color = parseHexColor(digits))
                {
                    state->color = *color;
                    state->hsv = toHsv(state->color);
                    render(*state, window);
                    repaint(window);
                }
            }
            return TRUE;
        }
        return TRUE;
    }
    case WM_LBUTTONDOWN:
    {
        if (state == nullptr)
        {
            break;
        }
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        if (inside(state->sv, x, y))
        {
            state->dragSv = true;
            SetCapture(window);
            updateFromSv(*state, window, x, y);
        }
        else if (inside(state->hue, x, y))
        {
            state->dragHue = true;
            SetCapture(window);
            updateFromHue(*state, window, y);
        }
        else if (inside(state->alpha, x, y))
        {
            state->dragAlpha = true;
            SetCapture(window);
            updateFromAlpha(*state, window, y);
        }
        else if (inside(state->preview, x, y))
        {
            // Restore the original color.
            state->color = state->input->initial;
            state->hsv = toHsv(state->color);
            if (state->hsv.v < 0.01) state->hsv.v = 1.0;
            if (state->hsv.s < 0.01) state->hsv.s = 1.0;
            applyHsv(*state, window);
        }
        return TRUE;
    }
    case WM_MOUSEMOVE:
        if (state != nullptr)
        {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            if (state->dragSv) updateFromSv(*state, window, x, y);
            else if (state->dragHue) updateFromHue(*state, window, y);
            else if (state->dragAlpha) updateFromAlpha(*state, window, y);
        }
        return TRUE;
    case WM_LBUTTONUP:
        if (state != nullptr && (state->dragSv || state->dragHue || state->dragAlpha))
        {
            state->dragSv = false;
            state->dragHue = false;
            state->dragAlpha = false;
            ReleaseCapture();
        }
        return TRUE;
    case WM_CAPTURECHANGED:
        if (state != nullptr)
        {
            state->dragSv = false;
            state->dragHue = false;
            state->dragAlpha = false;
        }
        return TRUE;
    case WM_SETCURSOR:
    {
        if (state != nullptr && LOWORD(lparam) == HTCLIENT)
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            const bool active = inside(state->sv, point.x, point.y) ||
                                inside(state->hue, point.x, point.y) ||
                                inside(state->alpha, point.x, point.y) ||
                                inside(state->preview, point.x, point.y);
            SetCursor(LoadCursorW(nullptr, active ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    }
    case WM_NCDESTROY:
        if (state != nullptr)
        {
            if (state->font != nullptr) DeleteObject(state->font);
            if (state->editBrush != nullptr) DeleteObject(state->editBrush);
            delete state;
            SetWindowLongPtrW(window, DWLP_USER, 0);
        }
        return TRUE;
    default:
        break;
    }
    return FALSE;
}

} // namespace

bool pickColor(HWND owner, Color initial, Color& result)
{
    Input input;
    input.initial = colorAlpha(initial) == 0 ? withAlpha(initial, 255) : initial;
    input.result = input.initial;
    const INT_PTR code = DialogBoxParamW(GetModuleHandleW(nullptr),
                                         MAKEINTRESOURCEW(PICKER_DIALOG), owner,
                                         &pickerProc, reinterpret_cast<LPARAM>(&input));
    if (code == IDOK && input.accepted)
    {
        result = input.result;
        return true;
    }
    return false;
}

} // namespace lb
