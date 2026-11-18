// Minimal 32-bpp DIB section wrapper with direct pixel access.
// All pixels of the layered panel are kept premultiplied (0xAARRGGBB), which is
// the format UpdateLayeredWindow(ULW_ALPHA) expects.

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
    void makeOpaque() noexcept;
    void makeOpaqueRect(const RECT& rect) noexcept;

    // Writes a premultiplied color into every pixel of the rectangle.
    void fillRect(const RECT& rect, std::uint32_t premultiplied) noexcept;

    // Source-over composition of a premultiplied bitmap at (x, y); all four
    // channels are blended, so the destination alpha is updated as well.
    void compositeOver(const DibBitmap& source, int x, int y) noexcept;
    void compositeOverScaled(const DibBitmap& source, int x, int y, int width, int height) noexcept;

    // Replaces the content with `source` scaled to width x height (nearest neighbour).
    void assignFrom(HBITMAP source, int width, int height);

    // Converts straight alpha into premultiplied alpha in place.
    void premultiply() noexcept;

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

} // namespace lb
