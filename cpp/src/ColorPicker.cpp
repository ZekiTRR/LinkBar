#include "ColorPicker.h"

#include "Common.h"
#include "DibBitmap.h"
#include "Localization.h"
#include "StringUtil.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) (static_cast<int>(static_cast<short>(LOWORD(lp))))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) (static_cast<int>(static_cast<short>(HIWORD(lp))))
#endif

namespace lb {
namespace {

struct Rgb
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

// ColorUtils.pas: exact port.
void minMax(std::uint8_t i, std::uint8_t j, std::uint8_t k, int& minimum, int& maximum)
{
    if (i > j)
    {
        maximum = i > k ? i : k;
        minimum = j < k ? j : k;
    }
    else
    {
        maximum = j > k ? j : k;
        minimum = i < k ? i : k;
    }
}

void rgbToHsb(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
              int& hue, int& saturation, int& brightness)
{
    int minimum = 0;
    int maximum = 0;
    minMax(red, green, blue, minimum, maximum);
    brightness = maximum;
    const int delta = maximum - minimum;
    saturation = (maximum == 0) ? 0 : (255 * delta) / maximum;
    int result = 0;
    if (saturation != 0)
    {
        if (red == brightness)
        {
            result = (60 * (green - blue)) / delta;
        }
        else if (green == brightness)
        {
            result = 120 + (60 * (blue - red)) / delta;
        }
        else if (blue == brightness)
        {
            result = 240 + (60 * (red - green)) / delta;
        }
        if (result < 0)
        {
            result += 360;
        }
    }
    hue = result;
}

void hsbToRgb(int hue, int saturation, int brightness, Rgb& color)
{
    constexpr int divisor = 255 * 60;
    if (saturation == 0)
    {
        color = Rgb{static_cast<std::uint8_t>(brightness),
                    static_cast<std::uint8_t>(brightness),
                    static_cast<std::uint8_t>(brightness)};
        return;
    }
    const int temp = (hue == 360) ? 0 : hue;
    const int f = temp % 60;
    const int vs = brightness * saturation;
    const int p = brightness - vs / 255;
    const int q = brightness - (vs * f) / divisor;
    const int t = brightness - (vs * (60 - f)) / divisor;
    switch (temp / 60)
    {
    case 0: color = Rgb{static_cast<std::uint8_t>(brightness),
                        static_cast<std::uint8_t>(t),
                        static_cast<std::uint8_t>(p)}; break;
    case 1: color = Rgb{static_cast<std::uint8_t>(q),
                        static_cast<std::uint8_t>(brightness),
                        static_cast<std::uint8_t>(p)}; break;
    case 2: color = Rgb{static_cast<std::uint8_t>(p),
                        static_cast<std::uint8_t>(brightness),
                        static_cast<std::uint8_t>(t)}; break;
    case 3: color = Rgb{static_cast<std::uint8_t>(p),
                        static_cast<std::uint8_t>(q),
                        static_cast<std::uint8_t>(brightness)}; break;
    case 4: color = Rgb{static_cast<std::uint8_t>(t),
                        static_cast<std::uint8_t>(p),
                        static_cast<std::uint8_t>(brightness)}; break;
    default: color = Rgb{static_cast<std::uint8_t>(brightness),
                         static_cast<std::uint8_t>(p),
                         static_cast<std::uint8_t>(q)}; break;
    }
}

std::uint32_t makeColor(const Rgb& color, std::uint8_t alpha) noexcept
{
    return (static_cast<std::uint32_t>(alpha) << 24) |
           (static_cast<std::uint32_t>(color.r) << 16) |
           (static_cast<std::uint32_t>(color.g) << 8) |
           color.b;
}

constexpr int BOX_LEFT = 32;
constexpr int BOX_TOP = 34;
constexpr int BOX_SIZE = 137;
constexpr int ALPHA_LEFT = 5;
constexpr int ALPHA_TOP = 34;
constexpr int ALPHA_WIDTH = 22;
constexpr int ALPHA_HEIGHT = 137;
constexpr int HUE_LEFT = 5;
constexpr int HUE_TOP = 176;
constexpr int HUE_WIDTH = 191;
constexpr int HUE_HEIGHT = 22;
constexpr int PREVIEW_LEFT = 174;
constexpr int PREVIEW_TOP = 34;
constexpr int PREVIEW_WIDTH = 22;
constexpr int PREVIEW_HEIGHT = 137;

struct ColorState
{
    HWND window = nullptr;
    HWND edit = nullptr;
    std::optional<std::uint32_t> result;
    std::uint32_t oldColor = 0;
    std::uint32_t newColor = 0;
    Rgb oldRgb{};
    Rgb newRgb{};
    std::uint8_t newAlpha = 255;
    int boxX = 0;
    int boxY = 0;
    int barX = 0;
    int barA = 0;
    bool dragBox = false;
    bool dragBar = false;
    bool dragAlpha = false;
    bool updatingEdit = false;
    int lastHue = -1;
    DibBitmap canvas;
    std::array<std::uint8_t, 138> lut{};
};

void setFromColor(ColorState& state, std::uint32_t color, bool updateOld)
{
    if (updateOld)
    {
        state.oldColor = color;
        state.oldRgb = Rgb{static_cast<std::uint8_t>((color >> 16) & 0xFF),
                           static_cast<std::uint8_t>((color >> 8) & 0xFF),
                           static_cast<std::uint8_t>(color & 0xFF)};
    }
    state.newColor = color;
    state.newRgb = Rgb{static_cast<std::uint8_t>((color >> 16) & 0xFF),
                       static_cast<std::uint8_t>((color >> 8) & 0xFF),
                       static_cast<std::uint8_t>(color & 0xFF)};
    state.newAlpha = static_cast<std::uint8_t>((color >> 24) & 0xFF);

    int hue = 0;
    int saturation = 0;
    int brightness = 0;
    rgbToHsb(state.newRgb.r, state.newRgb.g, state.newRgb.b, hue, saturation, brightness);
    state.boxX = MulDiv(137, saturation, 255);
    state.boxY = 137 - MulDiv(137, brightness, 255);
    state.barX = MulDiv(HUE_WIDTH - 1, hue, 360);
    state.barA = MulDiv(ALPHA_HEIGHT - 1, state.newAlpha, 255);
    state.lastHue = -1;
}

void updateFromBox(ColorState& state)
{
    hsbToRgb(MulDiv(360, state.barX, HUE_WIDTH - 1), state.lut[static_cast<std::size_t>(state.boxX)],
             255 - state.lut[static_cast<std::size_t>(state.boxY)], state.newRgb);
    state.newColor = makeColor(state.newRgb, state.newAlpha);
}

void updateEditText(ColorState& state)
{
    if (state.edit == nullptr)
    {
        return;
    }
    state.updatingEdit = true;
    const std::wstring text = format(L"%08X", state.newColor);
    SetWindowTextW(state.edit, text.c_str());
    state.updatingEdit = false;
}

void ensureCanvas(ColorState& state, int width, int height)
{
    if (state.canvas.width() != width || state.canvas.height() != height)
    {
        state.canvas.setSize(width, height);
    }
    state.canvas.clear();
    std::uint32_t* pixels = static_cast<std::uint32_t*>(state.canvas.bits());
    const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::uint32_t face = 0xFFF0F0F0U;
    for (std::size_t i = 0; i < total; ++i)
    {
        pixels[i] = face;
    }
}

void putPixel(DibBitmap& bitmap, int x, int y, std::uint32_t color)
{
    auto* pixels = static_cast<std::uint32_t*>(bitmap.bits());
    if (pixels == nullptr || x < 0 || y < 0 || x >= bitmap.width() || y >= bitmap.height())
    {
        return;
    }
    // DIB pixels are 0xAARRGGBB in memory on little-endian; GDI expects BGR order,
    // so build 0x00RRGGBB for direct pixel writes.
    pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap.width()) +
           static_cast<std::size_t>(x)] = color & 0x00FFFFFFU;
}

void drawHueBar(ColorState& state)
{
    for (int x = 0; x < HUE_WIDTH; ++x)
    {
        Rgb color{};
        hsbToRgb(MulDiv(360, x, HUE_WIDTH - 1), 255, 255, color);
        const std::uint32_t pixel = (static_cast<std::uint32_t>(color.r) << 16) |
                                    (static_cast<std::uint32_t>(color.g) << 8) | color.b;
        for (int y = 0; y < HUE_HEIGHT; ++y)
        {
            putPixel(state.canvas, HUE_LEFT + x, HUE_TOP + y, pixel);
        }
    }
}

void drawAlphaBar(ColorState& state)
{
    for (int y = 0; y < ALPHA_HEIGHT; ++y)
    {
        bool check = (y & 4) == 0;
        const int a = 255 - MulDiv(255, y, ALPHA_HEIGHT - 1);
        const int r1 = (a * (164 - state.newRgb.r) >> 8) + state.newRgb.r;
        const int g1 = (a * (164 - state.newRgb.g) >> 8) + state.newRgb.g;
        const int b1 = (a * (164 - state.newRgb.b) >> 8) + state.newRgb.b;
        const int r2 = (a * (255 - state.newRgb.r) >> 8) + state.newRgb.r;
        const int g2 = (a * (255 - state.newRgb.g) >> 8) + state.newRgb.g;
        const int b2 = (a * (255 - state.newRgb.b) >> 8) + state.newRgb.b;
        for (int x = 0; x < ALPHA_WIDTH; ++x)
        {
            if ((x + 1) % 4 == 0)
            {
                check = !check;
            }
            const std::uint32_t color = check
                ? (static_cast<std::uint32_t>(r1) << 16) | (static_cast<std::uint32_t>(g1) << 8) | b1
                : (static_cast<std::uint32_t>(r2) << 16) | (static_cast<std::uint32_t>(g2) << 8) | b2;
            putPixel(state.canvas, ALPHA_LEFT + x, ALPHA_TOP + y, color);
        }
    }
}

void drawColorBox(ColorState& state)
{
    const int hue = MulDiv(360, state.barX, HUE_WIDTH - 1);
    if (hue != state.lastHue)
    {
        state.lastHue = hue;
        Rgb base{};
        hsbToRgb(hue, 255, 255, base);
        for (int y = 0; y < BOX_SIZE; ++y)
        {
            const int s1 = state.lut[static_cast<std::size_t>(y)];
            const int m1 = (s1 * -255 >> 8) + 255;
            const int q1 = ((s1 * -base.r) >> 8) + base.r - m1;
            const int q2 = ((s1 * -base.g) >> 8) + base.g - m1;
            const int q3 = ((s1 * -base.b) >> 8) + base.b - m1;
            for (int x = 0; x < BOX_SIZE; ++x)
            {
                const int s2 = state.lut[static_cast<std::size_t>(x)];
                const int r = std::clamp((s2 * q1 >> 8) + m1, 0, 255);
                const int g = std::clamp((s2 * q2 >> 8) + m1, 0, 255);
                const int b = std::clamp((s2 * q3 >> 8) + m1, 0, 255);
                putPixel(state.canvas, BOX_LEFT + x, BOX_TOP + y,
                         (static_cast<std::uint32_t>(r) << 16) |
                         (static_cast<std::uint32_t>(g) << 8) |
                         static_cast<std::uint32_t>(b));
            }
        }
    }
}

void drawPreview(ColorState& state)
{
    const Rgb colors[2] = {state.oldRgb, state.newRgb};
    const std::uint8_t alphas[2] = {static_cast<std::uint8_t>(0xFF - ((state.oldColor >> 24) & 0xFF)),
                                    static_cast<std::uint8_t>(0xFF - ((state.newColor >> 24) & 0xFF))};
    for (int half = 0; half < 2; ++half)
    {
        const Rgb& color = colors[half];
        const int a = alphas[half];
        const int r1 = (a * (164 - color.r) >> 8) + color.r;
        const int g1 = (a * (164 - color.g) >> 8) + color.g;
        const int b1 = (a * (164 - color.b) >> 8) + color.b;
        const int r2 = (a * (255 - color.r) >> 8) + color.r;
        const int g2 = (a * (255 - color.g) >> 8) + color.g;
        const int b2 = (a * (255 - color.b) >> 8) + color.b;
        for (int y = 0; y < PREVIEW_HEIGHT / 2; ++y)
        {
            bool check = (y & 4) == 0;
            for (int x = 0; x < PREVIEW_WIDTH; ++x)
            {
                if ((x + 1) % 4 == 0)
                {
                    check = !check;
                }
                const std::uint32_t pixel = check
                    ? (static_cast<std::uint32_t>(r1) << 16) | (static_cast<std::uint32_t>(g1) << 8) | b1
                    : (static_cast<std::uint32_t>(r2) << 16) | (static_cast<std::uint32_t>(g2) << 8) | b2;
                putPixel(state.canvas, PREVIEW_LEFT + x, PREVIEW_TOP + half * (PREVIEW_HEIGHT / 2) + y,
                         pixel);
            }
        }
    }
}

void drawMarker(DibBitmap& bitmap, int centerX, int centerY, int radius)
{
    HDC dc = bitmap.dc();
    HPEN black = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    HPEN white = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ oldPen = SelectObject(dc, black);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, centerX - radius, centerY - radius, centerX + radius + 1, centerY + radius + 1);
    SelectObject(dc, white);
    Ellipse(dc, centerX - radius + 1, centerY - radius + 1,
            centerX + radius, centerY + radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(black);
    DeleteObject(white);
}

void paint(ColorState& state)
{
    RECT client{};
    GetClientRect(state.window, &client);
    const int width = rectWidth(client);
    const int height = rectHeight(client);
    ensureCanvas(state, width, height);
    drawAlphaBar(state);
    drawHueBar(state);
    drawColorBox(state);
    drawPreview(state);

    drawMarker(state.canvas, BOX_LEFT + state.boxX, BOX_TOP + state.boxY, 5);
    drawMarker(state.canvas, HUE_LEFT + state.barX, HUE_TOP + HUE_HEIGHT / 2, 7);
    drawMarker(state.canvas, ALPHA_LEFT + ALPHA_WIDTH / 2, ALPHA_TOP + state.barA, 7);

    HDC dc = GetDC(state.window);
    BitBlt(dc, 0, 0, width, height, state.canvas.dc(), 0, 0, SRCCOPY);
    ReleaseDC(state.window, dc);
    updateEditText(state);
}

void clampPoint(const RECT& rect, int& x, int& y)
{
    x = std::clamp<int>(x, static_cast<int>(rect.left), static_cast<int>(rect.right) - 1);
    y = std::clamp<int>(y, static_cast<int>(rect.top), static_cast<int>(rect.bottom) - 1);
}

LRESULT CALLBACK colorProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto* state = reinterpret_cast<ColorState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message)
    {
    case WM_NCCREATE:
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paintStruct{};
        BeginPaint(window, &paintStruct);
        if (state != nullptr && state->canvas.handle() != nullptr)
        {
            RECT client{};
            GetClientRect(window, &client);
            BitBlt(paintStruct.hdc, 0, 0, rectWidth(client), rectHeight(client),
                   state->canvas.dc(), 0, 0, SRCCOPY);
        }
        EndPaint(window, &paintStruct);
        return 0;
    }
    case WM_COMMAND:
        if (state != nullptr && LOWORD(wparam) == 1)
        {
            state->result = state->newColor;
            DestroyWindow(window);
            return 0;
        }
        if (state != nullptr && LOWORD(wparam) == 2)
        {
            DestroyWindow(window);
            return 0;
        }
        if (state != nullptr && LOWORD(wparam) == 3 && HIWORD(wparam) == EN_CHANGE && !state->updatingEdit)
        {
            wchar_t text[32]{};
            GetWindowTextW(state->edit, text, 32);
            std::uint32_t value = 0;
            bool valid = text[0] != L'\0';
            for (const wchar_t* p = text; *p != L'\0'; ++p)
            {
                int digit = 0;
                if (*p >= L'0' && *p <= L'9') digit = *p - L'0';
                else if (*p >= L'a' && *p <= L'f') digit = *p - L'a' + 10;
                else if (*p >= L'A' && *p <= L'F') digit = *p - L'A' + 10;
                else { valid = false; break; }
                value = (value << 4) | static_cast<std::uint32_t>(digit);
            }
            if (valid)
            {
                setFromColor(*state, value, false);
                paint(*state);
            }
            return 0;
        }
        return 0;
    case WM_LBUTTONDOWN:
    {
        if (state == nullptr)
        {
            return 0;
        }
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        SetCapture(window);
        const RECT box = makeRect(BOX_LEFT, BOX_TOP, BOX_SIZE, BOX_SIZE);
        const RECT hue = makeRect(HUE_LEFT, HUE_TOP, HUE_WIDTH, HUE_HEIGHT);
        const RECT alpha = makeRect(ALPHA_LEFT, ALPHA_TOP, ALPHA_WIDTH, ALPHA_HEIGHT);
        const RECT preview = makeRect(PREVIEW_LEFT, PREVIEW_TOP, PREVIEW_WIDTH, PREVIEW_HEIGHT);
        if (pointInRect(box, POINT{x, y}))
        {
            state->dragBox = true;
            state->boxX = std::clamp(x - BOX_LEFT, 0, BOX_SIZE - 1);
            state->boxY = std::clamp(y - BOX_TOP, 0, BOX_SIZE - 1);
            updateFromBox(*state);
            paint(*state);
        }
        else if (pointInRect(hue, POINT{x, y}))
        {
            state->dragBar = true;
            state->barX = std::clamp(x - HUE_LEFT, 0, HUE_WIDTH - 1);
            updateFromBox(*state);
            paint(*state);
        }
        else if (pointInRect(alpha, POINT{x, y}))
        {
            state->dragAlpha = true;
            state->barA = std::clamp(y - ALPHA_TOP, 0, ALPHA_HEIGHT - 1);
            state->newAlpha = static_cast<std::uint8_t>(MulDiv(255, state->barA, ALPHA_HEIGHT - 1));
            state->newColor = makeColor(state->newRgb, state->newAlpha);
            paint(*state);
        }
        else if (pointInRect(preview, POINT{x, y}) && y < PREVIEW_TOP + PREVIEW_HEIGHT / 2)
        {
            setFromColor(*state, state->oldColor, false);
            paint(*state);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (state == nullptr)
        {
            return 0;
        }
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        if (state->dragBox)
        {
            state->boxX = std::clamp(x - BOX_LEFT, 0, BOX_SIZE - 1);
            state->boxY = std::clamp(y - BOX_TOP, 0, BOX_SIZE - 1);
            updateFromBox(*state);
            paint(*state);
        }
        else if (state->dragBar)
        {
            state->barX = std::clamp(x - HUE_LEFT, 0, HUE_WIDTH - 1);
            updateFromBox(*state);
            paint(*state);
        }
        else if (state->dragAlpha)
        {
            state->barA = std::clamp(y - ALPHA_TOP, 0, ALPHA_HEIGHT - 1);
            state->newAlpha = static_cast<std::uint8_t>(MulDiv(255, state->barA, ALPHA_HEIGHT - 1));
            state->newColor = makeColor(state->newRgb, state->newAlpha);
            paint(*state);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (state != nullptr)
        {
            state->dragBox = false;
            state->dragBar = false;
            state->dragAlpha = false;
        }
        ReleaseCapture();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK hexFilterProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept
{
    auto* original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CHAR)
    {
        const wchar_t c = static_cast<wchar_t>(wparam);
        const bool hex = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') ||
                         (c >= L'A' && c <= L'F');
        if (!hex && c != L'\b' && c != 0x7F)
        {
            return 0;
        }
    }
    return CallWindowProcW(original, window, message, wparam, lparam);
}

void centerWindow(HWND window, HWND owner)
{
    RECT rect{};
    GetWindowRect(window, &rect);
    RECT reference{};
    if (owner != nullptr)
    {
        GetWindowRect(owner, &reference);
    }
    else
    {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &reference, 0);
    }
    const int x = reference.left + (rectWidth(reference) - rectWidth(rect)) / 2;
    const int y = reference.top + (rectHeight(reference) - rectHeight(rect)) / 2;
    SetWindowPos(window, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

} // namespace

std::optional<std::uint32_t> showColorDialog(HWND owner, std::uint32_t initialColor)
{
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &colorProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = L"LINKBAR_COLORPICKER";
    RegisterClassW(&windowClass);

    ColorState state;
    state.oldColor = initialColor;
    state.oldRgb = Rgb{static_cast<std::uint8_t>((initialColor >> 16) & 0xFF),
                       static_cast<std::uint8_t>((initialColor >> 8) & 0xFF),
                       static_cast<std::uint8_t>(initialColor & 0xFF)};
    for (int i = 0; i < 138; ++i)
    {
        state.lut[static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(MulDiv(255, i, 137));
    }

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
    RECT desired{0, 0, 201, 243};
    AdjustWindowRectEx(&desired, style, FALSE, exStyle);
    state.window = CreateWindowExW(exStyle, windowClass.lpszClassName,
        l10n(L"Color.Caption", L"Color").c_str(), style | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, rectWidth(desired), rectHeight(desired),
        owner, nullptr, windowClass.hInstance, nullptr);
    if (state.window == nullptr)
    {
        return std::nullopt;
    }
    SetWindowLongPtrW(state.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    SetWindowTextW(state.window, l10n(L"Color.Caption", L"Color").c_str());
    SetWindowPos(state.window, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    centerWindow(state.window, owner);

    const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    state.edit = CreateWindowExW(0, L"Edit", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
                                 ES_CENTER | ES_UPPERCASE,
                                 32, 5, 137, 24, state.window,
                                 reinterpret_cast<HMENU>(3), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(state.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(state.edit, EM_LIMITTEXT, 8, 0);
    WNDPROC originalEditProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(state.edit, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&hexFilterProc)));
    SetWindowLongPtrW(state.edit, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(originalEditProc));

    HWND ok = CreateWindowExW(0, L"Button", l10n(L"Button.Ok", L"OK").c_str(),
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              8, 210, 89, 25, state.window,
                              reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
    HWND cancel = CreateWindowExW(0, L"Button", l10n(L"Button.Cancel", L"Cancel").c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  103, 210, 90, 25, state.window,
                                  reinterpret_cast<HMENU>(2), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    setFromColor(state, initialColor, true);
    paint(state);

    if (owner != nullptr)
    {
        EnableWindow(owner, FALSE);
    }
    SetFocus(state.edit);
    ShowWindow(state.window, SW_SHOW);
    UpdateWindow(state.window);

    MSG message{};
    while (IsWindow(state.window) && GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (IsDialogMessageW(state.window, &message))
        {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (owner != nullptr)
    {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return state.result;
}

} // namespace lb
