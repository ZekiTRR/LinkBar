#include "Constants.h"

namespace lb {

Globals& globals() noexcept
{
    static Globals instance;
    return instance;
}

} // namespace lb
