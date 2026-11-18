#include "StringUtil.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cwctype>

namespace lb {

std::wstring utf8ToWide(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    if (size <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string wideToUtf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                         text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

bool iequals(std::wstring_view a, std::wstring_view b) noexcept
{
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
                                b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool iequals(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const auto left = static_cast<unsigned char>(a[i]);
        const auto right = static_cast<unsigned char>(b[i]);
        if (std::tolower(left) != std::tolower(right))
        {
            return false;
        }
    }
    return true;
}

std::wstring toLower(std::wstring_view text)
{
    std::wstring result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return result;
}

std::wstring trim(std::wstring_view text)
{
    constexpr std::wstring_view whitespace = L" \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::wstring_view::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(whitespace);
    return std::wstring(text.substr(first, last - first + 1));
}

std::optional<long long> parseInteger(std::wstring_view text, int base) noexcept
{
    bool negative = false;
    if (!text.empty() && (text.front() == L'-' || text.front() == L'+'))
    {
        negative = text.front() == L'-';
        text.remove_prefix(1);
    }
    if (text.empty())
    {
        return std::nullopt;
    }
    long long value = 0;
    for (const wchar_t c : text)
    {
        int digit = -1;
        if (c >= L'0' && c <= L'9') digit = c - L'0';
        else if (c >= L'a' && c <= L'f') digit = c - L'a' + 10;
        else if (c >= L'A' && c <= L'F') digit = c - L'A' + 10;
        if (digit < 0 || digit >= base)
        {
            return std::nullopt;
        }
        if (value > (1LL << 40))
        {
            return std::nullopt;
        }
        value = value * base + digit;
    }
    return negative ? -value : value;
}

std::uint32_t strHash(std::wstring_view text) noexcept
{
    // FNV-1a over the upper-cased UTF-16LE bytes of the string (the original
    // passes AnsiUpperCase to CalcFNVHashFromString, which hashes UTF-16 bytes).
    std::uint32_t hash = 2166136261U;
    for (const wchar_t raw : text)
    {
        const auto c = static_cast<std::uint16_t>(std::towupper(raw));
        hash = (hash ^ static_cast<std::uint8_t>(c & 0xFFU)) * 16777619U;
        hash = (hash ^ static_cast<std::uint8_t>(c >> 8)) * 16777619U;
    }
    return hash;
}

std::wstring expandPath(std::wstring_view path)
{
    std::wstring result(path);
    if (result.size() >= 2 && result[0] == L'.' && (result[1] == L'\\' || result[1] == L'/'))
    {
        std::array<wchar_t, MAX_PATH> exe{};
        const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length > 0 && length < exe.size())
        {
            std::wstring dir(exe.data(), length);
            const auto slash = dir.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
            {
                dir.resize(slash + 1);
                result = dir + result.substr(2);
            }
        }
    }
    return result;
}

std::vector<char> readFileBytes(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return {};
    }
    std::vector<char> bytes;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (1LL << 30))
    {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
        {
            bytes.clear();
        }
        bytes.resize(read);
    }
    CloseHandle(file);
    return bytes;
}

} // namespace lb
