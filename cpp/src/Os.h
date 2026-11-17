// OS version detection and process-level path helpers.

#pragma once

#include "Constants.h"

#include <string>

namespace lb {

struct OsInfo final
{
    DWORD major = 0;
    DWORD minor = 0;
    DWORD build = 0;

    bool vistaOrAbove = false;
    bool sevenOrAbove = false;
    bool eightOrAbove = false;
    bool eightPointOneOrAbove = false;
    bool tenOrAbove = false;

    bool isVista = false;
    bool isSeven = false;
    bool isEight = false;
    bool isEightPointOne = false;
    bool isEightAndEightPointOne = false;
    bool isTen = false;

    bool minimumSupported = false;
    bool jumplistAvailable = false;

    std::wstring versionString;
};

[[nodiscard]] const OsInfo& os() noexcept;

[[nodiscard]] std::wstring exeFileName();
[[nodiscard]] std::wstring exeDirectory();
[[nodiscard]] std::wstring linkbarRoamingFolder();

[[nodiscard]] std::wstring systemInfo();
[[nodiscard]] std::wstring versionResourceString();

// Creates a process with the given application name and full command line.
bool createProcess(const std::wstring& application, const std::wstring& commandLine);

// " /key" or " /key\"value with spaces\"" - matches LBCreateCommandParam.
[[nodiscard]] std::wstring commandParam(std::wstring_view key, std::wstring_view value = {});

} // namespace lb
