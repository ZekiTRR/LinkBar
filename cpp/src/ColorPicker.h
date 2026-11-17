// Gradient color picker: hue bar, saturation/brightness field, alpha bar,
// preview and hexadecimal input. Uses the original RGB<->HSB math.

#pragma once

#include "Constants.h"

#include <cstdint>
#include <optional>

namespace lb {

[[nodiscard]] std::optional<std::uint32_t> showColorDialog(HWND owner,
                                                          std::uint32_t initialColor);

} // namespace lb
