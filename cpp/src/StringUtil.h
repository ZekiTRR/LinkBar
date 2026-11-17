// Small string helpers built only on WinAPI + the C++ standard library.

#pragma once

#include "Constants.h"

#include <cstdint>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace lb {

[[nodiscard]] std::wstring utf8ToWide(std::string_view text);
[[nodiscard]] std::string wideToUtf8(std::wstring_view text);

// Case-insensitive comparison using Windows' ordinal (culture independent)
// comparison, matching Delphi's CompareText for ANSI key names.
[[nodiscard]] bool iequals(std::wstring_view a, std::wstring_view b) noexcept;
[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept;

[[nodiscard]] std::wstring toLower(std::wstring_view text);
[[nodiscard]] std::wstring trim(std::wstring_view text);

// Format helper with printf-style placeholders: format(L"...", args).
template<typename... Args>
[[nodiscard]] std::wstring format(std::wstring_view fmt, Args&&... args)
{
    const int size = std::swprintf(nullptr, 0, fmt.data(), args...);
    if (size <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    std::swprintf(result.data(), static_cast<std::size_t>(size) + 1, fmt.data(), args...);
    return result;
}

// Removes all '&' accelerator markers from a menu caption.
[[nodiscard]] std::wstring removePrefix(std::wstring_view text);

// Simple 32-bit hash of a file name (case-insensitive), matching the
// purposes of the original StrToHash used for directory-watch tracking.
[[nodiscard]] std::uint32_t strHash(std::wstring_view text) noexcept;

// Expands %ENV% variables and resolves a path relative to the exe folder
// when it starts with ".\".
[[nodiscard]] std::wstring expandPath(std::wstring_view path);

} // namespace lb
