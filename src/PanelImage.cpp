#include "PanelImage.h"

#include "Common.h"
#include "GdiplusCompat.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace lb {

struct PanelImage::Impl
{
    std::unique_ptr<Gdiplus::Image> image;
    int frames = 1;
    std::vector<int> delays;
};

PanelImage::PanelImage() : impl_(std::make_unique<Impl>()) {}
PanelImage::~PanelImage() = default;

bool PanelImage::load(const std::wstring& path)
{
    clear();
    auto image = std::make_unique<Gdiplus::Image>(path.c_str());
    if (image->GetLastStatus() != Gdiplus::Ok)
    {
        return false;
    }
    const GUID timeDimension = Gdiplus::FrameDimensionTime;
    const UINT frames = image->GetFrameCount(&timeDimension);
    impl_->frames = std::max(1, static_cast<int>(frames));
    impl_->image = std::move(image);
    path_ = path;

    impl_->delays.assign(impl_->frames, 100);
    UINT size = 0;
    impl_->image->GetPropertyItemSize(0x5100);
    if (size > 0)
    {
        std::vector<std::byte> buffer(size);
        Gdiplus::PropertyItem* item = reinterpret_cast<Gdiplus::PropertyItem*>(buffer.data());
        if (impl_->image->GetPropertyItem(0x5100, size, item) == Gdiplus::Ok &&
            item->type == 4 && item->length >= sizeof(long))
        {
            const long* values = reinterpret_cast<const long*>(item->value);
            const int count = static_cast<int>(item->length / sizeof(long));
            for (int i = 0; i < impl_->frames && i < count; ++i)
            {
                impl_->delays[static_cast<std::size_t>(i)] =
                    std::clamp(static_cast<int>(values[i]) * 10, 20, 5000);
            }
        }
    }
    return true;
}

void PanelImage::clear() noexcept
{
    impl_->image.reset();
    impl_->frames = 1;
    impl_->delays.clear();
    path_.clear();
}

bool PanelImage::valid() const noexcept { return impl_->image != nullptr; }
int PanelImage::frameCount() const noexcept { return impl_->frames; }

int PanelImage::frameDelay(int frame) const noexcept
{
    if (frame < 0 || static_cast<std::size_t>(frame) >= impl_->delays.size())
    {
        return 100;
    }
    return impl_->delays[static_cast<std::size_t>(frame)];
}

void PanelImage::render(DibBitmap& target, const RECT& clip, ImageLayout layout,
                        int opacity, int frame)
{
    if (!valid() || layout == ImageLayout::None)
    {
        return;
    }
    const int width = rectWidth(clip);
    const int height = rectHeight(clip);
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
    {
        return;
    }
    Gdiplus::Bitmap surface(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&surface);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    if (impl_->frames > 1)
    {
        const GUID timeDimension = Gdiplus::FrameDimensionTime;
        impl_->image->SelectActiveFrame(&timeDimension,
                                        static_cast<UINT>(std::clamp(frame, 0, impl_->frames - 1)));
    }

    const int imageWidth = static_cast<int>(impl_->image->GetWidth());
    const int imageHeight = static_cast<int>(impl_->image->GetHeight());
    const auto drawAt = [&](int x, int y, int w, int h) {
        graphics.DrawImage(impl_->image.get(), x, y, w, h);
    };
    switch (layout)
    {
    case ImageLayout::Stretch:
        drawAt(0, 0, width, height);
        break;
    case ImageLayout::Fit:
    {
        const double scale = std::min(static_cast<double>(width) / std::max(imageWidth, 1),
                                      static_cast<double>(height) / std::max(imageHeight, 1));
        const int w = std::max(1, static_cast<int>(imageWidth * scale));
        const int h = std::max(1, static_cast<int>(imageHeight * scale));
        drawAt((width - w) / 2, (height - h) / 2, w, h);
        break;
    }
    case ImageLayout::Tile:
        for (int y = 0; y < height; y += std::max(imageHeight, 1))
        {
            for (int x = 0; x < width; x += std::max(imageWidth, 1))
            {
                drawAt(x, y, imageWidth, imageHeight);
            }
        }
        break;
    case ImageLayout::Center:
        drawAt((width - imageWidth) / 2, (height - imageHeight) / 2, imageWidth, imageHeight);
        break;
    case ImageLayout::None:
        break;
    }

    HBITMAP handle = nullptr;
    if (surface.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &handle) != Gdiplus::Ok)
    {
        return;
    }
    DibBitmap layer;
    layer.assignFrom(handle, width, height);
    DeleteObject(handle);
    layer.premultiply();

    const double factor = std::clamp(opacity, 0, 100) / 100.0;
    if (factor < 1.0 && layer.bits() != nullptr)
    {
        auto* pixels = static_cast<std::uint32_t*>(layer.bits());
        const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::uint32_t alpha = static_cast<std::uint32_t>((pixels[i] >> 24) * factor + 0.5);
            pixels[i] = (alpha << 24) | (pixels[i] & 0x00FFFFFFU);
        }
    }
    target.compositeOver(layer, clip.left, clip.top);
}

} // namespace lb


