// Directory change watcher built directly on ReadDirectoryChangesW.
// Notifications are marshalled to the thread that created the watcher.

#pragma once

#include "Constants.h"

#include <functional>
#include <string>
#include <thread>

namespace lb {

class DirectoryWatch final
{
public:
    using ChangeCallback = std::function<void(WatchAction, const std::wstring&)>;
    using ErrorCallback = std::function<void(DWORD, const std::wstring&)>;

    DirectoryWatch() = default;
    ~DirectoryWatch();

    DirectoryWatch(const DirectoryWatch&) = delete;
    DirectoryWatch& operator=(const DirectoryWatch&) = delete;

    void start(const std::wstring& directory, ChangeCallback onChange, ErrorCallback onError);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;

    void threadProc() noexcept;
    void signalError(const std::wstring& message, DWORD code) noexcept;

    std::wstring directory_;
    ChangeCallback onChange_;
    ErrorCallback onError_;
    std::thread thread_;
    HWND messageWindow_ = nullptr;
    HANDLE directoryHandle_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
};

} // namespace lb
