#include "DarkTheme.h"
#include "Os.h"

namespace lb {
namespace {

using AllowDarkModeForAppFn = BOOL(WINAPI*)(BOOL);
using SetPreferredAppModeFn = UINT(WINAPI*)(UINT);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);

HMODULE themeLibrary = nullptr;
AllowDarkModeForAppFn allowDarkModeForAppFn = nullptr;
SetPreferredAppModeFn setPreferredAppModeFn = nullptr;
AllowDarkModeForWindowFn allowDarkModeForWindowFn = nullptr;
bool supported = false;

} // namespace

void initDarkMode()
{
    if (!os().isTen)
    {
        return;
    }
    themeLibrary = LoadLibraryW(L"uxtheme.dll");
    if (themeLibrary == nullptr)
    {
        return;
    }
    if (os().build < 18362)
    {
        allowDarkModeForAppFn = reinterpret_cast<AllowDarkModeForAppFn>(
            GetProcAddress(themeLibrary, MAKEINTRESOURCEA(135)));
    }
    else
    {
        setPreferredAppModeFn = reinterpret_cast<SetPreferredAppModeFn>(
            GetProcAddress(themeLibrary, MAKEINTRESOURCEA(135)));
    }
    allowDarkModeForWindowFn = reinterpret_cast<AllowDarkModeForWindowFn>(
        GetProcAddress(themeLibrary, MAKEINTRESOURCEA(133)));
    supported = (allowDarkModeForAppFn != nullptr || setPreferredAppModeFn != nullptr) &&
                allowDarkModeForWindowFn != nullptr;
}

void allowDarkModeForApp(bool allow)
{
    if (allowDarkModeForAppFn != nullptr)
    {
        allowDarkModeForAppFn(allow ? TRUE : FALSE);
    }
    else if (setPreferredAppModeFn != nullptr)
    {
        setPreferredAppModeFn(allow ? 1U : 0U); // ALLOW_DARK / DEFAULT
    }
}

void allowDarkModeForWindow(HWND window, bool allow)
{
    if (supported && allowDarkModeForWindowFn != nullptr)
    {
        allowDarkModeForWindowFn(window, allow ? TRUE : FALSE);
    }
}

bool darkModeSupported() noexcept
{
    return supported;
}

} // namespace lb
