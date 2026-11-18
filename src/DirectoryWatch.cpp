#include "DirectoryWatch.h"
#include "StringUtil.h"

#include <array>
#include <cstdlib>
#include <cstring>

namespace lb {
namespace {

constexpr UINT WM_DIRWATCH_NOTIFY = WM_USER + 137;
constexpr UINT WM_DIRWATCH_ERROR = WM_USER + 138;

struct Message
{
    DWORD action;
    DWORD error;
    wchar_t* text;
};

} // namespace

DirectoryWatch::~DirectoryWatch()
{
    stop();
}

LRESULT CALLBACK DirectoryWatch::windowProc(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam) noexcept
{
    auto* self = reinterpret_cast<DirectoryWatch*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_DIRWATCH_NOTIFY && self != nullptr)
    {
        auto* data = reinterpret_cast<Message*>(lparam);
        if (self->onChange_)
        {
            self->onChange_(static_cast<WatchAction>(static_cast<int>(data->action) - 1),
                            data->text);
        }
        std::free(data->text);
        std::free(data);
        return 0;
    }
    if (message == WM_DIRWATCH_ERROR && self != nullptr)
    {
        auto* data = reinterpret_cast<Message*>(lparam);
        if (self->onError_)
        {
            self->onError_(data->error, data->text);
        }
        std::free(data->text);
        std::free(data);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void DirectoryWatch::start(const std::wstring& directory,
                           ChangeCallback onChange, ErrorCallback onError)
{
    stop();
    directory_ = directory;
    onChange_ = std::move(onChange);
    onError_ = std::move(onError);

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &DirectoryWatch::windowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"LINKBAR_DIRWATCH_CALLBACK";
    RegisterClassW(&windowClass);
    messageWindow_ = CreateWindowW(windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                   HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
    if (messageWindow_ != nullptr)
    {
        SetWindowLongPtrW(messageWindow_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this] { threadProc(); });
}

void DirectoryWatch::stop() noexcept
{
    if (stopEvent_ != nullptr)
    {
        SetEvent(stopEvent_);
    }
    if (thread_.joinable())
    {
        if (directoryHandle_ != INVALID_HANDLE_VALUE)
        {
            CancelIo(directoryHandle_);
        }
        thread_.join();
    }
    if (directoryHandle_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(directoryHandle_);
        directoryHandle_ = INVALID_HANDLE_VALUE;
    }
    if (stopEvent_ != nullptr)
    {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (messageWindow_ != nullptr)
    {
        // Messages posted by the worker just before the join are never
        // dispatched once the window is destroyed: free their payload here.
        MSG pending{};
        while (PeekMessageW(&pending, messageWindow_, WM_DIRWATCH_NOTIFY, WM_DIRWATCH_ERROR,
                            PM_REMOVE))
        {
            auto* data = reinterpret_cast<Message*>(pending.lParam);
            if (data != nullptr)
            {
                std::free(data->text);
                std::free(data);
            }
        }
        SetWindowLongPtrW(messageWindow_, GWLP_USERDATA, 0);
        DestroyWindow(messageWindow_);
        messageWindow_ = nullptr;
    }
}

void DirectoryWatch::signalError(const std::wstring& message, DWORD code) noexcept
{
    if (code == 0)
    {
        code = GetLastError();
    }
    if (messageWindow_ == nullptr)
    {
        return;
    }
    DWORD length = static_cast<DWORD>(message.size() * sizeof(wchar_t) + sizeof(wchar_t));
    auto* text = static_cast<wchar_t*>(std::malloc(length));
    if (text != nullptr)
    {
        std::memcpy(text, message.c_str(), length);
        auto* data = static_cast<Message*>(std::malloc(sizeof(Message)));
        if (data != nullptr)
        {
            *data = Message{0, code, text};
            if (PostMessageW(messageWindow_, WM_DIRWATCH_ERROR, 0, reinterpret_cast<LPARAM>(data)))
            {
                return;
            }
            std::free(data);
        }
        std::free(text);
    }
}

void DirectoryWatch::threadProc() noexcept
{
    directoryHandle_ = CreateFileW(directory_.c_str(), FILE_LIST_DIRECTORY,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directoryHandle_ == INVALID_HANDLE_VALUE)
    {
        signalError(format(L"Error trying to create file handle for \"%s\".", directory_.c_str()),
                    GetLastError());
        return;
    }

    std::array<std::byte, 64 * 1024> buffer{};
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (overlapped.hEvent == nullptr)
    {
        return;
    }
    HANDLE events[2] = {overlapped.hEvent, stopEvent_};

    while (true)
    {
        DWORD bytes = 0;
        const BOOL started = ReadDirectoryChangesW(
            directoryHandle_, buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytes, &overlapped, nullptr);
        if (!started)
        {
            if (GetLastError() != ERROR_OPERATION_ABORTED)
            {
                signalError(L"Error in watch thread.", GetLastError());
            }
            break;
        }
        const DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0)
        {
            // The kernel may still write into `buffer` and `overlapped`, which live on
            // this thread's stack: cancel the request and wait until it is finished.
            CancelIoEx(directoryHandle_, &overlapped);
            DWORD ignored = 0;
            GetOverlappedResult(directoryHandle_, &overlapped, &ignored, TRUE);
            break;
        }
        if (!GetOverlappedResult(directoryHandle_, &overlapped, &bytes, FALSE))
        {
            if (GetLastError() != ERROR_OPERATION_ABORTED)
            {
                signalError(L"Error in watch thread.", GetLastError());
            }
            break;
        }
        if (bytes == 0)
        {
            signalError(L"Directory change buffer overflow.", ERROR_NOTIFY_ENUM_DIR);
            continue;
        }

        auto* entry = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());
        for (;;)
        {
            const std::wstring name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            const DWORD length = static_cast<DWORD>(name.size() * sizeof(wchar_t) + sizeof(wchar_t));
            auto* text = static_cast<wchar_t*>(std::malloc(length));
            if (text != nullptr)
            {
                std::memcpy(text, name.c_str(), length);
                auto* data = static_cast<Message*>(std::malloc(sizeof(Message)));
                bool posted = false;
                if (data != nullptr && messageWindow_ != nullptr)
                {
                    *data = Message{entry->Action, 0, text};
                    posted = PostMessageW(messageWindow_, WM_DIRWATCH_NOTIFY, 0,
                                          reinterpret_cast<LPARAM>(data)) != FALSE;
                }
                if (!posted)
                {
                    std::free(data);
                    std::free(text);
                }
            }
            if (entry->NextEntryOffset == 0)
            {
                break;
            }
            entry = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<std::byte*>(entry) + entry->NextEntryOffset);
        }
    }
    CloseHandle(overlapped.hEvent);
}

} // namespace lb
