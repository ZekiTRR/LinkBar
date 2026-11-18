// Theme data, taskbar colors, DWM composition policies and all
// themed drawing primitives (GDI + the system GDI+ only).

#pragma once

#include "Constants.h"
#include "DibBitmap.h"

#include <objidl.h>
#include <cstdint>
#include <string>

namespace lb {

struct DrawBackgroundParams
{
    DibBitmap* bitmap = nullptr;
    PanelAlign align = PanelAlign::Top;
    RECT clip{};
    bool isLight = false;
    std::uint32_t backgroundColor = 0;
};

struct LookMode
{
    Look color = Look::Accent;
    TransparencyMode transparency = TransparencyMode::Glass;
};

void themeInitData(HWND window, bool isLight);
void themeCloseData() noexcept;

void themeDrawButton(DibBitmap& bitmap, const RECT& rect, bool pressed);
void themeDrawHover(DibBitmap& bitmap, PanelAlign align, const RECT& rect);
void themeDrawSeparator(DibBitmap& bitmap, PanelAlign align, const RECT& rect);
void themeDrawBackground(const DrawBackgroundParams& params);

[[nodiscard]] std::uint32_t taskbarColor(const LookMode& mode);
[[nodiscard]] std::uint32_t immersiveColor(const std::wstring& name);
[[nodiscard]] std::uint32_t swapRedBlue(std::uint32_t color) noexcept;

void setWindowAttribute78(HWND window);
void setWindowAttribute10(HWND window, TransparencyMode mode, std::uint32_t color);
void setWindowAccentPolicy10(HWND window, TransparencyMode mode, std::uint32_t color);
void updateBlur(HWND window, bool enabled);

// Text with a configurable glow, matching VCL's DrawGlassText.
void drawGlassText(HDC dc, const std::wstring& text, const RECT& rect,
                   UINT flags, int glowSize, std::uint32_t color);

enum class ButtonTextKind : int { Normal = 0, Selected = 1, Pressed = 2 };
[[nodiscard]] std::uint32_t buttonTextColor(ButtonTextKind kind) noexcept;

[[nodiscard]] LOGFONTW titleFont(const LOGFONTW& fallback) noexcept;

[[nodiscard]] bool compositionEnabled() noexcept;

} // namespace lb
