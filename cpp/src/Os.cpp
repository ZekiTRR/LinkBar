#include "Os.h"
#include "StringUtil.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <winnls.h>

namespace lb {
namespace {

using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

OsInfo detectOs()
{
    OsInfo result;
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr)
    {
        auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtlGetVersion != nullptr && rtlGetVersion(&version) == 0)
        {
            result.major = version.dwMajorVersion;
            result.minor = version.dwMinorVersion;
            result.build = version.dwBuildNumber;
        }
    }

    result.vistaOrAbove = result.major > 6 || (result.major == 6 && result.minor >= 0);
    result.sevenOrAbove = result.major > 6 || (result.major == 6 && result.minor >= 1);
    result.eightOrAbove = result.major > 6 || (result.major == 6 && result.minor >= 2);
    result.eightPointOneOrAbove = result.major > 6 || (result.major == 6 && result.minor >= 3);
    result.tenOrAbove = result.major >= 10;

    result.isVista = result.vistaOrAbove && !result.sevenOrAbove;
    result.isSeven = result.sevenOrAbove && !result.eightOrAbove;
    result.isEight = result.eightOrAbove && !result.eightPointOneOrAbove;
    result.isEightPointOne = result.eightPointOneOrAbove && !result.tenOrAbove;
    result.isEightAndEightPointOne = result.eightOrAbove && !result.tenOrAbove;
    result.isTen = result.tenOrAbove;
    result.minimumSupported = result.vistaOrAbove;
    result.jumplistAvailable = result.sevenOrAbove;
    result.versionString = versionResourceString();
    return result;
}

} // namespace

const OsInfo& os() noexcept
{
    static const OsInfo info = detectOs();
    return info;
}

std::wstring versionResourceString()
{
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCE(1), RT_VERSION);
    if (resource == nullptr)
    {
        return L"Unknown";
    }
    const DWORD size = SizeofResource(module, resource);
    HGLOBAL loaded = LoadResource(module, resource);
    if (size == 0 || loaded == nullptr)
    {
        return L"Unknown";
    }
    const void* data = LockResource(loaded);
    if (data == nullptr)
    {
        return L"Unknown";
    }
    std::vector<std::byte> copy(static_cast<std::size_t>(size));
    std::memcpy(copy.data(), data, size);

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(copy.data(), L"\\", reinterpret_cast<void**>(&fileInfo), &length) || length == 0)
    {
        return L"Unknown";
    }

    const auto major = HIWORD(fileInfo->dwFileVersionMS);
    const auto minor = LOWORD(fileInfo->dwFileVersionMS);
    const auto release = HIWORD(fileInfo->dwFileVersionLS);
    std::wstring result = format(L"%u.%u.%u", major, minor, release);
    if ((fileInfo->dwFileFlags & VS_FF_DEBUG) != 0)
    {
        result += L"/Debug";
    }
    if ((fileInfo->dwFileFlags & VS_FF_SPECIALBUILD) != 0)
    {
        result += L"/Dev";
    }
    if ((fileInfo->dwFileFlags & VS_FF_PRERELEASE) != 0)
    {
        result += L"/Prerelease";
    }
    if ((fileInfo->dwFileFlags & VS_FF_PRIVATEBUILD) != 0)
    {
        wchar_t* privateBuild = nullptr;
        UINT privateLength = 0;
        if (VerQueryValueW(copy.data(), L"\\StringFileInfo\\040904E4\\PrivateBuild",
                           reinterpret_cast<void**>(&privateBuild), &privateLength) && privateLength > 0)
        {
            result += L"/";
            result.append(privateBuild, privateLength);
        }
    }
    return result;
}

std::wstring exeFileName()
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return {};
    }
    return std::wstring(buffer.data(), length);
}

std::wstring exeDirectory()
{
    const std::wstring exe = exeFileName();
    const auto slash = exe.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : exe.substr(0, slash + 1);
}

std::wstring linkbarRoamingFolder()
{
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &path)))
    {
        std::wstring result(path);
        CoTaskMemFree(path);
        if (!result.empty() && result.back() != L'\\')
        {
            result += L'\\';
        }
        result += L"Linkbar\\";
        return result;
    }
    return exeDirectory();
}

std::wstring systemInfo()
{
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH]{};
    if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) == 0)
    {
        wcscpy_s(localeName, L"unknown");
    }

    wchar_t languageName[256]{};
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SENGLISHDISPLAYNAME,
                    languageName, static_cast<int>(std::size(languageName)));

    const LCID localeId = GetUserDefaultLCID();
    return format(L"%u.%u.%u (%u) %s %s %s", os().major, os().minor, os().build,
                  localeId, localeName, languageName, format(L"%X", localeId).c_str());
}

bool createProcess(const std::wstring& application, const std::wstring& commandLine)
{
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring mutableCommand = commandLine;
    const BOOL ok = CreateProcessW(application.c_str(), mutableCommand.data(), nullptr, nullptr,
                                   FALSE, NORMAL_PRIORITY_CLASS, nullptr, nullptr,
                                   &startup, &process);
    if (!ok)
    {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

std::wstring commandParam(std::wstring_view key, std::wstring_view value)
{
    std::wstring result = L" /";
    result.append(key);
    if (!value.empty())
    {
        const bool quote = value.find(L' ') != std::wstring_view::npos;
        if (quote)
        {
            result += L'"';
        }
        result.append(value);
        if (quote)
        {
            result += L'"';
        }
    }
    return result;
}

} // namespace lb
