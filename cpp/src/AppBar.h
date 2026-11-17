// Windows AppBar integration: edge registration, QueryPos/SetPos,
// multi-monitor autohide edge reservation and TaskbarCreated handling.

#pragma once

#include "Constants.h"

#include <functional>

namespace lb {

class AppBar final
{
public:
    AppBar(HWND owner, PanelAlign align, bool autoHide);
    ~AppBar();

    AppBar(const AppBar&) = delete;
    AppBar& operator=(const AppBar&) = delete;

    void loaded();

    [[nodiscard]] PanelAlign align() const noexcept { return align_; }
    void setAlign(PanelAlign value);
    [[nodiscard]] bool autoHide() const noexcept { return autoHide_; }
    void setAutoHide(bool value);
    [[nodiscard]] bool stayOnTop() const noexcept { return stayOnTop_; }
    void setStayOnTop(bool value);
    [[nodiscard]] int monitorNum() const noexcept { return monitorNum_; }
    void setMonitorNum(int value);
    [[nodiscard]] bool vertical() const noexcept;

    void appBarPosChanged() { appBarQuerySetPos(); }
    void fullScreenApp(bool enabled);

    std::function<void(bool vertical, int& width, int& height)> querySizing;
    std::function<void(int x, int y, int width, int height)> querySized;
    std::function<void(bool enabled)> queryAutoHide;

private:
    static LRESULT CALLBACK hiddenWindowProc(HWND, UINT, WPARAM, LPARAM) noexcept;
    static LRESULT CALLBACK subclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;

    void registerAppBar();
    void unregisterAppBar();
    void appBarQuerySetPos();
    void appBarSetAutoHide(bool enabled);
    void hiddenSetMonitor(int monitorNum);

    HWND owner_ = nullptr;
    HWND hiddenWindow_ = nullptr;
    PanelAlign align_ = PanelAlign::Top;
    bool autoHide_ = false;
    bool stayOnTop_ = true;
    bool registered_ = false;
    int monitorNum_ = 0;
    int autoHideMonitorNum_ = 0;
    UINT taskbarCreatedMessage_ = 0;
};

} // namespace lb
