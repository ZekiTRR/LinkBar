#include "Theme.h"
#include "Common.h"
#include "Os.h"

#include "GdiplusCompat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <dwmapi.h>
#include <uxtheme.h>

namespace lb {
namespace {

using Gdiplus::Bitmap;
using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::Pen;
using Gdiplus::Rect;
using Gdiplus::SolidBrush;

constexpr int LB_TBP_BUTTON_W7 = 5;
constexpr int LB_TBS_BUTTON_NORMAL_W7 = 3;
constexpr int LB_TBS_BUTTON_PRESSED_W7 = 5;
constexpr int LB_TBS_BUTTON_SELECTED_W7 = 9;

HTHEME hBackground = nullptr;
HTHEME hBackgroundVertical = nullptr;
HTHEME hButton = nullptr;
HTHEME hButtonVertical = nullptr;
std::uint32_t textColors[3] = {0xFF000000U, 0xFF000000U, 0xFF000000U};

// Undocumented APIs, resolved lazily.
struct ColorizationParams
{
    COLORREF color;
    COLORREF afterglow;
    UINT colorBalance;
    UINT afterglowBalance;
    UINT blurBalance;
    UINT glassReflectionIntensity;
    DWORD opaqueBlend;
    DWORD extra;
};
using GetColorizationParamsFn = HRESULT(WINAPI*)(ColorizationParams*);
using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, void*);
using GetImmersiveColorFromColorSetExFn = COLORREF(WINAPI*)(UINT, UINT, BOOL, UINT);
using GetImmersiveColorTypeFromNameFn = int(WINAPI*)(PCWSTR);
using GetImmersiveUserColorSetPreferenceFn = int(WINAPI*)(BOOL, BOOL);

GetColorizationParamsFn getColorizationParams = nullptr;
SetWindowCompositionAttributeFn setWindowCompositionAttribute = nullptr;
GetImmersiveColorFromColorSetExFn getImmersiveColorFromColorSetEx = nullptr;
GetImmersiveColorTypeFromNameFn getImmersiveColorTypeFromName = nullptr;
GetImmersiveUserColorSetPreferenceFn getImmersiveUserColorSetPreference = nullptr;
bool functionsLoaded = false;

struct WcaData
{
    std::size_t attribute;
    void* value;
    ULONG size;
};
struct AccentPolicy
{
    int accentState;
    int accentFlags;
    COLORREF gradientColor;
    int animationId;
};

void loadUndocumentedFunctions()
{
    if (functionsLoaded)
    {
        return;
    }
    functionsLoaded = true;
    if (HMODULE dwmapi = GetModuleHandleW(L"dwmapi.dll"))
    {
        getColorizationParams = reinterpret_cast<GetColorizationParamsFn>(
            GetProcAddress(dwmapi, MAKEINTRESOURCEA(127)));
    }
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll"))
    {
        setWindowCompositionAttribute = reinterpret_cast<SetWindowCompositionAttributeFn>(
            GetProcAddress(user32, "SetWindowCompositionAttribute"));
    }
    if (os().tenOrAbove)
    {
        if (HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll"))
        {
            getImmersiveColorFromColorSetEx = reinterpret_cast<GetImmersiveColorFromColorSetExFn>(
                GetProcAddress(uxtheme, MAKEINTRESOURCEA(95)));
            getImmersiveColorTypeFromName = reinterpret_cast<GetImmersiveColorTypeFromNameFn>(
                GetProcAddress(uxtheme, MAKEINTRESOURCEA(96)));
            if (getImmersiveColorFromColorSetEx && getImmersiveColorTypeFromName)
            {
                getImmersiveUserColorSetPreference =
                    reinterpret_cast<GetImmersiveUserColorSetPreferenceFn>(
                        GetProcAddress(uxtheme, MAKEINTRESOURCEA(98)));
            }
        }
    }
}

std::uint32_t fixAlpha(std::uint32_t color) noexcept
{
    const auto alpha = std::max<std::uint32_t>(1U, color >> 24);
    return (alpha << 24) | (color & 0x00FFFFFFU);
}

constexpr std::array<std::array<int, 2>, 4> SEPARATOR_OFFSETS{{
    {{4, 6}}, {{4, 6}}, {{6, 4}}, {{6, 4}}
}};

constexpr std::array<std::array<int, 2>, 4> BACKGROUND_PARTS{{
    {{4, 3}}, {{3, 2}}, {{2, 4}}, {{1, 1}}
}};

constexpr std::array<const wchar_t*, 4> JL_IMMERSIVE_NAMES{
    L"ImmersiveSystemAccent", L"ImmersiveSystemAccentLight2",
    L"ImmersiveSystemAccentLight3", L"ImmersiveSystemAccent"
};

} // namespace

bool compositionEnabled() noexcept
{
    if (!os().vistaOrAbove)
    {
        return false;
    }
    BOOL enabled = FALSE;
    return SUCCEEDED(DwmIsCompositionEnabled(&enabled)) && enabled != FALSE;
}

std::uint32_t swapRedBlue(std::uint32_t color) noexcept
{
    return (color & 0xFF00FF00U) | ((color & 0xFFU) << 16) | ((color >> 16) & 0xFFU);
}

void themeInitData(HWND window, bool isLight)
{
    themeCloseData();
    if (!os().minimumSupported || window == nullptr)
    {
        return;
    }
    if (!IsThemeActive())
    {
        return;
    }
    const bool composed = compositionEnabled();

    if (os().isVista)
    {
        if (composed)
        {
            hBackground = OpenThemeData(window, L"TaskBarComposited::TaskBar");
            hButton = OpenThemeData(window, L"TaskBandComposited::Toolbar");
        }
        else
        {
            hBackground = OpenThemeData(window, L"TaskBar::TaskBar");
            hButton = OpenThemeData(window, L"TaskBand::Toolbar");
        }
        textColors[0] = textColors[1] = textColors[2] = 0xFFFFFFFFU;
        return;
    }

    if (os().eightOrAbove)
    {
        isLight = false;
    }
    if (composed)
    {
        if (isLight)
        {
            hBackground = OpenThemeData(window, L"TaskBand2Composited::TaskBand2");
            hBackgroundVertical = OpenThemeData(window, L"TaskBand2CompositedVertical::TaskBand2");
        }
        else
        {
            hBackground = OpenThemeData(window, L"TaskBar2Composited::TaskBar");
            hBackgroundVertical = hBackground;
        }
        hButton = OpenThemeData(window, L"TaskBand2Composited::TaskBand2");
        hButtonVertical = OpenThemeData(window, L"TaskBand2CompositedVertical::TaskBand2");
    }
    else
    {
        hBackground = OpenThemeData(window, isLight ? L"TaskBand2::TaskBand2" : L"TaskBar2::TaskBar");
        hButton = OpenThemeData(window, L"TaskBand2::TaskBand2");
        hButtonVertical = OpenThemeData(window, L"TaskBand2Vertical::TaskBand2");
    }

    COLORREF color = 0;
    textColors[0] = SUCCEEDED(GetThemeColor(hButton, LB_TBP_BUTTON_W7, LB_TBS_BUTTON_SELECTED_W7,
                                            3803, &color)) ? color : 0xFF000000U;
    textColors[1] = SUCCEEDED(GetThemeColor(hButton, LB_TBP_BUTTON_W7, LB_TBS_BUTTON_NORMAL_W7,
                                            3803, &color)) ? color : 0xFF000000U;
    textColors[2] = SUCCEEDED(GetThemeColor(hButton, LB_TBP_BUTTON_W7, LB_TBS_BUTTON_PRESSED_W7,
                                            3803, &color)) ? color : 0xFF000000U;
}

void themeCloseData() noexcept
{
    if (hBackground != nullptr) { CloseThemeData(hBackground); hBackground = nullptr; }
    if (hBackgroundVertical != nullptr && hBackgroundVertical != hBackground)
    {
        CloseThemeData(hBackgroundVertical);
    }
    hBackgroundVertical = nullptr;
    if (hButton != nullptr) { CloseThemeData(hButton); hButton = nullptr; }
    if (hButtonVertical != nullptr) { CloseThemeData(hButtonVertical); hButtonVertical = nullptr; }
}

std::uint32_t immersiveColor(const std::wstring& name)
{
    loadUndocumentedFunctions();
    if (getImmersiveColorFromColorSetEx == nullptr || getImmersiveColorTypeFromName == nullptr ||
        getImmersiveUserColorSetPreference == nullptr)
    {
        return 0;
    }
    const int type = getImmersiveColorTypeFromName(name.c_str());
    if (type < 0)
    {
        return 0;
    }
    const int set = getImmersiveUserColorSetPreference(FALSE, FALSE);
    return getImmersiveColorFromColorSetEx(static_cast<UINT>(set), static_cast<UINT>(type), TRUE, 0);
}

std::uint32_t taskbarColor(const LookMode& mode)
{
    if (os().isSeven)
    {
        return 0;
    }
    if (os().isEightPointOne)
    {
        loadUndocumentedFunctions();
        if (getColorizationParams == nullptr)
        {
            return 0;
        }
        ColorizationParams params{};
        if (FAILED(getColorizationParams(&params)))
        {
            return 0;
        }
        const int r = (params.color >> 16) & 0xFF;
        const int g = (params.color >> 8) & 0xFF;
        const int b = params.color & 0xFF;
        const int balance = static_cast<int>(params.colorBalance);
        const int gray = 217 * (100 - balance) + 50;
        const auto mix = [balance, gray](int value) {
            return static_cast<std::uint32_t>((value * balance + gray) / 100);
        };
        auto alpha = static_cast<std::uint32_t>((params.color >> 24) & 0xFF);
        alpha = alpha * 65 / 100;
        return (mix(r) << 16) | (mix(g) << 8) | mix(b) | (alpha << 24);
    }
    if (os().isTen)
    {
        if (mode.color == Look::Custom)
        {
            return 0;
        }
        const bool transparent = mode.transparency != TransparencyMode::Opaque;
        const wchar_t* name = nullptr;
        switch (mode.color)
        {
        case Look::Accent:
            name = transparent ? L"ImmersiveSystemAccentDark3" : L"ImmersiveSystemAccentDark2";
            break;
        case Look::Light:
            name = L"ImmersiveLightChromeTaskbarBase";
            break;
        case Look::Dark:
            name = L"ImmersiveDarkChromeTaskbarBase";
            break;
        default:
            break;
        }
        if (name == nullptr)
        {
            return 0;
        }
        auto result = swapRedBlue(immersiveColor(name));
        if (transparent)
        {
            result = (result & 0x00FFFFFFU) | (198U << 24);
        }
        return result;
    }
    return 0;
}

void setWindowAccentPolicy10(HWND window, TransparencyMode mode, std::uint32_t color)
{
    loadUndocumentedFunctions();
    if (setWindowCompositionAttribute == nullptr)
    {
        return;
    }
    constexpr std::array<int, 4> states{1, 2, 4, 0};
    AccentPolicy policy{};
    policy.accentState = states[static_cast<std::size_t>(mode)];
    policy.accentFlags = 0x13;
    policy.gradientColor = static_cast<COLORREF>(swapRedBlue(color));
    policy.animationId = 0;
    WcaData data{19, &policy, static_cast<ULONG>(sizeof(policy))};
    setWindowCompositionAttribute(window, &data);
}

void setWindowAttribute10(HWND window, TransparencyMode mode, std::uint32_t color)
{
    BOOL exclude = TRUE;
    DwmSetWindowAttribute(window, DWMWA_EXCLUDED_FROM_PEEK, &exclude, sizeof(exclude));
    int flip = DWMFLIP3D_EXCLUDEABOVE;
    DwmSetWindowAttribute(window, DWMWA_FLIP3D_POLICY, &flip, sizeof(flip));
    setWindowAccentPolicy10(window, mode, color);
}

void setWindowAttribute78(HWND window)
{
    BOOL exclude = TRUE;
    DwmSetWindowAttribute(window, DWMWA_EXCLUDED_FROM_PEEK, &exclude, sizeof(exclude));
    int flip = DWMFLIP3D_EXCLUDEBELOW;
    DwmSetWindowAttribute(window, DWMWA_FLIP3D_POLICY, &flip, sizeof(flip));
    if (os().isEightAndEightPointOne && !globals().aeroGlassEnabled)
    {
        return;
    }
    BOOL policy = TRUE;
    DwmSetWindowAttribute(window, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    loadUndocumentedFunctions();
    if (setWindowCompositionAttribute != nullptr)
    {
        WcaData data{os().eightOrAbove ? 15U : 16U, &policy, sizeof(policy)};
        setWindowCompositionAttribute(window, &data);
    }
}

void updateBlur(HWND window, bool enabled)
{
    if (os().tenOrAbove || !compositionEnabled())
    {
        return;
    }
    DWM_BLURBEHIND blur{};
    blur.dwFlags = DWM_BB_ENABLE;
    blur.fEnable = enabled ? TRUE : FALSE;
    if (enabled)
    {
        RECT rect{};
        if (GetWindowRect(window, &rect))
        {
            blur.dwFlags |= DWM_BB_BLURREGION;
            blur.hRgnBlur = CreateRectRgn(0, 0, rectWidth(rect), rectHeight(rect));
        }
        if (os().isVista)
        {
            blur.dwFlags |= DWM_BB_TRANSITIONONMAXIMIZED;
            blur.fTransitionOnMaximized = TRUE;
        }
    }
    DwmEnableBlurBehindWindow(window, &blur);
    if (blur.hRgnBlur != nullptr)
    {
        DeleteObject(blur.hRgnBlur);
    }
}

void themeDrawButton(DibBitmap& bitmap, const RECT& rect, bool pressed)
{
    HDC dc = bitmap.dc();
    if (os().isTen)
    {
        Graphics graphics(dc);
        const auto alpha = static_cast<std::uint32_t>(std::trunc(255.0 * 0.10)) << 24;
        const auto color = alpha | (globals().look == Look::Light ? 0x00000000U : 0x00FFFFFFU);
        SolidBrush brush{Color(color)};
        graphics.FillRectangle(&brush, static_cast<Gdiplus::REAL>(rect.left),
                               static_cast<Gdiplus::REAL>(rect.top),
                               static_cast<Gdiplus::REAL>(rectWidth(rect)),
                               static_cast<Gdiplus::REAL>(rectHeight(rect)));
        return;
    }
    if (IsThemeActive() && hButton != nullptr)
    {
        RECT paint = rect;
        HTHEME theme = hButton;
        int part = 1;
        int state = pressed ? 3 : 0;
        if (!os().isVista)
        {
            part = LB_TBP_BUTTON_W7;
            state = pressed ? LB_TBS_BUTTON_PRESSED_W7 : LB_TBS_BUTTON_NORMAL_W7;
        }
        DrawThemeBackground(theme, dc, part, state, &paint, &paint);
        return;
    }
    UINT flags = DFCS_BUTTONPUSH | (pressed ? DFCS_PUSHED : DFCS_HOT);
    DrawFrameControl(dc, const_cast<RECT*>(&rect), DFC_BUTTON, flags);
    bitmap.makeOpaqueRect(rect);
}

void themeDrawHover(DibBitmap& bitmap, PanelAlign align, const RECT& rect)
{
    if (os().isTen)
    {
        themeDrawButton(bitmap, rect, false);
        return;
    }
    if (IsThemeActive() && hButton != nullptr)
    {
        RECT paint = rect;
        HTHEME theme = hButton;
        int part = 1;
        int state = 3;
        if (!os().isVista)
        {
            part = LB_TBP_BUTTON_W7;
            state = LB_TBS_BUTTON_SELECTED_W7;
            theme = isVertical(align) ? hButtonVertical : hButton;
        }
        DrawThemeBackground(theme, bitmap.dc(), part, state, &paint, &paint);
        return;
    }
    UINT flags = DFCS_BUTTONPUSH | DFCS_HOT;
    DrawFrameControl(bitmap.dc(), const_cast<RECT*>(&rect), DFC_BUTTON, flags);
    bitmap.makeOpaqueRect(rect);
}

void themeDrawSeparator(DibBitmap& bitmap, PanelAlign align, const RECT& rect)
{
    HDC dc = bitmap.dc();
    const auto offsets = SEPARATOR_OFFSETS[static_cast<std::size_t>(align)];
    if (!os().vistaOrAbove || !IsThemeActive())
    {
        RECT r = rect;
        if (isHorizontal(align))
        {
            r.top = rect.top + (rectHeight(rect) - 3) / 2;
            r.bottom = r.top + 3;
            r.left = rect.left + offsets[0];
            r.right = rect.right - offsets[1];
        }
        else
        {
            r.left = rect.left + (rectWidth(rect) - 3) / 2;
            r.right = r.left + 3;
            r.top = rect.top + offsets[0];
            r.bottom = rect.bottom - offsets[1];
        }
        DrawEdge(dc, &r, BDR_RAISEDINNER, BF_RECT);
        bitmap.makeOpaqueRect(r);
        return;
    }
    if (os().isTen)
    {
        const auto w = scaleDimension(2);
        const auto h = scaleDimension(4);
        RECT r;
        if (isHorizontal(align))
        {
            r = makeRect(rect.left + h, rect.top + (rectHeight(rect) - w) / 2,
                         rectWidth(rect) - 2 * h, w);
        }
        else
        {
            r = makeRect(rect.left + (rectWidth(rect) - w) / 2, rect.top + h,
                         w, rectHeight(rect) - 2 * h);
        }
        const auto color = swapRedBlue(
            immersiveColor(JL_IMMERSIVE_NAMES[static_cast<std::size_t>(globals().look)]));
        Graphics graphics(dc);
        SolidBrush brush{Color(color)};
        graphics.FillRectangle(&brush, static_cast<Gdiplus::REAL>(r.left),
                               static_cast<Gdiplus::REAL>(r.top),
                               static_cast<Gdiplus::REAL>(rectWidth(r)),
                               static_cast<Gdiplus::REAL>(rectHeight(r)));
        return;
    }
    const auto color1 = (static_cast<std::uint32_t>(std::trunc(255.0 * 0.15)) << 24);
    const auto color2 = (static_cast<std::uint32_t>(std::trunc(255.0 * 0.11)) << 24) | 0x00FFFFFFU;
    Graphics graphics(dc);
    Pen pen{Color(color1), 1.0f};
    if (isHorizontal(align))
    {
        const int left = rect.left + offsets[0];
        const int right = rect.right - offsets[1];
        int top = rect.top + (rectHeight(rect) - 2) / 2;
        graphics.DrawLine(&pen, left, top, right, top);
        pen.SetColor(Color(color2));
        ++top;
        graphics.DrawLine(&pen, left, top, right, top);
    }
    else
    {
        const int top = rect.top + offsets[0];
        const int bottom = rect.bottom - offsets[1];
        int left = rect.left + (rectWidth(rect) - 2) / 2;
        graphics.DrawLine(&pen, left, top, left, bottom);
        pen.SetColor(Color(color2));
        ++left;
        graphics.DrawLine(&pen, left, top, left, bottom);
    }
}

void themeDrawBackground(const DrawBackgroundParams& params)
{
    DibBitmap* bitmap = params.bitmap;
    if (bitmap == nullptr)
    {
        return;
    }
    HDC dc = bitmap->dc();
    if (os().isTen)
    {
        Graphics graphics(dc);
        graphics.SetClip(Rect(params.clip.left, params.clip.top,
                              rectWidth(params.clip), rectHeight(params.clip)));
        graphics.Clear(Color(0x01000000U));
        return;
    }
    if (!os().vistaOrAbove || !IsThemeActive())
    {
        FillRect(dc, &params.clip, reinterpret_cast<HBRUSH>(GetSysColorBrush(COLOR_3DFACE)));
        constexpr std::array<UINT, 4> flags{{BF_RIGHT, BF_BOTTOM, BF_LEFT, BF_TOP}};
        DrawEdge(dc, const_cast<RECT*>(&params.clip), BDR_RAISED,
                 flags[static_cast<std::size_t>(params.align)]);
        bitmap->makeOpaqueRect(params.clip);
        return;
    }
    Graphics graphics(dc);
    graphics.SetClip(Rect(params.clip.left, params.clip.top,
                          rectWidth(params.clip), rectHeight(params.clip)));
    std::uint32_t clearColor = 0x01000000U;
    HTHEME theme = hBackground;
    if (os().sevenOrAbove && compositionEnabled())
    {
        clearColor = fixAlpha(params.backgroundColor);
        if (isVertical(params.align) && hBackgroundVertical != nullptr)
        {
            theme = hBackgroundVertical;
        }
    }
    graphics.Clear(Color(clearColor));
    if (theme != nullptr)
    {
        const int part = BACKGROUND_PARTS[static_cast<std::size_t>(params.align)]
                                         [params.isLight ? 1 : 0];
        RECT full = bitmap->bound();
        DrawThemeBackground(theme, dc, part, 0, &full, &params.clip);
    }
}

void drawGlassText(HDC dc, const std::wstring& text, const RECT& rect,
                   UINT flags, int glowSize, std::uint32_t color)
{
    HTHEME theme = OpenThemeData(nullptr, L"TEXTSTYLE");
    if (theme != nullptr)
    {
        DTTOPTS options{};
        options.dwSize = sizeof(options);
        options.dwFlags = DTT_TEXTCOLOR | DTT_GLOWSIZE | DTT_COMPOSITED;
        options.crText = static_cast<COLORREF>(color & 0x00FFFFFFU);
        options.iGlowSize = glowSize;
        RECT paint = rect;
        const HRESULT result = DrawThemeTextEx(theme, dc, 1, 1, text.c_str(),
                                               static_cast<int>(text.size()), flags,
                                               &paint, &options);
        CloseThemeData(theme);
        if (SUCCEEDED(result))
        {
            return;
        }
    }
    // Fallback: simple shadowed text.
    SetTextColor(dc, 0x00000000);
    RECT shadow = rect;
    OffsetRect(&shadow, 1, 1);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &shadow, flags);
    SetTextColor(dc, static_cast<COLORREF>(color & 0x00FFFFFFU));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), const_cast<RECT*>(&rect), flags);
}

std::uint32_t buttonTextColor(ButtonTextKind kind) noexcept
{
    return textColors[static_cast<std::size_t>(kind)];
}

LOGFONTW titleFont(const LOGFONTW& fallback) noexcept
{
    LOGFONTW result = fallback;
    HTHEME theme = OpenThemeData(nullptr, L"TEXTSTYLE");
    if (theme == nullptr)
    {
        return result;
    }
    COLORREF color = 0;
    GetThemeColor(theme, 1, 1, 3803, &color);
    LOGFONTW font{};
    if (SUCCEEDED(GetThemeFont(theme, nullptr, 1, 1, 210, &font)))
    {
        result = font;
        result.lfWeight = font.lfWeight >= FW_BOLD ? FW_BOLD : FW_NORMAL;
        result.lfItalic = static_cast<BYTE>(font.lfItalic > 1);
        result.lfUnderline = static_cast<BYTE>(font.lfUnderline > 1);
        result.lfStrikeOut = static_cast<BYTE>(font.lfStrikeOut > 1);
    }
    CloseThemeData(theme);
    return result;
}

} // namespace lb
