#include "IniFile.h"
#include "StringUtil.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace lb {
namespace {

std::wstring readWholeFile(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return {};
    }

    LARGE_INTEGER size{};
    std::wstring content;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (1LL << 30))
    {
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        if (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read > 0)
        {
            bytes.resize(read);
            // Skip UTF-8 BOM if present.
            if (bytes.size() >= 3 &&
                static_cast<unsigned char>(bytes[0]) == 0xEF &&
                static_cast<unsigned char>(bytes[1]) == 0xBB &&
                static_cast<unsigned char>(bytes[2]) == 0xBF)
            {
                bytes.erase(0, 3);
            }
            content = utf8ToWide(bytes);
        }
    }
    CloseHandle(file);
    return content;
}

void ensureParentDirectory(const std::wstring& path)
{
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
    {
        return;
    }
    const std::wstring dir = path.substr(0, slash);
    if (!dir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(dir), ec);
    }
}

bool parseInt(std::wstring_view text, int& out) noexcept
{
    if (text.empty())
    {
        return false;
    }
    int base = 10;
    std::size_t start = 0;
    if (text[0] == L'$')
    {
        base = 16;
        start = 1;
    }
    else if (text.size() > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X'))
    {
        base = 16;
        start = 2;
    }
    try
    {
        std::size_t used = 0;
        const long long value = std::stoll(std::wstring(text.substr(start)), &used, base);
        if (used != text.size() - start)
        {
            return false;
        }
        out = static_cast<int>(value);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace

IniFile::IniFile(std::wstring fileName) : fileName_(std::move(fileName))
{
    const std::wstring content = readWholeFile(fileName_);
    std::wstring section;
    std::size_t pos = 0;

    while (pos < content.size())
    {
        std::size_t end = content.find(L'\n', pos);
        if (end == std::wstring::npos)
        {
            end = content.size();
        }
        std::wstring line = trim(std::wstring_view(content).substr(pos, end - pos));
        pos = end + 1;

        if (line.empty() || line[0] == L';' || line[0] == L'#' ||
            line.rfind(L"//", 0) == 0)
        {
            continue;
        }
        if (line.front() == L'[' && line.back() == L']')
        {
            section = trim(std::wstring_view(line).substr(1, line.size() - 2));
            continue;
        }
        const auto equal = line.find(L'=');
        if (equal == std::wstring::npos)
        {
            continue;
        }
        std::wstring key = trim(std::wstring_view(line).substr(0, equal));
        std::wstring value = trim(std::wstring_view(line).substr(equal + 1));
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
        {
            value = value.substr(1, value.size() - 2);
        }
        if (!key.empty())
        {
            entries_.push_back({section, std::move(key), std::move(value)});
        }
    }
}

void IniFile::flush() const
{
    ensureParentDirectory(fileName_);

    std::wstring text;
    std::wstring lastSection;
    bool haveSection = false;
    for (const Entry& entry : entries_)
    {
        if (!haveSection || !iequals(entry.section, lastSection))
        {
            text += L'[';
            text += entry.section;
            text += L"]\r\n";
            lastSection = entry.section;
            haveSection = true;
        }
        text += entry.key;
        text += L'=';
        text += entry.value;
        text += L"\r\n";
    }

    HANDLE file = CreateFileW(fileName_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }
    const std::string bytes = wideToUtf8(text);
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
}

std::optional<std::wstring> IniFile::find(std::wstring_view section,
                                          std::wstring_view key) const
{
    for (const Entry& entry : entries_)
    {
        if (iequals(entry.section, section) && iequals(entry.key, key))
        {
            return entry.value;
        }
    }
    return std::nullopt;
}

void IniFile::set(std::wstring_view section, std::wstring_view key, std::wstring value)
{
    for (Entry& entry : entries_)
    {
        if (iequals(entry.section, section) && iequals(entry.key, key))
        {
            entry.value = std::move(value);
            return;
        }
    }
    entries_.push_back({std::wstring(section), std::wstring(key), std::move(value)});
}

std::wstring IniFile::readString(std::wstring_view section, std::wstring_view key,
                                 std::wstring_view def) const
{
    const auto value = find(section, key);
    return value ? *value : std::wstring(def);
}

bool IniFile::readBool(std::wstring_view section, std::wstring_view key, bool def) const
{
    const auto value = find(section, key);
    if (!value)
    {
        return def;
    }
    if (iequals(*value, L"true") || *value == L"1" || iequals(*value, L"yes"))
    {
        return true;
    }
    if (iequals(*value, L"false") || *value == L"0" || iequals(*value, L"no"))
    {
        return false;
    }
    return def;
}

int IniFile::readInt(std::wstring_view section, std::wstring_view key, int def) const
{
    const auto value = find(section, key);
    if (!value)
    {
        return def;
    }
    int result = 0;
    return parseInt(*value, result) ? result : def;
}

int IniFile::readInt(std::wstring_view section, std::wstring_view key,
                     int def, int min, int max) const
{
    const int result = readInt(section, key, def);
    return (result > max || result < min) ? def : result;
}

void IniFile::writeString(std::wstring_view section, std::wstring_view key,
                          std::wstring_view value)
{
    set(section, key, std::wstring(value));
}

void IniFile::writeBool(std::wstring_view section, std::wstring_view key, bool value)
{
    set(section, key, value ? L"True" : L"False");
}

void IniFile::writeInt(std::wstring_view section, std::wstring_view key, int value)
{
    set(section, key, format(L"%d", value));
}

void IniFile::writeHex(std::wstring_view section, std::wstring_view key,
                       std::uint32_t value, int digits)
{
    set(section, key, format(L"$%0*X", digits, value));
}

void IniFile::erase(std::wstring_view section, std::wstring_view key)
{
    std::erase_if(entries_, [&](const Entry& e) {
        return iequals(e.section, section) && iequals(e.key, key);
    });
}

bool IniFile::isValid(const std::wstring& fileName)
{
    IniFile ini(fileName);
    const std::wstring dir = ini.readString(INI_SECTION_MAIN, INI_DIR_LINKS, DEF_DIR_LINKS);
    DWORD attr = GetFileAttributesW(dir.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

void writeProfileBool(const std::wstring& fileName, std::wstring_view key, bool value)
{
    IniFile ini(fileName);
    ini.writeBool(INI_SECTION_MAIN, key, value);
    ini.flush();
}

void writeProfileInt(const std::wstring& fileName, std::wstring_view key, int value)
{
    IniFile ini(fileName);
    ini.writeInt(INI_SECTION_MAIN, key, value);
    ini.flush();
}

} // namespace lb
