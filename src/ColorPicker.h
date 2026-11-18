// Modal color picker built on a native Windows dialog template. The system
// owns activation, keyboard navigation and the modal message loop.

#pragma once

#include "ColorValue.h"
#include "Constants.h"

namespace lb {

// Shows the picker. Returns true when the user accepted and writes the ARGB
// value into `result`; false on Cancel / close. Fully transparent input is
// normalized to opaque so the picker is never invisible.
[[nodiscard]] bool pickColor(HWND owner, Color initial, Color& result);

} // namespace lb
