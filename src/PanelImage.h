// Background image for the panel, including animated GIF support.
// Renders through system GDI+ into a premultiplied DIB surface.

#pragma once

#include "Constants.h"
#include "DibBitmap.h"

#include <memory>
#include <string>

namespace lb {

enum class ImageLayout : std::uint8_t
{
    None = 0,
    Stretch = 1,
    Fit = 2,
    Tile = 3,
    Center = 4,
};

class PanelImage final
{
public:
    PanelImage();
    ~PanelImage();

    PanelImage(const PanelImage&) = delete;
    PanelImage& operator=(const PanelImage&) = delete;

    [[nodiscard]] bool load(const std::wstring& path);
    void clear() noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int frameCount() const noexcept;
    [[nodiscard]] int frameDelay(int frame) const noexcept;
    [[nodiscard]] std::wstring path() const { return path_; }

    void render(DibBitmap& target, const RECT& clip, ImageLayout layout,
                int opacity, int frame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::wstring path_;
};

} // namespace lb
