// Single source of truth for colors: one packed 0xAARRGGBB value, strict
// parsing, conversion to/from HSV and formatting. No duplicated parsers.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lb {

using Color = std::uint32_t;

constexpr Color makeArgb(int a, int r, int g, int b) noexcept
{
    const auto clampByte = [](int value) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp(value, 0, 255));
    };
    return (clampByte(a) << 24) | (clampByte(r) << 16) | (clampByte(g) << 8) | clampByte(b);
}

constexpr Color makeRgb(int r, int g, int b) noexcept { return makeArgb(255, r, g, b); }

constexpr int colorAlpha(Color c) noexcept { return static_cast<int>(c >> 24); }
constexpr int colorRed(Color c) noexcept { return static_cast<int>((c >> 16) & 0xFF); }
constexpr int colorGreen(Color c) noexcept { return static_cast<int>((c >> 8) & 0xFF); }
constexpr int colorBlue(Color c) noexcept { return static_cast<int>(c & 0xFF); }

constexpr Color withAlpha(Color c, int alpha) noexcept
{
    return makeArgb(alpha, colorRed(c), colorGreen(c), colorBlue(c));
}

struct Hsv
{
    double h = 0.0; // 0..360
    double s = 0.0; // 0..1
    double v = 0.0; // 0..1
};

[[nodiscard]] inline Hsv toHsv(Color color) noexcept
{
    const int r = colorRed(color);
    const int g = colorGreen(color);
    const int b = colorBlue(color);
    const int maximum = std::max({r, g, b});
    const int minimum = std::min({r, g, b});
    Hsv result;
    result.v = maximum / 255.0;
    result.s = maximum == 0 ? 0.0 : static_cast<double>(maximum - minimum) / maximum;
    if (maximum != minimum)
    {
        const double delta = static_cast<double>(maximum - minimum);
        if (maximum == r) result.h = 60.0 * std::fmod((g - b) / delta, 6.0);
        else if (maximum == g) result.h = 60.0 * (((b - r) / delta) + 2.0);
        else result.h = 60.0 * (((r - g) / delta) + 4.0);
        if (result.h < 0.0) result.h += 360.0;
    }
    return result;
}

[[nodiscard]] inline Color fromHsv(double hue, double saturation, double value, int alpha) noexcept
{
    hue = std::fmod(hue, 360.0);
    if (hue < 0.0) hue += 360.0;
    saturation = std::clamp(saturation, 0.0, 1.0);
    value = std::clamp(value, 0.0, 1.0);
    const double chroma = value * saturation;
    const double x = chroma * (1.0 - std::fabs(std::fmod(hue / 60.0, 2.0) - 1.0));
    const double m = value - chroma;
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    if (hue < 60.0) { r = chroma; g = x; }
    else if (hue < 120.0) { r = x; g = chroma; }
    else if (hue < 180.0) { g = chroma; b = x; }
    else if (hue < 240.0) { g = x; b = chroma; }
    else if (hue < 300.0) { r = x; b = chroma; }
    else { r = chroma; b = x; }
    return makeArgb(alpha, static_cast<int>((r + m) * 255.0 + 0.5),
                    static_cast<int>((g + m) * 255.0 + 0.5),
                    static_cast<int>((b + m) * 255.0 + 0.5));
}

inline bool isHexDigit(wchar_t c) noexcept
{
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

inline int hexValue(wchar_t c) noexcept
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    return c - L'A' + 10;
}

inline std::wstring_view trimView(std::wstring_view text) noexcept
{
    constexpr std::wstring_view whitespace = L" \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::wstring_view::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

// Accepts only exactly 6 (RRGGBB) or 8 (AARRGGBB) hex digits, with an optional
// '$', '#' or '0x' prefix. Nothing else is a valid color.
[[nodiscard]] inline std::optional<Color> parseHexColor(std::wstring_view text) noexcept
{
    text = trimView(text);
    if (!text.empty() && (text.front() == L'$' || text.front() == L'#')) text.remove_prefix(1);
    else if (text.size() > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) text.remove_prefix(2);
    if (text.size() != 6 && text.size() != 8)
    {
        return std::nullopt;
    }
    Color value = 0;
    for (const wchar_t c : text)
    {
        if (!isHexDigit(c))
        {
            return std::nullopt;
        }
        value = (value << 4) | static_cast<Color>(hexValue(c));
    }
    if (text.size() == 6)
    {
        value |= 0xFF000000U;
    }
    return value;
}

[[nodiscard]] inline std::wstring toHexColor(Color color, int digits) noexcept
{
    static constexpr wchar_t HEX[] = L"0123456789ABCDEF";
    std::wstring result;
    result.reserve(static_cast<std::size_t>(digits));
    const int start = digits == 6 ? 16 : 24;
    for (int shift = start; shift >= 0; shift -= 4)
    {
        result.push_back(HEX[(color >> shift) & 0xF]);
    }
    return result;
}

} // namespace lb
