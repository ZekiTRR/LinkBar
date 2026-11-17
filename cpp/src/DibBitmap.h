// Minimal 32-bpp DIB section wrapper (direct Win32 memory access).
// Replaces the Delphi THBitmap class without any external dependency.

#pragma once

#include "Constants.h"

#include <cstdint>

namespace lb {

class DibBitmap final
{
public:
    explicit DibBitmap(int bitsPerPixel = 32) noexcept;
    ~DibBitmap();

    DibBitmap(const DibBitmap&) = delete;
    DibBitmap& operator=(const DibBitmap&) = delete;
    DibBitmap(DibBitmap&& other) noexcept;
    DibBitmap& operator=(DibBitmap&& other) noexcept;

    void setSize(int width, int height);

    [[nodiscard]] HDC dc() noexcept;
    [[nodiscard]] HBITMAP handle() const noexcept { return handle_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] int bitsPerPixel() const noexcept { return bpp_; }
    [[nodiscard]] RECT bound() const noexcept;

    // Zeroes the whole surface.
    void clear() noexcept;

    // Forces the alpha channel of all pixels (or of a rectangle) to 255.
    // 32 bpp only; works directly on the DIB section bits.
    void makeOpaque() noexcept;
    void makeOpaqueRect(const RECT& rect) noexcept;

    [[nodiscard]] void* bits() const noexcept { return bits_; }

private:
    void allocate();
    void deleteDc() noexcept;
    void deleteHandle() noexcept;

    HBITMAP handle_ = nullptr;
    HDC dc_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* bits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int bpp_ = 32;
    std::size_t pitch_ = 0;
};

[[nodiscard]] DibBitmap cloneBitmap(const DibBitmap& source);

// Standard blend function used everywhere: pre-multiplied source alpha.
[[nodiscard]] BLENDFUNCTION premultipliedBlend() noexcept;

} // namespace lb
