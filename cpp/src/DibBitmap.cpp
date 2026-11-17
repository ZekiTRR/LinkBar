#include "DibBitmap.h"

#include <algorithm>
#include <cstring>

namespace lb {
namespace {

int bitmapPitch(int width, int bpp) noexcept
{
    return ((width * bpp + 31) & ~31) / 8;
}

} // namespace

DibBitmap::DibBitmap(int bitsPerPixel) noexcept : bpp_(bitsPerPixel)
{
}

DibBitmap::~DibBitmap()
{
    deleteDc();
    deleteHandle();
}

DibBitmap::DibBitmap(DibBitmap&& other) noexcept
    : handle_(other.handle_), dc_(other.dc_), oldBitmap_(other.oldBitmap_),
      bits_(other.bits_), width_(other.width_), height_(other.height_),
      bpp_(other.bpp_), pitch_(other.pitch_)
{
    other.handle_ = nullptr;
    other.dc_ = nullptr;
    other.oldBitmap_ = nullptr;
    other.bits_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
    other.pitch_ = 0;
}

DibBitmap& DibBitmap::operator=(DibBitmap&& other) noexcept
{
    if (this != &other)
    {
        deleteDc();
        deleteHandle();
        handle_ = other.handle_;
        dc_ = other.dc_;
        oldBitmap_ = other.oldBitmap_;
        bits_ = other.bits_;
        width_ = other.width_;
        height_ = other.height_;
        bpp_ = other.bpp_;
        pitch_ = other.pitch_;
        other.handle_ = nullptr;
        other.dc_ = nullptr;
        other.oldBitmap_ = nullptr;
        other.bits_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
        other.pitch_ = 0;
    }
    return *this;
}

void DibBitmap::deleteDc() noexcept
{
    if (dc_ != nullptr)
    {
        SelectObject(dc_, oldBitmap_);
        DeleteDC(dc_);
        dc_ = nullptr;
        oldBitmap_ = nullptr;
    }
}

void DibBitmap::deleteHandle() noexcept
{
    if (handle_ != nullptr)
    {
        DeleteObject(handle_);
        handle_ = nullptr;
        bits_ = nullptr;
    }
}

void DibBitmap::allocate()
{
    deleteDc();
    deleteHandle();
    if (width_ <= 0 || height_ <= 0 || bpp_ == 0)
    {
        return;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = -height_; // top-down, as in the original
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = static_cast<WORD>(bpp_);

    HDC temporary = CreateCompatibleDC(nullptr);
    handle_ = CreateDIBSection(temporary, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
    DeleteDC(temporary);
    if (handle_ == nullptr)
    {
        bits_ = nullptr;
    }
    pitch_ = static_cast<std::size_t>(bitmapPitch(width_, bpp_));
}

void DibBitmap::setSize(int width, int height)
{
    if (width_ == width && height_ == height)
    {
        return;
    }
    width_ = width;
    height_ = height;
    allocate();
}

HDC DibBitmap::dc() noexcept
{
    if (handle_ == nullptr)
    {
        return nullptr;
    }
    if (dc_ == nullptr)
    {
        dc_ = CreateCompatibleDC(nullptr);
        oldBitmap_ = SelectObject(dc_, handle_);
    }
    return dc_;
}

RECT DibBitmap::bound() const noexcept
{
    return RECT{0, 0, width_, height_};
}

void DibBitmap::clear() noexcept
{
    if (bits_ == nullptr)
    {
        return;
    }
    std::memset(bits_, 0, pitch_ * static_cast<std::size_t>(height_));
}

void DibBitmap::makeOpaque() noexcept
{
    if (bits_ == nullptr || bpp_ != 32)
    {
        return;
    }
    // Detach so GDI cannot write cached bits over our changes.
    if (dc_ != nullptr)
    {
        SelectObject(dc_, oldBitmap_);
    }
    auto* pixels = static_cast<std::uint32_t*>(bits_);
    const std::size_t count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    for (std::size_t i = 0; i < count; ++i)
    {
        pixels[i] = 0xFF000000U | (pixels[i] & 0x00FFFFFFU);
    }
    if (dc_ != nullptr)
    {
        oldBitmap_ = SelectObject(dc_, handle_);
    }
}

void DibBitmap::makeOpaqueRect(const RECT& rect) noexcept
{
    if (bits_ == nullptr || bpp_ != 32)
    {
        return;
    }
    if (rect.left < 0 || rect.top < 0 || rect.right > width_ || rect.bottom > height_)
    {
        return;
    }
    if (dc_ != nullptr)
    {
        SelectObject(dc_, oldBitmap_);
    }
    auto* base = static_cast<std::uint32_t*>(bits_);
    for (int y = rect.top; y < rect.bottom; ++y)
    {
        auto* pixels = base + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
                       static_cast<std::size_t>(rect.left);
        for (int x = rect.left; x < rect.right; ++x)
        {
            *pixels = 0xFF000000U | (*pixels & 0x00FFFFFFU);
            ++pixels;
        }
    }
    if (dc_ != nullptr)
    {
        oldBitmap_ = SelectObject(dc_, handle_);
    }
}

DibBitmap cloneBitmap(const DibBitmap& source)
{
    DibBitmap result(source.bitsPerPixel());
    result.setSize(source.width(), source.height());
    if (source.handle() != nullptr && result.dc() != nullptr)
    {
        HDC sourceDc = CreateCompatibleDC(nullptr);
        HGDIOBJ previous = SelectObject(sourceDc, source.handle());
        BitBlt(result.dc(), 0, 0, source.width(), source.height(), sourceDc, 0, 0, SRCCOPY);
        SelectObject(sourceDc, previous);
        DeleteDC(sourceDc);
    }
    return result;
}

BLENDFUNCTION premultipliedBlend() noexcept
{
    return BLENDFUNCTION{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
}

} // namespace lb
