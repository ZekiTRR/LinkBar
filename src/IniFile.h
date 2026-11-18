// In-memory INI reader/writer with Delphi TMemIniFile compatible semantics.
// Keys and section names are compared case-insensitively, values are kept
// verbatim. The whole file is loaded once and written back atomically.

#pragma once

#include "Constants.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lb {

class IniFile final
{
public:
    explicit IniFile(std::wstring fileName);
    ~IniFile() = default;

    IniFile(const IniFile&) = delete;
    IniFile& operator=(const IniFile&) = delete;
    IniFile(IniFile&&) noexcept = default;
    IniFile& operator=(IniFile&&) noexcept = default;

    // Writes the in-memory image back to disk. Creates missing directories.
    void flush() const;

    [[nodiscard]] std::wstring readString(std::wstring_view section,
                                          std::wstring_view key,
                                          std::wstring_view def) const;
    [[nodiscard]] bool readBool(std::wstring_view section,
                                std::wstring_view key, bool def) const;
    [[nodiscard]] int readInt(std::wstring_view section,
                              std::wstring_view key, int def) const;
    [[nodiscard]] int readInt(std::wstring_view section, std::wstring_view key,
                              int def, int min, int max) const;

    void writeString(std::wstring_view section, std::wstring_view key,
                     std::wstring_view value);
    void writeBool(std::wstring_view section, std::wstring_view key, bool value);
    void writeInt(std::wstring_view section, std::wstring_view key, int value);
    void writeHex(std::wstring_view section, std::wstring_view key,
                  std::uint32_t value, int digits);

    // Deletes a single key (used by static one-shot writes when needed).
    void erase(std::wstring_view section, std::wstring_view key);

    [[nodiscard]] const std::wstring& fileName() const noexcept { return fileName_; }

    // True when the profile exists and its working directory exists.
    [[nodiscard]] static bool isValid(const std::wstring& fileName);

private:
    struct Entry
    {
        std::wstring section;
        std::wstring key;
        std::wstring value;
    };

    [[nodiscard]] std::optional<std::wstring> find(std::wstring_view section,
                                                   std::wstring_view key) const;
    void set(std::wstring_view section, std::wstring_view key, std::wstring value);

    std::wstring fileName_;
    std::vector<Entry> entries_;
};

// Convenience: write one value to a profile without keeping it open.
void writeProfileBool(const std::wstring& fileName, std::wstring_view key, bool value);
void writeProfileInt(const std::wstring& fileName, std::wstring_view key, int value);

} // namespace lb
