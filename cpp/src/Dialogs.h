// WinAPI dialogs: Settings, New Linkbar, Rename, Color picker, Hotkey edit.

#pragma once

#include "Constants.h"

namespace lb {

class BarWindow;

void openSettingsDialog(BarWindow* bar);
[[nodiscard]] HWND openNewBarDialog(HWND owner);
void openRenameDialog(HWND owner, PIDLIST_ABSOLUTE pidl);

} // namespace lb
