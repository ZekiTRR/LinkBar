// Linkbar C++ - Windows desktop toolbar
// Faithful port of the Delphi application, Win32 + GDI + system GDI+ only.

#include "BarWindow.h"
#include "Common.h"
#include "DarkTheme.h"
#include "Dialogs.h"
#include "IniFile.h"
#include "Localization.h"
#include "Os.h"
#include "Shell.h"
#include "StringUtil.h"

#include "GdiplusCompat.h"

#include <algorithm>
#include <cstdio>
#include <vector>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>

namespace lb {
namespace {

std::vector<std::wstring> commandLineArguments()
{
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> result;
    if (arguments != nullptr)
    {
        // Normally argv[0] is the executable. If the process was started with
        // arguments only, the first switch is at argv[0] and must be kept.
        int first = 1;
        if (count > 0)
        {
            const std::wstring_view program(arguments[0]);
            if (!program.empty() && (program.front() == L'-' || program.front() == L'/') &&
                !iequals(PathFindExtensionW(arguments[0]), L".exe"))
            {
                first = 0;
            }
        }
        for (int i = first; i < count; ++i)
        {
            result.emplace_back(arguments[i]);
        }
        LocalFree(arguments);
    }
    return result;
}

bool isSwitch(const std::wstring& argument, std::wstring_view name)
{
    if (argument.size() < 2 || (argument[0] != L'-' && argument[0] != L'/'))
    {
        return false;
    }
    return iequals(std::wstring_view(argument).substr(1), name);
}

bool findSwitch(const std::vector<std::wstring>& arguments, std::wstring_view name,
                std::wstring& value)
{
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        const std::wstring& argument = arguments[i];
        if (argument.size() < 2 || (argument[0] != L'-' && argument[0] != L'/'))
        {
            continue;
        }
        const std::wstring_view text(argument.data() + 1, argument.size() - 1);
        if (iequals(text, name))
        {
            if (i + 1 < arguments.size() && arguments[i + 1][0] != L'-' &&
                arguments[i + 1][0] != L'/')
            {
                value = arguments[i + 1];
            }
            return true;
        }
        if (text.size() > name.size() && iequals(text.substr(0, name.size()), name))
        {
            value = std::wstring(text.substr(name.size()));
            return true;
        }
    }
    return false;
}

std::vector<std::wstring> profileFiles(const std::wstring& directory)
{
    std::vector<std::wstring> result;
    if (GetFileAttributesW(directory.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return result;
    }
    const std::wstring pattern = directory + std::wstring(MASK_LBR);
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        return result;
    }
    do
    {
        result.push_back(directory + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return result;
}

// Keeps an invalid profile recoverable instead of deleting user data.
void quarantineProfile(const std::wstring& file)
{
    std::wstring backup = file + L".bak";
    for (int attempt = 1; attempt < 100 && GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES;
         ++attempt)
    {
        backup = format(L"%s.%d.bak", file.c_str(), attempt);
    }
    if (!MoveFileExW(file.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(file.c_str());
    }
}

} // namespace
} // namespace lb

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    using namespace lb;

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartup(&token, &startupInput, nullptr);

    initDarkMode();
    allowDarkModeForApp(true);

    ComGuard com;
    // Dialogs opened from a posted menu command must be able to take focus
    // even when another application currently owns the foreground.
    AllowSetForegroundWindow(ASFW_ANY);
    const auto arguments = commandLineArguments();
    std::wstring value;

    if (findSwitch(arguments, CLK_DELAY, value))
    {
        try
        {
            std::size_t used = 0;
            const int delay = std::stoi(value, &used);
            if (used == value.size() && delay > 0)
            {
                Sleep(static_cast<DWORD>(delay));
            }
        }
        catch (...) {}
    }

    std::wstring locale;
    findSwitch(arguments, CLK_LANG, locale);
    initializeLocalization(exeDirectory() + std::wstring(DN_LOCALES), locale);

    if (!os().minimumSupported)
    {
        MessageBoxW(nullptr,
            L"Sorry :(\r\nLinkbar doesn't support your operating system.\r\n\r\n"
            L"Minimum supported client: Windows Vista\r\n"
            L"Minimum supported server: Windows Server 2008 R2",
            L"Linkbar", MB_ICONWARNING | MB_OK);
        Gdiplus::GdiplusShutdown(token);
        return 0;
    }

    bool newBar = false;
    bool closeAll = false;
    for (const std::wstring& argument : arguments)
    {
        if (isSwitch(argument, CLK_NEW)) newBar = true;
        if (isSwitch(argument, CLK_CLOSEALL)) closeAll = true;
    }

    if (closeAll)
    {
        // The bars live in their own processes, so WM_CLOSE is only posted and
        // there is nothing left to pump here: returning does not hang.
        BarWindow::closeAll();
        Gdiplus::GdiplusShutdown(token);
        return 0;
    }

    if (newBar)
    {
        int result = 0;
        if (openNewBarDialog(true) != nullptr)
        {
            result = runMessageLoop();
        }
        Gdiplus::GdiplusShutdown(token);
        return result;
    }

    std::wstring settingsFile;
    if (findSwitch(arguments, CLK_FILE, settingsFile) &&
        iequals(PathFindExtensionW(settingsFile.c_str()), std::wstring_view(EXT_LBR)) &&
        GetFileAttributesW(settingsFile.c_str()) != INVALID_FILE_ATTRIBUTES &&
        IniFile::isValid(settingsFile))
    {
        BarWindow::registerWindowClass();
        BarWindow bar(settingsFile);
        const int result = bar.run();
        Gdiplus::GdiplusShutdown(token);
        return result;
    }
    std::vector<std::wstring> files = profileFiles(exeDirectory() + std::wstring(DN_SHARED_BARS));
    const std::vector<std::wstring> userFiles =
        profileFiles(linkbarRoamingFolder() + std::wstring(DN_USER_BARS));
    files.insert(files.end(), userFiles.begin(), userFiles.end());

    int created = 0;
    for (const std::wstring& file : files)
    {
        if (!IniFile::isValid(file))
        {
            quarantineProfile(file);
            continue;
        }
        std::wstring command = commandParam(CLK_FILE, file);
        if (!locale.empty())
        {
            command = commandParam(CLK_LANG, locale) + command;
        }
        if (createProcess(exeFileName(), command))
        {
            ++created;
        }
    }
    if (created == 0)
    {
        createProcess(exeFileName(), commandParam(CLK_NEW));
    }

    Gdiplus::GdiplusShutdown(token);
    return 0;
}
