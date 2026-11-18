// Small WinAPI utilities shared across the application.

#pragma once

#include "Constants.h"

#include <cstdint>
#include <string>

namespace lb {

[[nodiscard]] POINT makePoint(std::uint32_t lparam) noexcept;
[[nodiscard]] POINT currentMessagePoint() noexcept;
[[nodiscard]] POINT centerPoint(const RECT& rect) noexcept;
[[nodiscard]] int rectWidth(const RECT& rect) noexcept;
[[nodiscard]] int rectHeight(const RECT& rect) noexcept;
[[nodiscard]] bool pointInRect(const RECT& rect, const POINT& point) noexcept;
[[nodiscard]] RECT makeRect(int left, int top, int width, int height) noexcept;
[[nodiscard]] RECT boundsRect(int left, int top, int width, int height) noexcept;

[[nodiscard]] bool isVertical(PanelAlign align) noexcept;
[[nodiscard]] bool isHorizontal(PanelAlign align) noexcept;
[[nodiscard]] UINT screenEdgeToEdge(PanelAlign align) noexcept;

void reduceSysMenu(HWND window) noexcept;
void preventSizing(LPARAM& result) noexcept;

[[nodiscard]] int scaleDimension(int value, unsigned dpi = 0) noexcept;
[[nodiscard]] unsigned systemDpi() noexcept;
[[nodiscard]] unsigned windowDpi(HWND window) noexcept;

void allowMessageThroughUipi(HWND window, UINT message) noexcept;

// "Ctrl+Shift+L"-style caption for a registered hotkey.
[[nodiscard]] std::wstring hotkeyName(std::uint16_t modifiers, std::uint16_t keyCode);

[[nodiscard]] HBITMAP bitmapFromIcon(HICON icon, int size);

// Tries a plain SetForegroundWindow first, then an ALT-key workaround.
[[nodiscard]] HWND setForegroundWindowInternal(HWND window);

// Reliably activates a window without injecting synthetic key presses:
// temporarily attaches to the foreground thread when Windows refuses focus.
void bringToFront(HWND window) noexcept;

} // namespace lb
