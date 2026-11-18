#include "DibBitmap.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace lb {
namespace {

int bitmapPitch(int width, int bpp) noexcept
{
    return ((width * bpp + 31) & ~31) / 8;
}

std::uint32_t overPremultiplied(std::uint32_t source, std::uint32_t destination) noexcept
{
    // Porter-Duff "source over" on premultiplied colors:
    // result = source + destination * (1 - sourceAlpha)
    const std::uint32_t inverse = 255U - (source >> 24);
    std::uint32_t result = 0;
    for (int shift = 0; shift < 32; shift += 8)
    {
        const std::uint32_t s = (source >> shift) & 0xFFU;
        const std::uint32_t d = (destination >> shift) & 0xFFU;
        const std::uint32_t channel = std::min(255U, s + (d * inverse + 127U) / 255U);
        result |= channel << shift;
    }
    return result;
}

std::uint32_t lerpPremultiplied(std::uint32_t a, std::uint32_t b, int weight) noexcept
{
    const auto mix = [weight](std::uint32_t from, std::uint32_t to) {
        const int value = static_cast<int>(from) +
                          ((static_cast<int>(to) - static_cast<int>(from)) * weight + 128) / 256;
        return static_cast<std::uint32_t>(std::clamp(value, 0, 255));
    };
    return ((mix(a >> 24, b >> 24) & 0xFFU) << 24) |
           ((mix((a >> 16) & 0xFF, (b >> 16) & 0xFF) & 0xFFU) << 16) |
           ((mix((a >> 8) & 0xFF, (b >> 8) & 0xFF) & 0xFFU) << 8) |
           (mix(a & 0xFF, b & 0xFF) & 0xFFU);
}

std::uint32_t premultiplyPixel(std::uint32_t pixel) noexcept
{
    const std::uint32_t alpha = pixel >> 24;
    if (alpha == 255U)
    {
        return pixel;
    }
    std::uint32_t result = alpha << 24;
    for (int shift = 0; shift < 24; shift += 8)
    {
        const std::uint32_t channel = (pixel >> shift) & 0xFFU;
        result |= ((channel * alpha + 127U) / 255U) << shift;
    }
    return result;
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

void DibBitmap::fillRect(const RECT& rect, std::uint32_t premultiplied) noexcept
{
    if (bits_ == nullptr)
    {
        return;
    }
    GdiFlush();
    const int left = std::max(static_cast<int>(rect.left), 0);
    const int top = std::max(static_cast<int>(rect.top), 0);
    const int right = std::min(static_cast<int>(rect.right), width_);
    const int bottom = std::min(static_cast<int>(rect.bottom), height_);
    auto* base = static_cast<std::uint32_t*>(bits_);
    for (int y = top; y < bottom; ++y)
    {
        std::uint32_t* row = base + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
        std::fill(row + left, row + right, premultiplied);
    }
}

void DibBitmap::compositeOver(const DibBitmap& source, int x, int y) noexcept
{
    if (bits_ == nullptr || source.bits_ == nullptr || source.bpp_ != 32 || bpp_ != 32)
    {
        return;
    }
    GdiFlush();
    const int sourceWidth = source.width_;
    const int left = std::max(x, 0);
    const int top = std::max(y, 0);
    const int right = std::min(x + sourceWidth, width_);
    const int bottom = std::min(y + static_cast<int>(source.height_), height_);
    const auto* from = static_cast<const std::uint32_t*>(source.bits_);
    auto* to = static_cast<std::uint32_t*>(bits_);
    for (int dy = top; dy < bottom; ++dy)
    {
        const std::uint32_t* sourceRow = from + static_cast<std::size_t>(dy - y) *
                                                static_cast<std::size_t>(sourceWidth);
        std::uint32_t* targetRow = to + static_cast<std::size_t>(dy) *
                                        static_cast<std::size_t>(width_);
        for (int dx = left; dx < right; ++dx)
        {
            const std::uint32_t pixel = sourceRow[dx - x];
            const std::uint32_t alpha = pixel >> 24;
            if (alpha == 255U)
            {
                targetRow[dx] = pixel;
            }
            else if (alpha != 0U)
            {
                targetRow[dx] = overPremultiplied(pixel, targetRow[dx]);
            }
        }
    }
}

void DibBitmap::compositeOverScaled(const DibBitmap& source, int x, int y, int width, int height) noexcept
{
    if (bits_ == nullptr || source.bits_ == nullptr || source.bpp_ != 32 || bpp_ != 32 ||
        width <= 0 || height <= 0 || source.width() <= 0 || source.height() <= 0)
    {
        return;
    }
    GdiFlush();
    const int left = std::max(x, 0);
    const int top = std::max(y, 0);
    const int right = std::min(x + width, width_);
    const int bottom = std::min(y + height, height_);
    const auto* from = static_cast<const std::uint32_t*>(source.bits_);
    auto* to = static_cast<std::uint32_t*>(bits_);
    const int sourceWidth = source.width();
    const int sourceHeight = source.height();
    const auto sample = [&](int sx, int sy) -> std::uint32_t {
        const int cx = std::clamp(sx, 0, sourceWidth - 1);
        const int cy = std::clamp(sy, 0, sourceHeight - 1);
        return from[static_cast<std::size_t>(cy) * static_cast<std::size_t>(sourceWidth) +
                    static_cast<std::size_t>(cx)];
    };
    // Bilinear sampling in 16.16 fixed point; colors stay premultiplied.
    for (int dy = top; dy < bottom; ++dy)
    {
        const long long fy = ((static_cast<long long>(dy - y) * 2 + 1) * sourceHeight * 32768) /
                             height - 32768;
        const int sy0 = static_cast<int>(fy >> 16);
        const int wy = static_cast<int>((fy & 0xFFFF) >> 8);
        std::uint32_t* targetRow = to + static_cast<std::size_t>(dy) * static_cast<std::size_t>(width_);
        for (int dx = left; dx < right; ++dx)
        {
            const long long fx = ((static_cast<long long>(dx - x) * 2 + 1) * sourceWidth * 32768) /
                                 width - 32768;
            const int sx0 = static_cast<int>(fx >> 16);
            const int wx = static_cast<int>((fx & 0xFFFF) >> 8);
            const std::uint32_t topColor = lerpPremultiplied(sample(sx0, sy0),
                                                              sample(sx0 + 1, sy0), wx);
            const std::uint32_t bottomColor = lerpPremultiplied(sample(sx0, sy0 + 1),
                                                                 sample(sx0 + 1, sy0 + 1), wx);
            const std::uint32_t pixel = lerpPremultiplied(topColor, bottomColor, wy);
            const std::uint32_t alpha = pixel >> 24;
            if (alpha == 255U)
            {
                targetRow[dx] = pixel;
            }
            else if (alpha != 0U)
            {
                targetRow[dx] = overPremultiplied(pixel, targetRow[dx]);
            }
        }
    }
}

void DibBitmap::assignFrom(HBITMAP source, int width, int height)
{
    if (source == nullptr || width <= 0 || height <= 0)
    {
        return;
    }
    BITMAP metrics{};
    if (GetObjectW(source, sizeof(metrics), &metrics) == 0 || metrics.bmWidth <= 0 ||
        metrics.bmHeight <= 0)
    {
        return;
    }
    const int sourceWidth = metrics.bmWidth;
    const int sourceHeight = metrics.bmHeight;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(sourceWidth) *
                                      static_cast<std::size_t>(sourceHeight));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = sourceWidth;
    info.bmiHeader.biHeight = -sourceHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    const int copied = GetDIBits(dc, source, 0, static_cast<UINT>(sourceHeight), pixels.data(),
                                 &info, DIB_RGB_COLORS);
    DeleteDC(dc);
    if (copied == 0)
    {
        return;
    }

    setSize(width, height);
    if (bits_ == nullptr)
    {
        return;
    }
    auto* target = static_cast<std::uint32_t*>(bits_);
    for (int y = 0; y < height; ++y)
    {
        const auto sy = static_cast<std::size_t>(y) * static_cast<std::size_t>(sourceHeight) /
                        static_cast<std::size_t>(height);
        for (int x = 0; x < width; ++x)
        {
            const auto sx = static_cast<std::size_t>(x) * static_cast<std::size_t>(sourceWidth) /
                            static_cast<std::size_t>(width);
            target[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] = pixels[sy * static_cast<std::size_t>(sourceWidth) + sx];
        }
    }
}

void DibBitmap::premultiply() noexcept
{
    if (bits_ == nullptr || bpp_ != 32)
    {
        return;
    }
    GdiFlush();
    auto* pixels = static_cast<std::uint32_t*>(bits_);
    const std::size_t count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    for (std::size_t i = 0; i < count; ++i)
    {
        pixels[i] = premultiplyPixel(pixels[i]);
    }
}

} // namespace lb
