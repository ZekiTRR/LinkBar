// Undocumented uxtheme.dll dark-mode support (ordinal based).

#pragma once

#include "Constants.h"

namespace lb {

void initDarkMode();
void allowDarkModeForApp(bool allow);
void allowDarkModeForWindow(HWND window, bool allow);
[[nodiscard]] bool darkModeSupported() noexcept;

} // namespace lb
