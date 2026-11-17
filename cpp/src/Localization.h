// Localization: hand-rolled INI translation files, byte-compatible with
// the original L10n implementation (including the %s fallback guard).

#pragma once

#include "Constants.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace lb {

class Translations final
{
public:
    static Translations& instance();

    void loadFromPath(std::wstring_view path, std::wstring_view locales);
    void loadFromFile(const std::wstring& fileName, std::wstring_view localeName);
    void clear() noexcept;

    [[nodiscard]] std::wstring find(std::wstring_view name,
                                    std::wstring_view defaultValue) const;

    [[nodiscard]] const std::wstring& locale() const noexcept { return locale_; }
    void setLocale(std::wstring_view value) { locale_ = value; }

private:
    Translations() = default;
    std::unordered_map<std::wstring, std::wstring> strings_;
    std::wstring locale_;
};

[[nodiscard]] std::wstring l10n(std::wstring_view name, std::wstring_view defaultValue);

[[nodiscard]] std::wstring loadStringResource(std::wstring_view moduleName, std::uint32_t id);
[[nodiscard]] std::wstring loadStringResource(HMODULE module, std::uint32_t id);

void initializeLocalization(std::wstring_view path, std::wstring_view forcedLocale);

inline constexpr std::wstring_view LB_FN_NEWSHORTCUT = L"shell32.dll";
inline constexpr std::uint32_t LB_RS_NSC_FILENAME = 30397;
inline constexpr std::wstring_view LB_FN_TOOLBAR = L"explorerframe.dll";
inline constexpr std::uint32_t LB_RS_TB_AUTOHIDEALREADYEXISTS = 28676;
inline constexpr std::uint32_t LB_RS_TB_NEWTOOLBAROPENDIALOGTITLE = 12387;
inline constexpr std::uint32_t LB_RS_IFNC_HINT = 4109;
inline constexpr std::uint32_t LB_RS_NFN_CUE = 4123;

} // namespace lb
