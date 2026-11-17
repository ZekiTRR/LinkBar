#include "Localization.h"
#include "StringUtil.h"

#include <algorithm>
#include <fstream>
#include <winnls.h>

#ifndef LANGUAGE_UI
#define LANGUAGE_UI 0x00000002
#endif
#include <vector>

namespace lb {
namespace {

std::vector<std::wstring> splitString(std::wstring_view text, wchar_t separator)
{
    std::vector<std::wstring> result;
    std::size_t start = 0;
    for (;;)
    {
        const auto pos = text.find(separator, start);
        if (pos == std::wstring_view::npos)
        {
            result.emplace_back(text.substr(start));
            break;
        }
        result.emplace_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return result;
}

std::wstring preferredUILanguages()
{
    using GetPreferredUILanguagesFn = BOOL(WINAPI*)(DWORD, PULONG, PWSTR, PULONG);
    auto getPreferred = reinterpret_cast<GetPreferredUILanguagesFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetPreferredUILanguages"));
    if (getPreferred == nullptr)
    {
        return {};
    }
    ULONG count = 0;
    ULONG size = 0;
    if (!getPreferred(LANGUAGE_UI, &count, nullptr, &size))
    {
        return {};
    }
    std::vector<wchar_t> buffer(size);
    if (!getPreferred(LANGUAGE_UI, &count, buffer.data(), &size))
    {
        return {};
    }
    std::wstring result;
    std::size_t pos = 0;
    for (ULONG i = 0; i < count; ++i)
    {
        const std::wstring_view name(buffer.data() + pos);
        if (i != 0)
        {
            result += L',';
        }
        result.append(name);
        pos += name.size() + 1;
    }
    return result;
}

std::wstring localeNameFromId(std::wstring_view text)
{
    int id = -1;
    try
    {
        std::size_t used = 0;
        id = std::stoi(std::wstring(text), &used, 10);
        if (used != text.size())
        {
            id = -1;
        }
    }
    catch (...)
    {
        id = -1;
    }
    if (id < 0)
    {
        return std::wstring(text);
    }
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if (LCIDToLocaleName(static_cast<LCID>(id), name, LOCALE_NAME_MAX_LENGTH, 0) > 0)
    {
        return std::wstring(name);
    }
    return {};
}

} // namespace

Translations& Translations::instance()
{
    static Translations translations;
    return translations;
}

void Translations::clear() noexcept
{
    strings_.clear();
}

void Translations::loadFromFile(const std::wstring& fileName, std::wstring_view localeName)
{
    std::ifstream file(fileName, std::ios::binary);
    if (!file)
    {
        return;
    }
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        bytes.erase(0, 3);
    }
    const std::wstring content = utf8ToWide(bytes);

    bool inSection = false;
    std::size_t pos = 0;
    while (pos < content.size())
    {
        std::size_t end = content.find(L'\n', pos);
        if (end == std::wstring::npos)
        {
            end = content.size();
        }
        std::wstring line(content, pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }

        if (!inSection)
        {
            // Header: "[locale] - description". Compare the characters after '['.
            if (!line.empty() && line.front() == L'[' && line.size() > 1)
            {
                const std::wstring_view candidate(line.data() + 1, line.size() - 1);
                const auto nameLength = std::min(candidate.size(), localeName.size());
                inSection = iequals(candidate.substr(0, nameLength), localeName.substr(0, nameLength)) &&
                            candidate.size() >= localeName.size();
            }
            continue;
        }

        if (line.empty() || line.front() == L'[')
        {
            break;
        }
        const auto equal = line.find(L'=');
        if (equal == std::wstring::npos || equal == 0)
        {
            continue;
        }
        std::wstring key = toLower(trim(std::wstring_view(line).substr(0, equal)));
        const std::size_t first = line.find_first_not_of(L" \t\r\n", equal + 1);
        if (first == std::wstring::npos)
        {
            continue;
        }
        const std::wstring value = line.substr(first); // TrimLeft only, trailing space kept
        if (!key.empty() && !value.empty())
        {
            strings_[std::move(key)] = value;
        }
    }
}

void Translations::loadFromPath(std::wstring_view path, std::wstring_view locales)
{
    const auto languages = splitString(locales, L',');
    // Reverse order: first language in the list has the highest priority.
    for (auto it = languages.rbegin(); it != languages.rend(); ++it)
    {
        if (it->empty())
        {
            continue;
        }
        std::wstring fileName(path);
        fileName += *it;
        fileName += L".ini";
        loadFromFile(fileName, *it);
    }
}

std::wstring Translations::find(std::wstring_view name, std::wstring_view defaultValue) const
{
    const auto it = strings_.find(std::wstring(toLower(name)));
    std::wstring result = (it != strings_.end()) ? it->second : std::wstring(defaultValue);
    if (defaultValue.find(L"%s") != std::wstring_view::npos &&
        toLower(result).find(L"%s") == std::wstring::npos)
    {
        return std::wstring(defaultValue);
    }
    return result;
}

std::wstring l10n(std::wstring_view name, std::wstring_view defaultValue)
{
    return Translations::instance().find(name, defaultValue);
}

std::wstring loadStringResource(HMODULE module, std::uint32_t id)
{
    if (module == nullptr || id >= 65536)
    {
        return L"resource_string_not_found";
    }
    std::array<wchar_t, 1024> buffer{};
    const int length = LoadStringW(module, static_cast<UINT>(id), buffer.data(),
                                   static_cast<int>(buffer.size()));
    if (length <= 0)
    {
        return L"resource_string_not_found";
    }
    return std::wstring(buffer.data(), static_cast<std::size_t>(length));
}

std::wstring loadStringResource(std::wstring_view moduleName, std::uint32_t id)
{
    HMODULE module = LoadLibraryExW(std::wstring(moduleName).c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
    const std::wstring result = loadStringResource(module, id);
    if (module != nullptr)
    {
        FreeLibrary(module);
    }
    return result;
}

void initializeLocalization(std::wstring_view path, std::wstring_view forcedLocale)
{
    auto& translations = Translations::instance();
    translations.clear();
    translations.setLocale(forcedLocale);
    std::wstring languages;
    if (forcedLocale.empty())
    {
        languages = preferredUILanguages();
    }
    else
    {
        languages = localeNameFromId(forcedLocale);
    }
    translations.loadFromPath(path, languages);
}

} // namespace lb
