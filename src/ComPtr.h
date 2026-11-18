// Minimal intrusive smart pointer for COM interfaces (RAII reference counting).

#pragma once

#include <utility>

namespace lb {

template<typename T>
class ComPtr final
{
public:
    ComPtr() noexcept = default;

    // Adopts an already owned reference (no AddRef).
    explicit ComPtr(T* value) noexcept : value_(value) {}

    // Shares ownership: AddRef's the pointer.
    [[nodiscard]] static ComPtr share(T* value) noexcept
    {
        if (value != nullptr)
        {
            value->AddRef();
        }
        return ComPtr(value);
    }

    ~ComPtr() { reset(); }

    ComPtr(const ComPtr& other) noexcept : value_(other.value_)
    {
        if (value_ != nullptr)
        {
            value_->AddRef();
        }
    }

    ComPtr& operator=(const ComPtr& other) noexcept
    {
        ComPtr copy(other);
        swap(copy);
        return *this;
    }

    ComPtr(ComPtr&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}

    ComPtr& operator=(ComPtr&& other) noexcept
    {
        ComPtr moved(std::move(other));
        swap(moved);
        return *this;
    }

    [[nodiscard]] T* get() const noexcept { return value_; }
    [[nodiscard]] T* operator->() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }

    // Out-parameter access: releases the current reference first.
    [[nodiscard]] T** address() noexcept
    {
        reset();
        return &value_;
    }

    [[nodiscard]] void** voidAddress() noexcept
    {
        reset();
        return reinterpret_cast<void**>(&value_);
    }

    [[nodiscard]] T* detach() noexcept { return std::exchange(value_, nullptr); }

    void reset() noexcept
    {
        if (value_ != nullptr)
        {
            T* old = std::exchange(value_, nullptr);
            old->Release();
        }
    }

    void swap(ComPtr& other) noexcept { std::swap(value_, other.value_); }

private:
    T* value_ = nullptr;
};

} // namespace lb
