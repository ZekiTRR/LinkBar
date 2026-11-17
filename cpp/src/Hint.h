// Native comctl32 tracking tooltip (no custom painting).

#pragma once

#include "Constants.h"

#include <commctrl.h>
#include <string>

namespace lb {

enum class HorzAlign : int { Left = 0, Right = 1, Center = 2 };
enum class VertAlign : int { Top = 0, Bottom = 1, Center = 2 };

class Tooltip final
{
public:
    explicit Tooltip(HWND parent);
    ~Tooltip();

    Tooltip(const Tooltip&) = delete;
    Tooltip& operator=(const Tooltip&) = delete;

    void activate(const POINT& position, const std::wstring& text,
                  HorzAlign horizontal, VertAlign vertical);
    void cancel() noexcept;
    void hide() noexcept;
    void themeChanged() noexcept;

private:
    HWND window_ = nullptr;
    TOOLINFOW tool_{};
};

} // namespace lb
