// WinAPI dialogs: Settings, New linkbar and Rename.

#pragma once

#include "Constants.h"

namespace lb {

class BarWindow;

void openSettingsDialog(BarWindow* bar);
void openRenameDialog(BarWindow* owner, PIDLIST_ABSOLUTE pidl);

// Creates the "New linkbar" window. The caller runs the message loop. With
// quitOnClose the window ends that loop when it closes; this is used only by
// the "-n" command-line mode, which runs in its own process.
[[nodiscard]] HWND openNewBarDialog(bool quitOnClose);

// Message loop shared by every window of the process. Keyboard navigation
// (Tab, Enter, Esc) of the open dialogs is handled here.
int runMessageLoop();

// Dialogs (including the modeless color picker) register here so the loop can
// route Tab/Enter/Esc into them.
void registerDialogWindow(HWND window);
void unregisterDialogWindow(HWND window);

} // namespace lb
